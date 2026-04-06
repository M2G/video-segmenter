#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "index_writer.h"

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
) {
    if (numsegments < 1) return SEG_OK;

    FILE *fp = fopen(tmp_index, "w");
    if (!fp) {
        fprintf(stderr, "Erreur: Impossible d'ouvrir '%s': %s\n",
                tmp_index, strerror(errno));
        return SEG_ERR;
    }

    // En-tête M3U8 - max_duration précalculé par l'appelant (O(1)/segment)
    fprintf(fp, "#EXTM3U\n#EXT-X-VERSION:3\n"
                "#EXT-X-MEDIA-SEQUENCE:%u\n#EXT-X-TARGETDURATION:%u\n",
            offset, max_duration);

    // Liste des segments actifs - O(L) inévitable (spec HLS)
    for (unsigned int i = 0; i < numsegments; i++) {
        if (fprintf(fp, "#EXTINF:%u,\n%s-%u%s\n",
                    durations[i], prefix, i + offset, ext) < 0) {
            fprintf(stderr, "Erreur: Échec d'écriture dans le fichier index\n");
            fclose(fp);
            return SEG_ERR;
        }
    }

    if (islast && fprintf(fp, "#EXT-X-ENDLIST\n") < 0) {
        fprintf(stderr, "Erreur: Échec d'écriture de EXT-X-ENDLIST\n");
        fclose(fp);
        return SEG_ERR;
    }

    fclose(fp);

    // Rename atomique : le lecteur voit toujours un fichier complet ou l'ancien
    if (rename(tmp_index, index) != 0) {
        fprintf(stderr, "Erreur: Impossible de renommer '%s' en '%s': %s\n",
                tmp_index, index, strerror(errno));
        return SEG_ERR;
    }

    return SEG_OK;
}
