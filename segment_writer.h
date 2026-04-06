#ifndef SEGMENT_WRITER_H
#define SEGMENT_WRITER_H

#include <stddef.h>
#include "libavformat/avformat.h"
#include "common.h"

// ---------------------------------------------------------------------------
// build_segment_path
//
// Construit le chemin complet d'un fichier segment dans "out".
// Ex : build_segment_path(buf, size, "/out", "stream", 3, ".ts")
//           -> "/out/stream-3.ts"
// ---------------------------------------------------------------------------
void build_segment_path(
    char *out, size_t size,
    const char *dir, const char *name,
    unsigned int index, const char *ext
);

// ---------------------------------------------------------------------------
// open_next_segment
//
// Construit le nom du segment numéro "index", ouvre le fichier en écriture
// via avio_open et met à jour "filename_buf" avec le chemin construit.
//
// Remplace le doublon snprintf + avio_open qui apparaissait 2 fois
// dans la logique de fenêtre glissante.
//
// Retourne SEG_OK ou SEG_ERR.
// ---------------------------------------------------------------------------
SegResult open_next_segment(
    AVFormatContext *output_ctx,
    char *filename_buf, size_t buf_size,
    const char *dir, const char *name,
    unsigned int index, const char *ext
);

#endif // SEGMENT_WRITER_H
