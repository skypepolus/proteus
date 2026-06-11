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
#ifndef PT_WATERMARK_H
#define PT_WATERMARK_H

extern g_pt_t g_pt;

#define pt_arena_watermark_no(coalesced_size) __builtin_expect((coalesced_size) < PT_INDEX_WATERMARK_WORDS + 8, 1) 

void* pt_arena_watermark_release(pt_arena_t* arena, pt_superpage_t* superapge, word_t* final_hdr, word_t coalesced_size);

#endif // PT_WATERMARK_H
