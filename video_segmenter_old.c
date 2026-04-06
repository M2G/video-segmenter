#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/mathematics.h"

// Constantes pour les limites du système
#define MAX_FILENAME_LENGTH 512
#define MAX_SEGMENTS 4096

/*
 * NOTE :
 * avformat_alloc_output_context2 : permet de créer un contexte de format de sortie pour le conteneur MPEG-TS, requis pour HLS.
 * avformat_new_stream : alloue un nouveau flux dans le conteneur de sortie, nécessaire pour chaque flux vidéo/audio.
 * avformat_open_input : ouvrir un flux d'entrée et lire l'en-tête. Les codecs ne sont pas ouverts. Le flux doit être fermé avec avformat_close_input() .
 * avformat_close_input : libère ainsi que tout son contenu et définissez *s sur NULL.
 * avformat_find_stream_info : lire les paquets d'un fichier multimédia pour obtenir les informations du flux.
 * avformat_free_context : libère le contexte de format et tous les flux associés. Doit être appelé après avformat_alloc_output_context2 pour éviter les fuites de mémoire.
 * avformat_write_header : écrit l'en-tête du conteneur de sortie (PAT/PMT pour MPEG-TS) avant d'écrire les paquets.
 * av_interleaved_write_frame : écrit un paquet dans le conteneur de sortie, en gérant l'entrelacement audio/vidéo.
 * av_write_trailer : écrit le trailer du conteneur de sortie (finalisation) après avoir écrit tous les paquets.
 * av_packet_unref : libère la mémoire d'un paquet après utilisation pour éviter les fuites.
 * av_rescale_q_rnd : rescale les timestamps d'un paquet pour correspondre à la base temporelle du flux de sortie, en arrondissant de manière appropriée.
 * av_q2d : convertit une base temporelle (AVRational) en un facteur de conversion en secondes.
 * av_rescale_q : rescale la durée d'un paquet pour correspondre à la base temporelle du flux de sortie.
 * avcodec_parameters_copy : copie les paramètres du codec (résolution, bitrate, etc.) du flux d'entrée vers le flux de sortie pour assurer la compatibilité.
 * avio_open : ouvre un fichier pour écriture binaire, utilisé pour créer les fichiers segments MPEG-TS.
 * avio_closep : ferme un fichier ouvert avec avio_open.
 * avio_flush : force l'écriture de tous les buffers dans le fichier, utile avant de fermer un segment pour garantir que tout est écrit sur le disque.
 */

/**
 * Crée un flux de sortie en copiant les paramètres d'un flux d'entrée
 * Utilisé pour dupliquer les flux vidéo/audio vers les fichiers segments
 */
static AVStream *add_output_stream(AVFormatContext *output_format_context, AVStream *input_stream) {
    // Alloue un nouveau flux dans le conteneur de sortie
    AVStream *output_stream = avformat_new_stream(output_format_context, NULL);
    if (!output_stream) {
        fprintf(stderr, "Erreur: Impossible d'allouer le flux de sortie\n");
        return NULL;
    }

    // Copie tous les paramètres du codec (résolution, bitrate, etc.)
    if (avcodec_parameters_copy(output_stream->codecpar, input_stream->codecpar) < 0) {
        fprintf(stderr, "Erreur: Impossible de copier les paramètres du codec\n");
        return NULL;
    }
    // Réinitialise le tag codec (NOTE: requis pour certains conteneurs)
    output_stream->codecpar->codec_tag = 0;
    // Conserve la base temporelle pour les timestamps
    output_stream->time_base = input_stream->time_base;
    return output_stream;
}

/**
 * Génère le fichier playlist M3U8 pour le streaming HLS
 * Utilise un fichier temporaire + rename pour éviter lectures incomplètes
 *
 * Accède directement au ring buffer - supprime la copie intermédiaire O(L)
 * maxDuration est maintenu de façon incrémentale par l'appelant (O(1)/segment)
 *
 * NOTE : O(L) - temps linéaire (proportionnel à L)
 * Le temps grandit proportionnellement à L (ici max_list_length, le nombre de segments actifs dans la playlist).
 */
