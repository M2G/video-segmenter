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

static AVStream *add_out_stream(AVFormatContext *out_ctx, AVStream *in_stream) {
     AVStream *out_stream = avformat_new_stream(out_ctx, NULL);

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
        return SEG_ERR;
    }

    return SEG_OK;
}