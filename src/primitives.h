#ifndef PT_PRIMITIVES_H
#define PT_PRIMITIVES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * NATIVE ARCHITECTURE PRIMITIVES & CONVERSIONS (2-Word Aligned)
 * ============================================================================ */

typedef intptr_t word_t;

#define PT_WORD_SIZE_BYTES       (sizeof(word_t)) // 8 bytes on 64-bit macOS/Linux
#define PT_WORD_MASK             (PT_WORD_SIZE_BYTES - 1)

// Round up requested bytes to ensure the user payload is a multiple of 2 words (16 bytes)
#define PT_BYTES_TO_WORDS(bytes) ((((bytes) + 15) & ~15) / PT_WORD_SIZE_BYTES)
#define PT_WORDS_TO_BYTES(words) ((words) * PT_WORD_SIZE_BYTES)

#define PT_SUPER_PAGE_BYTES      (4ULL * 1024 * 1024 * 1024)
#define PT_SUPER_PAGE_WORDS      (PT_SUPER_PAGE_BYTES / PT_WORD_SIZE_BYTES)

#define PT_HUGE_THRESHOLD_WORDS  (PT_SUPER_PAGE_WORDS - 4)

/* ============================================================================
 * TRAILING-EDGE STRUCTURAL OVERLAYS
 * ============================================================================ */

typedef struct pt_remnant {
    word_t hdr[1];
    word_t ftr[1];
} pt_remnant_t;

typedef struct pt_link {
    struct pt_link* prev;
    struct pt_link* next;
    word_t ftr[1]; // Maps to the physical footer at (hdr_ptr + size - 1)
} pt_link_t;

typedef struct pt_list {
    pt_link_t sentinel;
} pt_list_t;

typedef struct pt_redblack {
    struct pt_redblack* left;
    struct pt_redblack* right;
    struct pt_redblack* parent;
    word_t color;
    word_t left_max;
    word_t right_max;
    word_t ftr[1]; // Maps to the physical footer at (hdr_ptr + size - 1)
} pt_redblack_t;

#endif // PT_PRIMITIVES_H
