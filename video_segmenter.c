#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/mathematics.h"

#include "common.h"
#include "video_segmenter.h"
#include "stream_utils.h"
#include "segment_writer.h"
#include "index_writer.h"

// ---------------------------------------------------------------------------
// init_output_context  (privé à ce fichier)
//
// Crée le contexte de sortie MPEG-TS, y ajoute les flux vidéo et audio,
// ouvre le premier fichier segment et écrit l'en-tête.
// En cas d'erreur, output_ctx est libéré et mis à NULL.
// ---------------------------------------------------------------------------
static SegResult init_output_context(
    AVFormatContext  *input_ctx,
    AVFormatContext **output_ctx,
    int               in_video_index,
    int               in_audio_index,
    const char       *first_segment,
    AVStream        **out_video_st,
    AVStream        **out_audio_st
) {
    avformat_alloc_output_context2(output_ctx, NULL, "mpegts", NULL);
    if (!*output_ctx) {
        fprintf(stderr, "Erreur: Impossible d'allouer le contexte de sortie\n");
        return SEG_ERR;
    }

    *out_video_st = add_output_stream(*output_ctx, input_ctx->streams[in_video_index]);
    if (!*out_video_st) goto fail;

    if (in_audio_index >= 0) {
        *out_audio_st = add_output_stream(*output_ctx, input_ctx->streams[in_audio_index]);
        if (!*out_audio_st) goto fail;
    }

    if (avio_open(&(*output_ctx)->pb, first_segment, AVIO_FLAG_WRITE) < 0) {
        fprintf(stderr, "Erreur: Impossible d'ouvrir '%s'\n", first_segment);
        goto fail;
    }

    printf("Démarrage du segment: '%s'\n", first_segment);

    if (avformat_write_header(*output_ctx, NULL) < 0) {
        fprintf(stderr, "Erreur: Impossible d'écrire l'en-tête\n");
        avio_closep(&(*output_ctx)->pb);
        goto fail;
    }

    return SEG_OK;

fail:
    avformat_free_context(*output_ctx);
    *output_ctx = NULL;
    return SEG_ERR;
}