static int write_index_file(
    const char *index,
    const char *tmp_index,
    unsigned int numsegments,
    const unsigned int *actual_segment_duration,
    unsigned int segment_number_offset,
    const char *output_prefix,
    const char *output_file_extension,
    unsigned int maxDuration,
    int islast
) {
    if (numsegments < 1) return 0;
    // Ouvre le fichier temporaire pour écriture
    FILE *tmp_index_fp = fopen(tmp_index, "w");
    if (!tmp_index_fp) {
        fprintf(stderr, "Erreur: Impossible d'ouvrir '%s': %s\n", tmp_index, strerror(errno));
        return -1;
    }

    // maxDuration fourni par l'appelant - plus de scan O(L) ici
    // Écrit l'en-tête M3U8
    fprintf(tmp_index_fp, "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-MEDIA-SEQUENCE:%u\n#EXT-X-TARGETDURATION:%u\n",
            segment_number_offset, maxDuration);

    // Liste tous les segments avec leur durée individuelle
    for (unsigned int i = 0; i < numsegments; i++) {
        // EXTINF : durée du segment
        // Nom du fichier segment
        if (fprintf(tmp_index_fp, "#EXTINF:%u,\n%s-%u%s\n",
                   actual_segment_duration[i], output_prefix,
                   i + segment_number_offset, output_file_extension) < 0) {
            fprintf(stderr, "Erreur: Échec d'écriture dans le fichier index\n");
            fclose(tmp_index_fp);
            return -1;
        }
    }
    // Marque la fin du stream si c'est le dernier segment
    if (islast && fprintf(tmp_index_fp, "#EXT-X-ENDLIST\n") < 0) {
        fprintf(stderr, "Erreur: Échec d'écriture de EXT-X-ENDLIST\n");
        fclose(tmp_index_fp);
        return -1;
    }

    fclose(tmp_index_fp);
    // Renommage : pour garantir que lecteurs voient fichier complet ou ancien
    if (rename(tmp_index, index) != 0) {
        fprintf(stderr, "Erreur: Impossible de renommer '%s' en '%s': %s\n",
                tmp_index, index, strerror(errno));
        return -1;
    }

    return 0;
}

/**
 * Fonction de segmentation vidéo en chunks HLS
 */
