#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "common.h"
#include "video_segmenter.h"

int main(int argc, char **argv) {
printf("=== HLS Video Segmenter ===\n");
    if (argc < 7) {
        fprintf(stderr,
            "Usage: %s <input> <output_dir> <index.m3u8> <base_name> <.ext> "
            "<segment_duration> [max_segments]\n", argv[0]);
        return SEG_ERR;
    }

    const char *input_file       = argv[1];
    const char *output_dir       = argv[2];
    const char *index_file       = argv[3];
    const char *base_name        = argv[4];
    const char *extension        = argv[5];
    int         segment_duration = atoi(argv[6]);
    int         max_segments     = argc > 7 ? atoi(argv[7]) : 0;

    if (segment_duration <= 0) {
        fprintf(stderr, "Erreur: La durée du segment doit être positive\n");
        return SEG_ERR;
    }

    // Crée le répertoire de sortie s'il n'existe pas
    struct stat st = {0};
    if (stat(output_dir, &st) == -1) {
        if (mkdir(output_dir, 0755) != 0) {
            fprintf(stderr, "Erreur: Impossible de créer '%s': %s\n",
                    output_dir, strerror(errno));
            return SEG_ERR;
        }
    }

    printf("=== Segmentation vidéo ===\n");
    printf("Entrée : %s\n",               input_file);
    printf("Sortie : %s/%s-*%s\n",        output_dir, base_name, extension);
    printf("Index  : %s\n",               index_file);
    printf("Durée  : %ds | Max : %d\n\n", segment_duration, max_segments);

    SegResult result = segment_video(input_file, output_dir, index_file,
                                     base_name, extension,
                                     segment_duration, max_segments);

    printf("\n%s\n", result == SEG_OK ? "Succès" : "Échec");
    return result;
}
