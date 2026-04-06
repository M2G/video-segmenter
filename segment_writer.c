#include <stdio.h>

#include "libavformat/avformat.h"
#include "segment_writer.h"

void build_segment_path(
    char *out, size_t size,
    const char *dir, const char *name,
    unsigned int index, const char *ext
) {
    snprintf(out, size, "%s/%s-%u%s", dir, name, index, ext);
}

SegResult open_next_segment(
    AVFormatContext *output_ctx,
    char *filename_buf, size_t buf_size,
    const char *dir, const char *name,
    unsigned int index, const char *ext
) {
    build_segment_path(filename_buf, buf_size, dir, name, index, ext);

    if (avio_open(&output_ctx->pb, filename_buf, AVIO_FLAG_WRITE) < 0) {
        fprintf(stderr, "Erreur: Impossible d'ouvrir '%s'\n", filename_buf);
        return SEG_ERR;
    }

    printf("Segment: '%s'\n", filename_buf);
    return SEG_OK;
}
