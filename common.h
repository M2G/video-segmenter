#ifndef COMMON_H
#define COMMON_H

// ---------------------------------------------------------------------------
// Constantes globales
// ---------------------------------------------------------------------------
#define MAX_FILENAME_LENGTH 512
#define MAX_SEGMENTS        4096

// ---------------------------------------------------------------------------
// Codes de retour
// ---------------------------------------------------------------------------
typedef enum {
    SEG_OK  =  0,   // OK
    SEG_ERR = -1    // FAIL
} SegResult;

// ---------------------------------------------------------------------------
// Macro CHECK - gestion d'erreur unifiée
//
// Usage : CHECK(condition_echec, "message d'erreur");
//
// NOTE : Fail :
//   - Affiche le message sur stderr
//   - Positionne ret = SEG_ERR
//   - Saute au label "cleanup" via goto
//
// Prérequis : la fonction appelante doit déclarer "SegResult ret"
//             et posséder un label "cleanup:" en fin de corps.
// ---------------------------------------------------------------------------
#define CHECK(cond, msg, ...)                                     \
    do {                                                          \
        if (cond) {                                               \
            fprintf(stderr, "Erreur: " msg "\n", ##__VA_ARGS__); \
            ret = SEG_ERR;                                        \
            goto cleanup;                                         \
        }                                                         \
    } while (0)

#endif // COMMON_H
