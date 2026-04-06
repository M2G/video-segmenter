#ifndef STREAM_UTILS_H
#define STREAM_UTILS_H

#include "libavformat/avformat.h"

// ---------------------------------------------------------------------------
// add_output_stream
//
// Duplique un flux d'entrée (vidéo ou audio) vers le conteneur de sortie.
// Copie les paramètres codec, réinitialise le tag et conserve la time_base.
//
// Retourne le nouveau AVStream, ou NULL en cas d'erreur.
// ---------------------------------------------------------------------------
AVStream *add_output_stream(AVFormatContext *output_ctx, AVStream *input_stream);

#endif // STREAM_UTILS_H
