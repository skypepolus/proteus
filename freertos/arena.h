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
/* freertos/arena.h */
#ifndef PT_ARENA_H
#define PT_ARENA_H

#include "primitives.h"
#include "arena-st.h" // We use the single-arena layout for RTOS

// The actual static SRAM chunk Proteus will manage
extern uint8_t ucHeap[configTOTAL_HEAP_SIZE];

void pt_freertos_init(void);

static inline pt_arena_t* pt_arena_get_local(void) 
{
    if (__builtin_expect(g_pt.num_cores == 0, 0)) {
        pt_freertos_init();
    }
    return g_pt.arenas;
}

#endif // PT_ARENA_H
