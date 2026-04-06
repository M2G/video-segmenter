#include <stdio.h>

#include "stream_utils.h"

AVStream *add_output_stream(AVFormatContext *output_ctx, AVStream *input_stream) {
    AVStream *out = avformat_new_stream(output_ctx, NULL);
    if (!out) {
        fprintf(stderr, "Erreur: Impossible d'allouer le flux de sortie\n");
        return NULL;
    }
    if (avcodec_parameters_copy(out->codecpar, input_stream->codecpar) < 0) {
        fprintf(stderr, "Erreur: Impossible de copier les paramètres du codec\n");
        return NULL;
    }
    out->codecpar->codec_tag = 0;             // IMPORTANT : certains conteneurs
    out->time_base           = input_stream->time_base; // IMPORTANT : conserve la base temporelle
    return out;
}