// segment_video
SegResult segment_video(
    const char *input_file,
    const char *base_dirpath,
    const char *output_index_file,
    const char *base_file_name,
    const char *base_file_extension,
    int         segment_length,
    int         max_list_length
) {
    AVFormatContext *input_ctx  = NULL;
    AVFormatContext *output_ctx = NULL;
    SegResult        ret        = SEG_OK;

    char         current_filename[MAX_FILENAME_LENGTH];
    char         tmp_index_file[MAX_FILENAME_LENGTH];
    unsigned int durations[MAX_SEGMENTS + 1];
    unsigned int max_duration   = 0;
    unsigned int num_segments   = 0;
    unsigned int output_index   = 1;
    unsigned int list_offset    = 1;

    double segment_start = 0.0;
    double pkt_time      = 0.0;
    double prev_pkt_time = 0.0;

    int in_video_index      = -1;
    int in_audio_index      = -1;
    int wait_first_keyframe = 1;

    snprintf(tmp_index_file, MAX_FILENAME_LENGTH, "%s.tmp", output_index_file);

    // open et analyse du fichier source
    int ff_ret = avformat_open_input(&input_ctx, input_file, NULL, NULL);
    if (ff_ret < 0) {
        char errbuf[128];
        av_strerror(ff_ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "Erreur: Impossible d'ouvrir '%s': %s\n", input_file, errbuf);
        return SEG_ERR;
    }

    CHECK(avformat_find_stream_info(input_ctx, NULL) < 0,
          "Impossible de lire les informations des flux");

    for (unsigned int i = 0; i < input_ctx->nb_streams; i++) {
        enum AVMediaType type = input_ctx->streams[i]->codecpar->codec_type;
        if      (type == AVMEDIA_TYPE_VIDEO && in_video_index < 0) in_video_index = i;
        else if (type == AVMEDIA_TYPE_AUDIO && in_audio_index < 0) in_audio_index = i;
    }

    CHECK(in_video_index < 0, "Aucun flux vidéo trouvé dans le fichier source");

    printf("Flux vidéo: index %d\n", in_video_index);
    if (in_audio_index >= 0) printf("Flux audio: index %d\n", in_audio_index);

    // init ctx sortie
    AVStream *out_video_st = NULL;
    AVStream *out_audio_st = NULL;
    build_segment_path(current_filename, MAX_FILENAME_LENGTH,
                       base_dirpath, base_file_name, output_index, base_file_extension);

    CHECK(init_output_context(input_ctx, &output_ctx,
                              in_video_index, in_audio_index,
                              current_filename,
                              &out_video_st, &out_audio_st) != SEG_OK,
          "Échec d'initialisation du contexte de sortie");

    // Boucle principale de lecture et segmentation
    const double vid_pts2time  = av_q2d(input_ctx->streams[in_video_index]->time_base);
    const int    out_video_idx = out_video_st->index;
    const int    out_audio_idx = out_audio_st ? out_audio_st->index : -1;

    // av_packet_alloc initialise correctement tous les champs internes -
    // évite le comportement indéfini d'une AVPacket non initialisée sur la stack (FFmpeg 5+)
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "Erreur: Impossible d'allouer AVPacket\n");
        ret = SEG_ERR;
        goto cleanup;
    }

    while (av_read_frame(input_ctx, pkt) >= 0) {
        int is_keyframe           = 0;
        int original_stream_index = pkt->stream_index;

        if (pkt->stream_index == in_video_index) {
            pkt_time    = pkt->pts * vid_pts2time;
            is_keyframe = pkt->flags & AV_PKT_FLAG_KEY;
            if (is_keyframe && wait_first_keyframe) {
                wait_first_keyframe = 0;
                prev_pkt_time       = pkt_time;
                segment_start       = pkt_time;
            }
            pkt->stream_index = out_video_idx;
        } else if (pkt->stream_index == in_audio_index && out_audio_st) {
            pkt->stream_index = out_audio_idx;
        } else {
            av_packet_unref(pkt);
            continue;
        }

        if (wait_first_keyframe) { av_packet_unref(pkt); continue; }

        // Découpe de segment
        if (is_keyframe && (pkt_time - segment_start) >= (segment_length - 0.25)) {
            avio_flush(output_ctx->pb);
            avio_closep(&output_ctx->pb);

            unsigned int seg_dur        = (unsigned int)rint(prev_pkt_time - segment_start);
            durations[num_segments]     = seg_dur;
            if (seg_dur > max_duration) max_duration = seg_dur;
            num_segments++;

            // Fenêtre glissante : prépare le nom de l'ancien segment avant avio_open
            char old_filename[MAX_FILENAME_LENGTH];
            old_filename[0] = '\0';
            if (max_list_length > 0 && num_segments > (unsigned int)max_list_length) {
                build_segment_path(old_filename, MAX_FILENAME_LENGTH,
                                   base_dirpath, base_file_name,
                                   list_offset, base_file_extension);
                list_offset++;
                num_segments--;
                memmove(durations, durations + 1, num_segments * sizeof(durations[0]));
                // Recalcule max_duration seulement si le segment supprimé était le max
                if (durations[0] >= max_duration) {
                    max_duration = 0;
                    for (unsigned int i = 0; i < num_segments; i++)
                        if (durations[i] > max_duration) max_duration = durations[i];
                }
            }

            write_index_file(output_index_file, tmp_index_file,
                             num_segments, durations, list_offset,
                             base_file_name, base_file_extension, max_duration, 0);

            if (num_segments >= MAX_SEGMENTS) {
                fprintf(stderr, "Limite de segments atteinte (%u)\n", MAX_SEGMENTS);
                av_packet_unref(pkt);
                break;
            }

            // Ouvre le segment suivant, puis supprime l'ancien (unlink différé)
            if (open_next_segment(output_ctx, current_filename, MAX_FILENAME_LENGTH,
                                  base_dirpath, base_file_name,
                                  ++output_index, base_file_extension) != SEG_OK) {
                av_packet_unref(pkt);
                break;
            }
            if (old_filename[0]) unlink(old_filename);

            segment_start = pkt_time;
        }

        if (pkt->stream_index == out_video_idx) prev_pkt_time = pkt_time;

        // Rescale timestamps vers la base temporelle de sortie
        AVStream *in_st  = input_ctx->streams[original_stream_index];
        AVStream *out_st = output_ctx->streams[pkt->stream_index];
        pkt->pts      = av_rescale_q_rnd(pkt->pts,      in_st->time_base, out_st->time_base,
                                         AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt->dts      = av_rescale_q_rnd(pkt->dts,      in_st->time_base, out_st->time_base,
                                         AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt->duration = av_rescale_q(pkt->duration, in_st->time_base, out_st->time_base);
        pkt->pos      = -1;

        if (av_interleaved_write_frame(output_ctx, pkt) < 0)
            fprintf(stderr, "Attention: Impossible d'écrire le paquet\n");

        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);

    // Finalisation du dernier segment
    if (num_segments < MAX_SEGMENTS) {
        av_write_trailer(output_ctx);
        avio_closep(&output_ctx->pb);
        if (num_segments > 0 || !wait_first_keyframe) {
            unsigned int last_dur = (unsigned int)rint(pkt_time - segment_start);
            if (last_dur == 0) last_dur = 1;
            durations[num_segments] = last_dur;
            if (last_dur > max_duration) max_duration = last_dur;
            num_segments++;
            write_index_file(output_index_file, tmp_index_file,
                             num_segments, durations, list_offset,
                             base_file_name, base_file_extension, max_duration, 1);
        }
    }

cleanup:
    if (output_ctx) {
        // Ferme explicitement pb si encore ouvert (ex: break en cours de boucle)
        // évite une fuite de descripteur fichier avant avformat_free_context
        if (output_ctx->pb) avio_closep(&output_ctx->pb);
        avformat_free_context(output_ctx);
    }
    if (input_ctx)  avformat_close_input(&input_ctx);
    if (ret == SEG_OK)
        printf("Segmentation terminée: %u segments créés\n", num_segments);
    return ret;
}
