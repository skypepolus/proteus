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

// Tuned synchronization thresholds derived from our concurrency model
#define MALLOC_SPIN_COUNTER	500
#define FREE_SPIN_COUNTER	250

/* ============================================================================
 * CORE PROTOTYPES
 * ============================================================================ */
void* pt_idx_allocate_from_arena(pt_arena_t* arena, word_t size_words);
pt_redblack_t* pt_idx_tree_find_first_fit(pt_redblack_t* root, word_t size_words);

// ---> ADD THESE FIVE MISSING SIGNATURES <---
void* pt_idx_extract_and_split(pt_arena_t* arena, pt_redblack_t* node, word_t r_words);
word_t* pt_idx_coalesce_state_machine(pt_arena_t* arena, word_t* hdr, word_t* ftr, word_t* out_size_words);

#endif // PT_CORE_H
