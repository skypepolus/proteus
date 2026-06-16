/* wasm32-st/arena.h */
#ifndef PT_ARENA_H
#define PT_ARENA_H

#include "primitives.h"
#include "arena-st.h"

// Symbol provided automatically by the LLVM/Clang WASM linker
extern unsigned char __heap_base;
extern void* __heap_end;

// Forward declare our custom WASM memory grower
int wasm_brk(void* addr);

static inline pt_arena_t* pt_arena_get_local(void) 
{
    if (__builtin_expect(g_pt.num_cores == 0, 0)) {
        enum { alignment = sizeof(word_t) * 2, mask = alignment - 1 };
        word_t* addr;
        
        pt_arena_t* arena = g_pt.arenas;

        // WASM Hardware invariant
        arena->page_size = WASM_PAGE_SIZE;
        arena->page_mask = arena->page_size - 1;

        arena->segregate[0].head.next = &arena->segregate[0].tail;
        arena->segregate[0].tail.prev = &arena->segregate[0].head;
        arena->segregate[1].head.next = &arena->segregate[1].tail;
        arena->segregate[1].tail.prev = &arena->segregate[1].head;

        // Start the heap exactly where the binary's data section ends
		__heap_end = (void*)(__builtin_wasm_memory_size(0) << 16);
        addr = (word_t*)(((uintptr_t)&__heap_base + alignment + mask) & ~(uintptr_t)mask); 
        
        if(wasm_brk(addr + 2)) __builtin_trap();
        
        g_pt.superpage->hdr = --addr;
        g_pt.superpage->hdr[0] = 0;
        g_pt.superpage->ftr = --addr;
        g_pt.superpage->ftr[0] = 0;
        g_pt.superpage->arena_ptr = g_pt.arenas;
        g_pt.num_cores = 1;
    }
    return g_pt.arenas;
}

#endif // PT_ARENA_H
