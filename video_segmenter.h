#ifndef SEGMENTER_H
#define SEGMENTER_H

#include "common.h"

// ---------------------------------------------------------------------------
// segment_video
//
// Fonction principale : lit `input_file`, découpe la vidéo en segments
// MPEG-TS de `segment_length` secondes et génère la playlist HLS.
//
// Paramètres :
//   input_file        - fichier vidéo source
//   base_dirpath      - répertoire de sortie des segments
//   output_index_file - chemin du fichier .m3u8 à générer
//   base_file_name    - préfixe des segments (ex: "stream")
//   base_file_extension - extension des segments (ex: ".ts")
//   segment_length    - durée cible de chaque segment en secondes
//   max_list_length   - taille de la fenêtre glissante (0 = illimitée)
//
// Retourne SEG_OK ou SEG_ERR.
// ---------------------------------------------------------------------------

SegResult segment_video(
    const char *input_file,
    const char *base_dirpath,
    const char *output_index_file,
    const char *base_file_name,
    const char *base_file_extension,
    int         segment_length,
    int         max_list_length
);

#endif // SEGMENTER_H
