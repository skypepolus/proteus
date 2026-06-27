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
#ifndef __platform_h__
#define __platform_h__ __platform_h__

#ifndef PT_DEFAULT_SUPER_PAGE_BYTES
#define PT_DEFAULT_SUPER_PAGE_BYTES      ((word_t)4 * 1024 * 1024 * 1024) // 4 GiB Default
#endif

#define PT_SUPER_PAGE_BYTES	g_pt.superpage_bytes 

// Dynamically derive the mask from the size
#define PT_SUPER_PAGE_MASK g_pt.superpage_mask

#define PT_SUPER_PAGE_WORDS g_pt.superpage_words

#define PT_DEFAULT_INDEX_WATERMARK_BYTES	(8 * 1024)

#define PT_INDEX_WATERMARK_BYTES	g_pt.watermark_bytes

#define PT_INDEX_WATERMARK_MASK		g_pt.watermark_mask

// Watermark threshold for the Unified Differential Filter 
#define PT_INDEX_WATERMARK_WORDS     g_pt.watermark_words

#endif/*__platform_h__*/
