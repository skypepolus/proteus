/* wasm32-st/platform.c */
#include "platform.h"
#include "primitives.h"
#include "arena.h"
#include "index.h"

int errno;

// Track the top of the WASM linear memory
void* wasm_heap_ptr = 0;

// Replaces the POSIX brk() system call
int wasm_brk(void* addr) {
    uintptr_t current_pages = __builtin_wasm_memory_size(0);
    uintptr_t target_pages = ((uintptr_t)addr + (WASM_PAGE_SIZE - 1)) / WASM_PAGE_SIZE;
    
    if (target_pages > current_pages) {
        uintptr_t delta = target_pages - current_pages;
        if (__builtin_wasm_memory_grow(0, delta) == (size_t)-1) {
            return -1; // Out of memory (Browser limit reached)
        }
    }
    wasm_heap_ptr = addr;
    return 0;
}

// Minimal bare-metal string utilities for core.c
void* memset(void* dest, int val, size_t len) {
    unsigned char* ptr = dest;
    while (len-- > 0) *ptr++ = val;
    return dest;
}

void* memcpy(void* dest, const void* src, size_t len) {
    char* d = dest;
    const char* s = src;
    while (len--) *d++ = *s++;
    return dest;
}

// Exactly the same logic as posix-st, just invoking wasm_brk instead of brk
pt_redblack_t* pt_core_allocate_superpage_fallback(pt_arena_t* arena, word_t size_words) 
{
    size_t increment = size_words * sizeof(word_t);
    pt_superpage_t* superpage = g_pt.superpage;
    word_t* addr;
    word_t tail_words;
    word_t* tail_hdr;
    pt_redblack_t* node;

    if(0 < superpage->hdr[-1]) {
        tail_words = superpage->hdr[-1];
        tail_hdr = superpage->hdr - tail_words;
        addr = (word_t*)(((uintptr_t)(tail_hdr + 1) + increment + PT_INDEX_WATERMARK_MASK) & ~(uintptr_t)PT_INDEX_WATERMARK_MASK);
        
        if(wasm_brk(addr)) return NULL; // WASM memory expansion failed
        
        superpage->hdr = --addr;
        word_t delta = superpage->hdr - tail_hdr - tail_words;
        switch((unsigned)tail_words >> 1) {
        default:
        case 4: // Tree
            node = pt_idx_hdr_to_tree(tail_hdr, tail_words);
            node = pt_idx_tree_migrate_rightward(arena, node, tail_hdr, tail_words + delta);
            node->hdr[0] = delta;
            break;
        case 3: // List
        case 2: // List
            pt_idx_list_unlink(tail_hdr, tail_words);
        case 1: // Remnant
            pt_idx_tree_insert(arena, arena->root, tail_hdr, tail_words + delta);
            tail_hdr[0] = tail_words + delta;
            node = pt_idx_hdr_to_tree(tail_hdr, tail_words + delta);
            node->hdr[0] = delta;
            break;
        case 0:
            __builtin_trap();
        }
    } else {
        tail_hdr = superpage->hdr;
        addr = (word_t*)(((uintptr_t)(tail_hdr + 1) + increment + PT_INDEX_WATERMARK_MASK) & ~(uintptr_t)PT_INDEX_WATERMARK_MASK);
        
        if(wasm_brk(addr)) return NULL;
        
        superpage->hdr = --addr;
        tail_words = superpage->hdr - tail_hdr;
        tail_hdr[0] = tail_words;
        pt_idx_tree_insert(arena, arena->root, tail_hdr, tail_words);
        node = pt_idx_hdr_to_tree(tail_hdr, tail_words);
        node->hdr[0] = tail_words; 
    }
    superpage->hdr[0] = 0;
    return node; 
}
