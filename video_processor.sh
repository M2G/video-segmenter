#!/bin/bash

#############################################
# Video Processor : Script de surveillance
# Traite automatiquement les MP4 d'un dossier
#############################################

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Chemins configurables via variables d'environnement, avec fallback dev
WATCH_DIR="${WATCH_DIR:-$SCRIPT_DIR/tmp/videos}"
OUTPUT_DIR="${OUTPUT_DIR:-$SCRIPT_DIR/var/www/html/streams}"
PROCESSING_DIR="${PROCESSING_DIR:-$WATCH_DIR/processing}"
DONE_DIR="${DONE_DIR:-$WATCH_DIR/done}"
ERROR_DIR="${ERROR_DIR:-$WATCH_DIR/error}"
LOG_FILE="${LOG_FILE:-$SCRIPT_DIR/var/log/video_processor.log}"
LOCK_FILE="${LOCK_FILE:-$SCRIPT_DIR/var/run/video_processor.lock}"
SEGMENTER="${SEGMENTER:-$SCRIPT_DIR/usr/local/bin/video_segmenter}"

# Paramètres de segmentation
SEGMENT_DURATION="${SEGMENT_DURATION:-10}"
MAX_SEGMENTS="${MAX_SEGMENTS:-0}"
EXTENSION="${EXTENSION:-.ts}"

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" | tee -a "$LOG_FILE"
}

error() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] ERROR: $1" | tee -a "$LOG_FILE" >&2
}

init_directories() {
    mkdir -p "$WATCH_DIR" "$OUTPUT_DIR" "$PROCESSING_DIR" "$DONE_DIR" "$ERROR_DIR"
    mkdir -p "$(dirname "$LOG_FILE")"
    mkdir -p "$(dirname "$LOCK_FILE")"
    touch "$LOG_FILE"
}

is_file_stable() {
    local file="$1"

    if [ ! -f "$file" ]; then
        log "Fichier introuvable: $file"
        return 1
    fi

    local size1 size2
    if [[ "$OSTYPE" == "darwin"* ]]; then
        size1=$(stat -f%z "$file" 2>/dev/null || echo 0)
        sleep 2
        size2=$(stat -f%z "$file" 2>/dev/null || echo 0)
    else
        size1=$(stat -c%s "$file" 2>/dev/null || echo 0)
        sleep 2
        size2=$(stat -c%s "$file" 2>/dev/null || echo 0)
    fi

    log "Vérification stabilité: taille1=$size1, taille2=$size2"

    if [ "$size1" -eq "$size2" ] && [ "$size1" -gt 0 ]; then
        return 0
    else
        return 1
    fi
}

acquire_lock() {
    if [ -f "$LOCK_FILE" ]; then
        local pid=$(cat "$LOCK_FILE")
        if kill -0 "$pid" 2>/dev/null; then
            log "Une autre instance est déjà en cours (PID: $pid)"
            return 1
        else
            log "Suppression du verrou obsolète (PID: $pid)"
            rm -f "$LOCK_FILE"
        fi
    fi

    echo $$ > "$LOCK_FILE"
    return 0
}

release_lock() {
    rm -f "$LOCK_FILE"
}

