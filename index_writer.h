#ifndef INDEX_WRITER_H
#define INDEX_WRITER_H

#include "common.h"

// ---------------------------------------------------------------------------
// write_index_file
//
// Génère le fichier playlist M3U8 pour le streaming HLS.
// Utilise un fichier temporaire + rename atomique pour garantir que les
// lecteurs voient toujours un fichier complet ou l'ancienne version.
//
// Paramètres :
//   index        - chemin du fichier .m3u8 final
//   tmp_index    - chemin du fichier temporaire (ex: index.m3u8.tmp)
//   numsegments  - nombre de segments actifs dans la fenêtre
//   durations    - tableau des durées (en secondes) de chaque segment actif
//   offset       - numéro du premier segment de la fenêtre (EXT-X-MEDIA-SEQUENCE)
//   prefix       - préfixe des noms de fichiers segment (ex: "stream")
//   ext          - extension des segments (ex: ".ts")
//   max_duration - durée max des segments, maintenu O(1) par l'appelant
//   islast       - 1 si c'est le dernier segment (écrit EXT-X-ENDLIST)
//
// Retourne SEG_OK ou SEG_ERR.
// ---------------------------------------------------------------------------

SegResult write_index_file(
    const char        *index,
    const char        *tmp_index,
    unsigned int       numsegments,
    const unsigned int *durations,
    unsigned int       offset,
    const char        *prefix,
    const char        *ext,
    unsigned int       max_duration,
    int                islast
);

#endif // INDEX_WRITER_H
