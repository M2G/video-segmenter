// test_video_segmenter.c
//
// Tests unitaires pour video_segmenter.c
//
// Compilation :
//   gcc -Wall -Wextra -O0 -g test_video_segmenter.c -o test_video_segmenter \
//       $(pkg-config --cflags --libs libavformat libavcodec libavutil) -lm
//
// Exécution :
//   ./test_video_segmenter

#include <stdio.h>
#include <assert.h>

#define main video_segmenter_main
#include "video_segmenter.c"
#undef main

// ---------------------------------------------------------------------------
// Compteurs globaux de test
// ---------------------------------------------------------------------------
static int tests_run    = 0;
static int tests_passed = 0;

// ---------------------------------------------------------------------------
// Utilitaires de test
// ---------------------------------------------------------------------------

// Lit le contenu complet d'un fichier dans un buffer alloué (à libérer par
// l'appelant). Retourne NULL si le fichier n'existe pas ou est illisible.
static char *read_file_content(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size < 0) { fclose(fp); return NULL; }

    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(fp); return NULL; }

    size_t read_bytes = fread(buf, 1, (size_t)size, fp);
    buf[read_bytes] = '\0';
    fclose(fp);
    return buf;
}

// Vérif. qu'un fichier existe sur disque.
static int file_exists(const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp) { fclose(fp); return 1; }
    return 0;
}

