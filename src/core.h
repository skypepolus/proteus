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
#ifndef PT_CORE_H
#define PT_CORE_H

#include "primitives.h"

// Tuned synchronization thresholds derived from our concurrency model
#define DEFAULT_MALLOC_SPIN_COUNTER	500
#define MALLOC_SPIN_COUNTER	g_pt.malloc_spin
#define FREE_SPIN_COUNTER	250

/* ============================================================================
 * CORE PROTOTYPES
 * ============================================================================ */
word_t* pt_core_allocate_superpage_fallback(pt_arena_t* arena, word_t size_words);

#endif // PT_CORE_H