static int segment_video(
    const char *input_file,
    const char *base_dirpath,
    const char *output_index_file,
    const char *base_file_name,
    const char *base_file_extension,
    int segment_length,
    int max_list_length
) {
    // Contextes FFmpeg pour lecture et écriture
    AVFormatContext *input_ctx = NULL;
    AVFormatContext *output_ctx = NULL;
    // Buffer pour noms de fichiers
    char current_output_filename[MAX_FILENAME_LENGTH];
    char tmp_output_index_file[MAX_FILENAME_LENGTH];
    // Tableau des durées réelles de chaque segment
    unsigned int actual_segment_durations[MAX_SEGMENTS + 1];
    // maxDuration maintenu de façon incrémentale : O(1) par segment
    // au lieu d'un scan O(L) dans write_index_file à chaque découpe
    unsigned int max_duration = 0;
    // Compteurs et indices
    unsigned int num_segments = 0;
    unsigned int output_index = 1;
    unsigned int list_offset = 1;
    // Timestamps pour calcul durée segments
    double segment_start_time = 0.0;
    double packet_time = 0.0;
    double prev_packet_time = 0.0;
    // Indices des flux dans les conteneurs
    int in_video_index = -1;
    int in_audio_index = -1;
    // Flag pour attendre première keyframe avant segmentation
    int wait_first_keyframe = 1;
    int ret;
    // Construit le nom du fichier index temporaire
    snprintf(tmp_output_index_file, MAX_FILENAME_LENGTH, "%s.tmp", output_index_file);

    // Ouvre le fichier vidéo source
    ret = avformat_open_input(&input_ctx, input_file, NULL, NULL);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "Erreur: Impossible d'ouvrir '%s': %s\n", input_file, errbuf);
        return -1;
    }
    // Lit les métadonnées des flux (codecs, résolution, etc.)
    if (avformat_find_stream_info(input_ctx, NULL) < 0) {
        fprintf(stderr, "Erreur: Impossible de lire les informations des flux\n");
        avformat_close_input(&input_ctx);
        return -1;
    }

    // Identifie les flux vidéo et audio (prend le premier de chaque type)
    for (unsigned int i = 0; i < input_ctx->nb_streams; i++) {
        enum AVMediaType type = input_ctx->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && in_video_index < 0) {
            in_video_index = i;
        } else if (type == AVMEDIA_TYPE_AUDIO && in_audio_index < 0) {
            in_audio_index = i;
        }
    }
    // Flux vidéo obligatoire
    if (in_video_index < 0) {
        fprintf(stderr, "Erreur: Aucun flux vidéo trouvé dans le fichier source\n");
        avformat_close_input(&input_ctx);
        return -1;
    }

    printf("Flux vidéo: index %d\n", in_video_index);
    if (in_audio_index >= 0) printf("Flux audio: index %d\n", in_audio_index);

    // Crée le contexte de sortie au format MPEG-TS (requis pour HLS)
    avformat_alloc_output_context2(&output_ctx, NULL, "mpegts", NULL);
    if (!output_ctx) {
        fprintf(stderr, "Erreur: Impossible d'allouer le contexte de sortie\n");
        avformat_close_input(&input_ctx);
        return -1;
    }

    // Ajoute le flux vidéo au conteneur de sortie
    AVStream *out_video_st = add_output_stream(output_ctx, input_ctx->streams[in_video_index]);
    if (!out_video_st) {
        avformat_free_context(output_ctx);
        avformat_close_input(&input_ctx);
        return -1;
    }

    AVStream *out_audio_st = NULL;
    // Ajoute le flux audio si présent
    if (in_audio_index >= 0) {
        out_audio_st = add_output_stream(output_ctx, input_ctx->streams[in_audio_index]);
        if (!out_audio_st) {
            avformat_free_context(output_ctx);
            avformat_close_input(&input_ctx);
            return -1;
        }
    }

    // Crée le premier fichier segment
    snprintf(current_output_filename, MAX_FILENAME_LENGTH,
             "%s/%s-%u%s", base_dirpath, base_file_name, output_index, base_file_extension);
    // Ouvre le fichier pour écriture binaire
    if (avio_open(&output_ctx->pb, current_output_filename, AVIO_FLAG_WRITE) < 0) {
        fprintf(stderr, "Erreur: Impossible d'ouvrir '%s'\n", current_output_filename);
        avformat_free_context(output_ctx);
        avformat_close_input(&input_ctx);
        return -1;
    }

    printf("Démarrage du segment: '%s'\n", current_output_filename);
    // Écrit l'en-tête MPEG-TS (PAT/PMT tables)
    if (avformat_write_header(output_ctx, NULL) < 0) {
        fprintf(stderr, "Erreur: Impossible d'écrire l'en-tête\n");
        avio_closep(&output_ctx->pb);
        avformat_free_context(output_ctx);
        avformat_close_input(&input_ctx);
        return -1;
    }

    // Facteur de conversion PTS -> temps en secondes
    const double vid_pts2time = av_q2d(input_ctx->streams[in_video_index]->time_base);
    const int out_video_index = out_video_st->index;
    const int out_audio_index = out_audio_st ? out_audio_st->index : -1;

    // Boucle principale : lit et écrit les paquets
    AVPacket pkt;
    while (av_read_frame(input_ctx, &pkt) >= 0) {
        int is_keyframe = 0;
        int original_stream_index = pkt.stream_index;
        // Traitement paquet vidéo
        if (pkt.stream_index == in_video_index) {
            // Convertit PTS en secondes
            packet_time = pkt.pts * vid_pts2time;
            // Vérifie si c'est une keyframe (I-frame)
            is_keyframe = pkt.flags & AV_PKT_FLAG_KEY;
            // Initialise au premier keyframe trouvé
            if (is_keyframe && wait_first_keyframe) {
                wait_first_keyframe = 0;
                prev_packet_time = packet_time;
                segment_start_time = packet_time;
            }
            // Remappe vers l'index de sortie
            pkt.stream_index = out_video_index;
            // Traitement paquet audio
        } else if (pkt.stream_index == in_audio_index && out_audio_st) {
            pkt.stream_index = out_audio_index;
            // Ignore les autres flux (sous-titres, etc.)
        } else {
            av_packet_unref(&pkt);
            continue;
        }
        // Skip jusqu'au premier keyframe
        if (wait_first_keyframe) {
            av_packet_unref(&pkt);
            continue;
        }

        // Vérifie si on doit créer un nouveau segment
        // Condition : keyframe ET durée cible atteinte (avec marge de 250ms)
        if (is_keyframe && (packet_time - segment_start_time) >= (segment_length - 0.25)) {
            // Force l'écriture des buffers
            avio_flush(output_ctx->pb);
            // Ferme le fichier segment actuel
            avio_closep(&output_ctx->pb);
            // Enregistre la durée réelle du segment (arrondie)
            unsigned int seg_duration = (unsigned int)rint(prev_packet_time - segment_start_time);
            actual_segment_durations[num_segments] = seg_duration;
            // Update : incrémentale O(1) - plus de scan dans write_index_file
            if (seg_duration > max_duration) max_duration = seg_duration;
            num_segments++;

            // Gestion fenêtre glissante : supprime vieux segments si limite atteinte
            if (max_list_length > 0 && num_segments > (unsigned int)max_list_length) {
                // Nom du vieux segment - construit avant avio_open du suivant
                // unlink différé : exécuté après avio_open pour masquer la latence I/O
                char old_segment_filename[MAX_FILENAME_LENGTH];
                snprintf(old_segment_filename, MAX_FILENAME_LENGTH,
                        "%s/%s-%u%s", base_dirpath, base_file_name, list_offset, base_file_extension);
                list_offset++;
                num_segments--;
                // Décale le tableau des durées
                memmove(actual_segment_durations, actual_segment_durations + 1,
                       num_segments * sizeof(actual_segment_durations[0]));
                // Recalcule max_duration seulement si le segment supprimé était le max
                if (actual_segment_durations[0] == max_duration) {
                    max_duration = 0;
                    for (unsigned int i = 0; i < num_segments; i++)
                        if (actual_segment_durations[i] > max_duration)
                            max_duration = actual_segment_durations[i];
                }
                // Update : le fichier playlist M3U8
                write_index_file(output_index_file, tmp_output_index_file,
                               num_segments, actual_segment_durations, list_offset,
                               base_file_name, base_file_extension, max_duration, 0);
                // Sécurité : stop si trop de segments
                if (num_segments >= MAX_SEGMENTS) {
                    fprintf(stderr, "Limite de segments atteinte (%u)\n", MAX_SEGMENTS);
                    av_packet_unref(&pkt);
                    break;
                }
                // Incrémente le numéro de segment
                output_index++;
                // Crée le nouveau nom de fichier
                snprintf(current_output_filename, MAX_FILENAME_LENGTH,
                        "%s/%s-%u%s", base_dirpath, base_file_name, output_index, base_file_extension);
                // Ouvre le nouveau fichier segment
                if (avio_open(&output_ctx->pb, current_output_filename, AVIO_FLAG_WRITE) < 0) {
                    fprintf(stderr, "Erreur: Impossible d'ouvrir '%s'\n", current_output_filename);
                    av_packet_unref(&pkt);
                    break;
                }
                // unlink différé : après avio_open pour masquer latence derrière ouverture fichier
                unlink(old_segment_filename);
            } else {
                // Update : le fichier playlist M3U8
                write_index_file(output_index_file, tmp_output_index_file,
                               num_segments, actual_segment_durations, list_offset,
                               base_file_name, base_file_extension, max_duration, 0);
                // Sécurité : arrête si trop de segments
                if (num_segments >= MAX_SEGMENTS) {
                    fprintf(stderr, "Limite de segments atteinte (%u)\n", MAX_SEGMENTS);
                    av_packet_unref(&pkt);
                    break;
                }
                // Incrémente le numéro de segment
                output_index++;
                // Crée le nouveau nom de fichier
                snprintf(current_output_filename, MAX_FILENAME_LENGTH,
                        "%s/%s-%u%s", base_dirpath, base_file_name, output_index, base_file_extension);
                // Ouvre le nouveau fichier segment
                if (avio_open(&output_ctx->pb, current_output_filename, AVIO_FLAG_WRITE) < 0) {
                    fprintf(stderr, "Erreur: Impossible d'ouvrir '%s'\n", current_output_filename);
                    av_packet_unref(&pkt);
                    break;
                }
            }
            // Réinitialise le timer du segment
            segment_start_time = packet_time;
        }
        // Mémorise le temps du dernier paquet vidéo
        if (pkt.stream_index == out_video_index) {
            prev_packet_time = packet_time;
        }

        // Recalcule les timestamps
        AVStream *in_stream = input_ctx->streams[original_stream_index];
        AVStream *out_stream = output_ctx->streams[pkt.stream_index];
        // Recalcule les timestamps pour la nouvelle base temporelle
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
                                    AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
                                    AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        // Position dans fichier non pertinente après remux
        pkt.pos = -1;
        // Écrit le paquet dans le fichier segment (avec entrelacement audio/vidéo)
        if (av_interleaved_write_frame(output_ctx, &pkt) < 0) {
            fprintf(stderr, "Attention: Impossible d'écrire le paquet\n");
        }
        // Libère la mémoire du paquet
        av_packet_unref(&pkt);
    }

    // Finalisation du dernier segment
    if (num_segments < MAX_SEGMENTS) {
        // Écrit le trailer MPEG-TS
        av_write_trailer(output_ctx);
        // Ferme le dernier fichier
        avio_closep(&output_ctx->pb);
        // Enregistre le dernier segment si valide
        if (num_segments > 0 || !wait_first_keyframe) {
            // Calcule la durée du dernier segment
            actual_segment_durations[num_segments] = (unsigned int)rint(packet_time - segment_start_time);
            // Garantit durée minimale de 1 seconde
            if (actual_segment_durations[num_segments] == 0) {
                actual_segment_durations[num_segments] = 1;
            }
            // Mise à jour incrémentale du max
            if (actual_segment_durations[num_segments] > max_duration)
                max_duration = actual_segment_durations[num_segments];
            num_segments++;
            // Écrit le fichier M3U8 final avec flag END
            write_index_file(output_index_file, tmp_output_index_file,
                           num_segments, actual_segment_durations, list_offset,
                           base_file_name, base_file_extension, max_duration, 1);
        }
    }
    // IMPORTANT : Libère ressources allouées
    avformat_free_context(output_ctx);
    avformat_close_input(&input_ctx);

    printf("Segmentation terminée: %u segments créés\n", num_segments);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 7) {
       return 1;
    }

    const char *input_file = argv[1];
    const char *output_dir = argv[2];
    const char *index_file = argv[3];
    const char *base_name = argv[4];
    const char *extension = argv[5];
    int segment_duration = atoi(argv[6]);
    int max_segments = (argc > 7) ? atoi(argv[7]) : 0;

    if (segment_duration <= 0) {
        fprintf(stderr, "Erreur: La durée du segment doit être positive\n");
        return 1;
    }
    // Crée le répertoire de sortie s'il n'existe pas
    struct stat st = {0};
    if (stat(output_dir, &st) == -1) {
        if (mkdir(output_dir, 0755) != 0) {
            fprintf(stderr, "Erreur: Impossible de créer '%s': %s\n", output_dir, strerror(errno));
            return 1;
        }
    }

    printf("=== Segmentation vidéo ===\n");
    printf("Entrée: %s\n", input_file);
    printf("Sortie: %s/%s-*%s\n", output_dir, base_name, extension);
    printf("Index: %s\n", index_file);
    printf("Durée: %ds | Max segments: %d\n\n", segment_duration, max_segments);

    int result = segment_video(input_file, output_dir, index_file, base_name,
                               extension, segment_duration, max_segments);

    printf("\n%s\n", result == 0 ? "Succès" : "Échec");
    return result;
}