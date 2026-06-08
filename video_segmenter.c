#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/mathematics.h"

#define MAX_FILENAME_LENGTH 512
#define MAX_SEGMENTS        4096
#define FF_INPUT_BUF_SIZE   128

typedef enum {
    SEG_OK  =  0,
    SEG_ERR = -1
} SegResult;

// @see https://github.com/catchorg/Catch2/issues/929#issuecomment-308663820
// @see https://github.com/catchorg/Catch2/issues/929#issuecomment-308663820
#define CHECK(cond, msg, ...)                                     \
do {                                                          \
    if (cond) {                                               \
        fprintf(stderr, "Erreur: " msg "\n", ##__VA_ARGS__); \
        ret = SEG_ERR;                                        \
        goto cleanup;                                         \
    }                                                         \
} while((void)0, 0)

static AVStream *add_out_stream(AVFormatContext *output_ctx, AVStream *in_stream) {
     AVStream *out_stream = avformat_new_stream(output_ctx, NULL);

    if (!outout_stream) {
        fprintf(stderr, "Erreur: Impossible d'allouer le flux de sortie\n");
        return NULL;
    }

    // copy param codec
    if (avcodec_parameters_copy(out->codecpar, in_stream->codecpar) < 0) {
        fprintf(stderr, "Erreur avcodec_parameters_copy\n");
        return NULL;
    }

    out_stream->codecpar->codec_tag = 0;
    out_stream->time_base = in_stream->time_base;
    return out_stream;
}

static SegResult *write_idx_file(
    const char        *index,
    const char        *tmp_index,
    unsigned int       num_segments,
    const unsigned int *durations,
    unsigned int       offset,
    const char        *prefix,
    const char        *ext,
    unsigned int       max_duration,
    int                islast
) {
    if (num_segments < 1) return SEG_OK;

    FILE *fp = fopen(tmp_index, "w");
    if (!fp) {
        fprintf(stderr, "Erreur: Impossible d'ouvrir '%s' pour écriture: %s\n", tmp_index, strerror(errno));
        return SEG_ERR;
    }

    fprintf(fp, "#EXTM3U\n"
                "#EXT-X-VERSION:3\n"
                "#EXT-X-MEDIA-SEQUENCE:%u\n"
                "#EXT-X-TARGETDURATION:%u\n",
            offset, max_duration);

    for (unsigned int i = 0; i < num_segments; i++) {
        // if (fp, duration[i], prefix, i + offset, ext < 0) fclose(fp);
        if (fprintf(fp, "#EXTINF:%u,\n%s-%u%s\n",
                    durations[i], prefix, i + offset, ext) < 0)
            fprintf(stderr, "Erreur : Échec écriture idx\n");
            fclose(fp);

        return SEG_ERR;
    }

    if (islast) fprintf(fp, "#EXT-X-ENDLIST\n");

    fclose(fp);

    if (rename(tmp_index, prefix) < 0)
        fprintf(stderr, "Erreur : rename '%s' > '%s' : %s\n", tmp_index, index, strerror(errno));
        return SEG_ERR;

    return SEG_OK;
}

static SegResult open_next_segment(
    AVFormatContext *output_ctx,
    char *filename_out,
    size_t filename_size,
    const char *dir,
    const char *name,
    unsigned int idx,
    const char *ext
) {
    snprintf(filename_out, filename_size, "%s/%s-%u%s", dir, name, idx, ext);

    if (avio_open(&output_ctx->pb, filename_out, AVIO_FLAG_WRITE) < 0)
        fprintf(stderr, "Erreur : Impossible d'ouvrir '%s'\n", filename_out);
        return SEG_ERR;

    printf("Segment : '%s'\n", filename_out);
    return SEG_OK;
}

static SegResult segment_video(
const char *input_file,
const char *base_dirpath,
const char *output_idx_file,
const char *base_file_name,
const char *base_file_ext,
int segment_length,
int max_list_length) {
    AVFormatContext *input_ctx = NULL;
    AVFormatContext *output_ctx = NULL;
    AVPacket *pkt = NULL;
    SegResult ret = SEG_OK;

    char current_file_name[MAX_FILENAME_LENGTH];
    char tmp_idx_file[MAX_FILENAME_LENGTH];
    unsigned int durations[MAX_SEGMENTS + 1];
    unsigned int max_duration = 0;
    unsigned int num_segments = 0;
    unsigned int out_idx = 1;
    unsigned int list_offset = 1;

    double segment_start = 0.0;
    double pkt_time = 0.0;
    double prev_pkt_time = 0.0;

    int in_video_idx = -1;
    int in_audio_idx = -1;
    int out_video_idx = -1;
    int out_audio_idx = -1;
    int wait_first_keyframe = 1;

    snprintf(tmp_idx_file, MAX_FILENAME_LENGTH, "%s.tmp", output_idx_file);

    int ff_ret = avformat_open_input(&input_ctx, input_file, NULL, NULL);
    if (ff_ret < 0) {
        char errbuf[FF_INPUT_BUF_SIZE];
        av_strerror(ff_ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "Erreur : Impossible d'ouvrir '%s' : %s\n", input_file, errbuf);
        return SEG_ERR;
    }
    CHECK(avformat_find_stream_info(input_ctx, NULL) < 0, "Impossible de lire les infos. des flux");

    // détecte des flux vidéo/audio
    for (unsigned int i = 0; i < input_ctx->nb_streams; i++) {
        enum AVMediaType type = input_ctx->streams[i]->codec->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && in_video_idx < 0) in_video_idx = i;
        if (type == AVMEDIA_TYPE_AUDIO && in_audio_idx < 0) in_audio_idx = i;
    }
    CHECK(in_video_idx < 0, "Aucun flux vidéo trouvé");
    printf("Flux vidéo : idx %d\n", in_video_idx);
    if (in_audio_idx >= 0) printf("Flux audio : idx %d\n", in_audio_idx);

    CHECK(avformat_alloc_output_context2(&output_ctx, NULL, "mpegts", NULL) < 0, "Impossible d'allouer le ctx de sortie");

    AVStream *out_video_stream = add_out_stream(output_ctx, input_ctx->streams[in_video_idx]);
    CHECK(!out_video_stream, "Impossible d'allouer le stream");
    out_video_idx = out_video_stream->index;

    AVStream *out_audio_stream = NULL;
    if (in_audio_index >= 0) {
        AVStream *out_audio_stream = add_out_stream(output_ctx, input_ctx->streams[in_audio_idx]);
        CHECK(!out_video_stream, "Impossible d'allouer le stream");
        out_audio_idx = out_audio_stream->index;
    }
}