// Macro d'assertion de test : affiche PASS/FAIL et incrémente les compteurs.
// Ne stoppe pas l'exécution des autres tests en cas d'échec (contrairement
// à assert() standard), pour voir tous les résultats en une passe.
#define TEST_ASSERT(cond, msg)                                    \
    do {                                                          \
        tests_run++;                                              \
        if (cond) {                                               \
            tests_passed++;                                       \
            printf("  [PASS] %s\n", msg);                         \
        } else {                                                  \
            printf("  [FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        }                                                          \
    } while (0)

// ---------------------------------------------------------------------------
// Test 1 - Cas normal : plusieurs segments, islast=0
// ---------------------------------------------------------------------------

static void test_write_index_basic(void) {
    printf("test_write_index_basic\n");

    const char *index_path = "/tmp/test_index.m3u8";
    const char *tmp_path   = "/tmp/test_index.m3u8.tmp";
    unsigned int durations[3] = {10, 9, 11};

    SegResult ret = write_index_file(
        index_path, tmp_path,
        3, durations,
        1,                  // offset
        "segment", ".ts",
        11,                 // max_duration
        0                   // islast
    );

    TEST_ASSERT(ret == SEG_OK, "write_index_file retourne SEG_OK");
    TEST_ASSERT(file_exists(index_path), "le fichier index.m3u8 existe apres l'appel");
    TEST_ASSERT(!file_exists(tmp_path), "le fichier temporaire a ete supprime (rename)");

    char *content = read_file_content(index_path);
    TEST_ASSERT(content != NULL, "le contenu du fichier est lisible");

    if (content) {
        TEST_ASSERT(strstr(content, "#EXTM3U") != NULL, "contient #EXTM3U");
        TEST_ASSERT(strstr(content, "#EXT-X-VERSION:3") != NULL, "contient #EXT-X-VERSION:3");
        TEST_ASSERT(strstr(content, "#EXT-X-MEDIA-SEQUENCE:1") != NULL, "contient le bon offset");
        TEST_ASSERT(strstr(content, "#EXT-X-TARGETDURATION:11") != NULL, "contient la bonne duree max");
        TEST_ASSERT(strstr(content, "#EXTINF:10,\nsegment-1.ts") != NULL, "contient le segment 1");
        TEST_ASSERT(strstr(content, "#EXTINF:9,\nsegment-2.ts") != NULL, "contient le segment 2");
        TEST_ASSERT(strstr(content, "#EXTINF:11,\nsegment-3.ts") != NULL, "contient le segment 3");
        free(content);
    }

    unlink(index_path);
}

// ---------------------------------------------------------------------------
// Test 2 - islast=1 doit ecrire EXT-X-ENDLIST
// ---------------------------------------------------------------------------

static void test_write_index_islast_true(void) {
    printf("test_write_index_islast_true\n");

    const char *index_path = "/tmp/test_index_last.m3u8";
    const char *tmp_path   = "/tmp/test_index_last.m3u8.tmp";
    unsigned int durations[1] = {5};

    SegResult ret = write_index_file(
        index_path, tmp_path,
        1, durations,
        1, "segment", ".ts",
        5,
        1   // islast = 1
    );

    TEST_ASSERT(ret == SEG_OK, "write_index_file retourne SEG_OK");

    char *content = read_file_content(index_path);
    TEST_ASSERT(content != NULL, "le contenu du fichier est lisible");
    if (content) {
        TEST_ASSERT(strstr(content, "#EXT-X-ENDLIST") != NULL,
                    "contient #EXT-X-ENDLIST quand islast=1");
        free(content);
    }

    unlink(index_path);
}

// ---------------------------------------------------------------------------
// Test 3 - islast=0 ne doit PAS ecrire EXT-X-ENDLIST
// ---------------------------------------------------------------------------

static void test_write_index_islast_false(void) {
    printf("test_write_index_islast_false\n");

    const char *index_path = "/tmp/test_index_notlast.m3u8";
    const char *tmp_path   = "/tmp/test_index_notlast.m3u8.tmp";
    unsigned int durations[1] = {5};

    SegResult ret = write_index_file(
        index_path, tmp_path,
        1, durations,
        1, "segment", ".ts",
        5,
        0   // islast = 0
    );

    TEST_ASSERT(ret == SEG_OK, "write_index_file retourne SEG_OK");

    char *content = read_file_content(index_path);
    TEST_ASSERT(content != NULL, "le contenu du fichier est lisible");
    if (content) {
        TEST_ASSERT(strstr(content, "#EXT-X-ENDLIST") == NULL,
                    "ne contient PAS #EXT-X-ENDLIST quand islast=0");
        free(content);
    }

    unlink(index_path);
}

// ---------------------------------------------------------------------------
// Test 4 - num_segments=0 : ne doit rien ecrire, retourne SEG_OK
// ---------------------------------------------------------------------------

static void test_write_index_empty(void) {
    printf("test_write_index_empty\n");

    const char *index_path = "/tmp/test_index_empty.m3u8";
    const char *tmp_path   = "/tmp/test_index_empty.m3u8.tmp";
    unsigned int durations[1] = {0};  // tableau non utilisé si num_segments=0

    // S'assure qu'aucun fichier residuel d'un test precedent ne fausse le resultat
    unlink(index_path);
    unlink(tmp_path);

    SegResult ret = write_index_file(
        index_path, tmp_path,
        0,  // num_segments = 0
        durations,
        1, "segment", ".ts",
        0, 0
    );

    TEST_ASSERT(ret == SEG_OK, "retourne SEG_OK meme avec num_segments=0");
    TEST_ASSERT(!file_exists(index_path), "n'ecrit aucun fichier si num_segments=0");
}

// ---------------------------------------------------------------------------
// Test 5 - Le rename est bien atomique : tmp disparait, index apparait
// ---------------------------------------------------------------------------

static void test_write_index_rename_cleanup(void) {
    printf("test_write_index_rename_cleanup\n");

    const char *index_path = "/tmp/test_index_rename.m3u8";
    const char *tmp_path   = "/tmp/test_index_rename.m3u8.tmp";
    unsigned int durations[1] = {7};

    unlink(index_path);
    unlink(tmp_path);

    write_index_file(index_path, tmp_path, 1, durations, 1, "segment", ".ts", 7, 0);

    TEST_ASSERT(file_exists(index_path), "index.m3u8 existe apres rename");
    TEST_ASSERT(!file_exists(tmp_path), "le .tmp n'existe plus apres rename");

    unlink(index_path);
}

int main(void) {
    printf("=== Tests unitaires : write_index_file ===\n\n");

    test_write_index_basic();
    test_write_index_islast_true();
    test_write_index_islast_false();
    test_write_index_empty();
    test_write_index_rename_cleanup();

    printf("\n=== Resultat : %d/%d tests reussis ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}