process_video() {
    local input_file="$1"
    local filename=$(basename "$input_file")
    local name_without_ext
    shopt -s nocasematch
    if [[ "$filename" =~ ^(.+)\.mp4$ ]]; then
        name_without_ext="${BASH_REMATCH[1]}"
    else
        name_without_ext="$filename"
    fi
    shopt -u nocasematch

    log "========================================="
    log "Traitement: $filename"
    log "========================================="

    local processing_file="$PROCESSING_DIR/$filename"
    if ! mv "$input_file" "$processing_file"; then
        error "Impossible de déplacer le fichier vers processing"
        return 1
    fi

    log "Vérification de la stabilité du fichier..."
    local attempts=0
    local max_attempts=5

    while ! is_file_stable "$processing_file"; do
        attempts=$((attempts + 1))
        if [ $attempts -gt $max_attempts ]; then
            error "Timeout: le fichier n'est pas stable après $((max_attempts * 2)) secondes"
            mv "$processing_file" "$ERROR_DIR/"
            return 1
        fi
        log "Tentative $attempts/$max_attempts - Fichier potentiellement en cours d'écriture, attente..."
    done

    log "Fichier stable, début du traitement"

    local output_subdir="$OUTPUT_DIR/$name_without_ext"
    local index_file="$output_subdir/${name_without_ext}.m3u8"

    mkdir -p "$output_subdir"

    log "Lancement de la segmentation..."
    log "Commande: $SEGMENTER \"$processing_file\" \"$output_subdir\" \"$index_file\" \"segment\" \"$EXTENSION\" \"$SEGMENT_DURATION\" \"$MAX_SEGMENTS\""

    if "$SEGMENTER" "$processing_file" "$output_subdir" "$index_file" "segment" "$EXTENSION" "$SEGMENT_DURATION" "$MAX_SEGMENTS" >> "$LOG_FILE" 2>&1; then
        log "Segmentation réussie: $filename"

        mv "$processing_file" "$DONE_DIR/"

        cat > "$output_subdir/info.txt" <<EOF
Fichier source: $filename
Date de traitement: $(date '+%Y-%m-%d %H:%M:%S')
Durée segments: ${SEGMENT_DURATION}s
Index: ${name_without_ext}.m3u8
EOF

        log "Fichiers générés dans: $output_subdir"
        log "Nombre de segments: $(ls -1 "$output_subdir"/segment-*.ts 2>/dev/null | wc -l)"
        return 0
    else
        error "Échec de la segmentation: $filename"
        mv "$processing_file" "$ERROR_DIR/"
        return 1
    fi
}

process_all_videos() {
    local count=0
    local success=0
    local failed=0

    log "Recherche de vidéos à traiter dans: $WATCH_DIR"

    shopt -s nocaseglob
    for video in "$WATCH_DIR"/*.mp4; do
        if [ ! -f "$video" ]; then
            continue
        fi

        count=$((count + 1))

        if process_video "$video"; then
            success=$((success + 1))
        else
            failed=$((failed + 1))
        fi
    done
    shopt -u nocaseglob

    if [ $count -gt 0 ]; then
        log "========================================="
        log "Résumé: $count vidéo(s) traitée(s)"
        log "Succès: $success | Échecs: $failed"
        log "========================================="
    else
        log "Aucune vidéo à traiter"
    fi
}

watch_mode() {
    log "Mode surveillance activé (Ctrl+C pour arrêter)"

    while true; do
        process_all_videos
        sleep 30
    done
}

cleanup_old_files() {
    local days=${1:-7}
    log "Nettoyage des fichiers de plus de $days jours..."

    find "$DONE_DIR" -name "*.mp4" -mtime +$days -delete 2>/dev/null
    find "$ERROR_DIR" -name "*.mp4" -mtime +$days -delete 2>/dev/null

    log "Nettoyage terminé"
}

main() {
    init_directories

    if [ ! -x "$SEGMENTER" ]; then
        error "Le binaire $SEGMENTER n'existe pas ou n'est pas exécutable"
        exit 1
    fi

    if ! acquire_lock; then
        exit 1
    fi

    trap release_lock EXIT INT TERM

    case "${1:-}" in
        watch)
            watch_mode
            ;;
        cleanup)
            cleanup_old_files "${2:-7}"
            ;;
        *)
            process_all_videos
            ;;
    esac
}

if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    cat <<EOF
Usage: $0 [OPTIONS]

OPTIONS:
  (aucun)       Traite tous les MP4 du dossier une seule fois
  watch         Mode surveillance continue (boucle infinie)
  cleanup [N]   Nettoie les fichiers de plus de N jours (défaut: 7)
  -h, --help    Affiche cette aide

ENVIRONMENT VARIABLES:
  WATCH_DIR, OUTPUT_DIR, PROCESSING_DIR, DONE_DIR, ERROR_DIR
  LOG_FILE, LOCK_FILE, SEGMENTER
  SEGMENT_DURATION, MAX_SEGMENTS, EXTENSION

EXEMPLES:
  $0                    # Traite une fois
  $0 watch              # Surveillance continue
  $0 cleanup 14         # Nettoie les fichiers de +14 jours

LOGS:
  $LOG_FILE
EOF
    exit 0
fi

main "$@"