#!/bin/bash

###############################################
# Script d'installation du Video Processor
###############################################

set -e

echo "╔════════════════════════════════════════╗"
echo "║   Installation Video Processor         ║"
echo "╚════════════════════════════════════════╝"
echo ""

# Répertoire du script : ancre les chemins relatifs, indépendamment du cwd d'appel
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Contexte d'exécution : "dev" (chemins relatifs) ou "prod" (chemins absolus, /usr/local/bin etc.)
# Même variable que video_processor.sh : bascule les deux scripts ensemble :
# APP_CONTEXT=prod sudo -E ./install.sh
APP_CONTEXT="${APP_CONTEXT:-dev}"

# Le check root n'a de sens qu'en mode prod (écriture dans /usr/local/bin, /var/www...)
# En mode dev, tout reste dans SCRIPT_DIR : pas besoin de privilèges élevés
if [ "$APP_CONTEXT" = "prod" ] && [ "$EUID" -ne 0 ]; then
    echo "Ce script doit être exécuté en root (sudo -E) en mode prod"
    exit 1
fi

# Compilation du segmenteur
echo "Compilation du video_segmenter..."
if [ -f "video_segmenter.c" ]; then
    if ! pkg-config --exists libavformat libavcodec libavutil; then
        echo "Erreur: FFmpeg (libavformat/libavcodec/libavutil) introuvable via pkg-config"
        exit 1
    fi
    # --cflags avant le fichier source, --libs après : requis par les linkers
    # modernes (--as-needed par défaut sur Linux), sinon les symboles FFmpeg
    # ne sont pas résolus car aucune référence n'existe encore à ce stade.
    gcc -Wall -Wextra -O2 $(pkg-config --cflags libavformat libavcodec libavutil) \
        -o video_segmenter video_segmenter.c \
        $(pkg-config --libs libavformat libavcodec libavutil)
    echo "Compilation réussie"
else
    echo "Fichier video_segmenter.c introuvable"
    exit 1
fi

# Détermine le préfixe d'installation selon le contexte
if [ "$APP_CONTEXT" = "prod" ]; then
    INSTALL_PREFIX="/usr/local/bin"
    DATA_PREFIX=""   # racine du système : /tmp, /var, etc.
else
    INSTALL_PREFIX="$SCRIPT_DIR/usr/local/bin"
    DATA_PREFIX="$SCRIPT_DIR"
fi

# Installation des binaires
echo "Installation des binaires..."
mkdir -p "$INSTALL_PREFIX"
install -m 755 video_segmenter "$INSTALL_PREFIX/"
install -m 755 video_processor.sh "$INSTALL_PREFIX/"
echo "Binaires installés dans $INSTALL_PREFIX/"

# Création des dossiers
echo "Création des dossiers..."
mkdir -p "$DATA_PREFIX/tmp/videos/"{processing,done,error}
mkdir -p "$DATA_PREFIX/var/www/html/streams"
mkdir -p "$DATA_PREFIX/var/log"
touch "$DATA_PREFIX/var/log/video_processor.log"
echo "Dossiers créés"

# Config. des permissions
echo "Configuration des permissions..."
if id "www-data" &>/dev/null; then
    chown -R www-data:www-data "$DATA_PREFIX/var/www/html/streams"
    chown www-data:www-data "$DATA_PREFIX/var/log/video_processor.log"
    echo "Permissions configurées (utilisateur www-data)"
else
    echo "Utilisateur www-data introuvable, permissions non modifiées"
fi

# Config. du cron
echo "Configuration du cron..."
# cron ignore le cwd d'appel : les chemins doivent TOUJOURS être absolus,
# même en mode dev (SCRIPT_DIR est déjà un chemin absolu résolu plus haut).
CRON_LINE="*/5 * * * * $INSTALL_PREFIX/video_processor.sh >> $DATA_PREFIX/var/log/video_processor_cron.log 2>&1"
# Nettoyage hebdomadaire des anciens fichiers (dimanche à 3h)
CLEANUP_LINE="0 3 * * 0 $INSTALL_PREFIX/video_processor.sh cleanup 7"

# Ajoute au cron si pas déjà présent
(crontab -l 2>/dev/null | grep -v video_processor.sh; echo "$CRON_LINE"; echo "$CLEANUP_LINE") | crontab -

echo "Tâches cron configurées (vérification toutes les 5 min)"

# Test de l'install.
echo ""
echo "Test de l'installation..."
if "$INSTALL_PREFIX/video_segmenter" 2>&1 | grep -q "Usage"; then
    echo "video_segmenter fonctionne"
else
    echo "video_segmenter ne fonctionne pas correctement"
fi

if [ -x "$INSTALL_PREFIX/video_processor.sh" ]; then
    echo "video_processor.sh est exécutable"
else
    echo "video_processor.sh n'est pas exécutable"
fi

echo ""
echo "╔════════════════════════════════════════╗"
echo "║   Installation terminée !              ║"
echo "╚════════════════════════════════════════╝"
echo ""
echo "Contexte:            $APP_CONTEXT"
echo "Configuration:"
echo "   Dossier surveillé:  $DATA_PREFIX/tmp/videos"
echo "   Dossier de sortie:  $DATA_PREFIX/var/www/html/streams"
echo "   Logs:               $DATA_PREFIX/var/log/video_processor.log"
echo "   Fréquence:          Toutes les 5 minutes"
echo ""
echo "Commandes utiles:"
echo "   Test manuel:        $INSTALL_PREFIX/video_processor.sh"
echo "   Mode surveillance:  $INSTALL_PREFIX/video_processor.sh watch"
echo "   Voir les logs:      tail -f $DATA_PREFIX/var/log/video_processor.log"
echo "   Voir le cron:       crontab -l"
echo ""
echo "Pour tester maintenant:"
echo "   cp votre_video.mp4 $DATA_PREFIX/tmp/videos/"
echo "   $INSTALL_PREFIX/video_processor.sh"
echo ""