/*
 * Copyright 2026 Young H. Song
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "platform.h"
#include "primitives.h"
#include "arena.h"
#include "index.h"
#include "proteus.h"

// Force 64-byte alignment for cache and DMA safety on embedded chips
uint8_t ucHeap[configTOTAL_HEAP_SIZE] __attribute__((aligned(64)));

void pt_freertos_init(void) {
    pt_arena_t* arena = g_pt.arenas;

    // Use a conservative page size for embedded systems
    arena->page_size = 4096;
    arena->page_mask = arena->page_size - 1;

    arena->segregate[0].head.next = &arena->segregate[0].tail;
    arena->segregate[0].tail.prev = &arena->segregate[0].head;
    arena->segregate[1].head.next = &arena->segregate[1].tail;
    arena->segregate[1].tail.prev = &arena->segregate[1].head;

    // Anchor the Proteus superpage headers at the very start and end of the static SRAM
    word_t* heap_start = (word_t*)ucHeap;

    g_pt.superpage->ftr = heap_start;
    g_pt.superpage->ftr[0] = 0;
    
    g_pt.superpage->hdr = heap_start + 1;
    g_pt.superpage->hdr[0] = 0;

    g_pt.superpage->arena_ptr = g_pt.arenas;
    g_pt.num_cores = 1;
}

// In FreeRTOS, we cannot request more memory from an OS. 
// We just yield the static array.
pt_redblack_t* pt_core_allocate_superpage_fallback(pt_arena_t* arena, word_t size_words) 
{
    word_t* heap_end   = (word_t*)(ucHeap + configTOTAL_HEAP_SIZE);
    pt_superpage_t* superpage = g_pt.superpage;

	size_words = size_words < 8 ? 8 : size_words;

	if(0 < superpage->hdr[-1]) {
		word_t tail_words = superpage->hdr[-1];
		word_t* tail_hdr = superpage->hdr - tail_words;
		word_t delta = size_words - tail_words;
		if(superpage->hdr + delta < heap_end) {
			pt_redblack_t* node;
			superpage->hdr += delta;
			switch((unsigned)tail_words >> 1) {
			default:
			case 4: // Tree
				node = pt_idx_hdr_to_tree(tail_hdr, tail_words);
				node = pt_idx_tree_migrate_rightward(arena, node, tail_hdr, tail_words + delta);
				node->hdr[0] = delta; // watermark update
				break;
			case 3: // List
			case 2: // List
				pt_idx_list_unlink(tail_hdr, tail_words);
			case 1: // Remnant
				pt_idx_tree_insert(arena, arena->root, tail_hdr, tail_words + delta);
				tail_hdr[0] = tail_words + delta;
				node = pt_idx_hdr_to_tree(tail_hdr, tail_words + delta);
				node->hdr[0] = delta; // watermark update
				break;
			case 0:
				__builtin_trap();
			}
			superpage->hdr[0] = 0;
			return node; 
		}
	} else if(superpage->hdr + size_words < heap_end) {
		// The usable space is between the tail header and end header
		word_t* tail_hdr = superpage->hdr;
		superpage->hdr += size_words;

		// Insert the static SRAM block into the Proteus Index
		tail_hdr[0] = size_words;
		pt_idx_tree_insert(arena, arena->root, tail_hdr, size_words);
		
		pt_redblack_t* node = pt_idx_hdr_to_tree(tail_hdr, size_words);
		node->hdr[0] = size_words; // Watermark init

		superpage->hdr[0] = 0;
		return node; 
	}
	// Out of Memory: The RTOS heap is entirely consumed and fragmented
	return NULL; 
}

// Provide the mandatory FreeRTOS portable allocation hooks
void* pvPortMalloc(size_t xWantedSize) {
    return proteus_memalign(0, xWantedSize);
}

void vPortFree(void* pv) {
    proteus_free(pv);
}

void* pvPortRealloc(void* pv, size_t xWantedSize) {
    return proteus_realloc(pv, xWantedSize);
}
