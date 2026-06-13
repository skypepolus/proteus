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

#ifndef __debug_h__
#define __debug_h__ __debug_h__

#ifdef DEBUG

#include <stdlib.h>

void *pt_debug_malloc(size_t size);
void pt_debug_free(void *ptr);
void *pt_debug_realloc(void *ptr, size_t size);

#define malloc(size) pt_debug_malloc(size)
#define free(ptr) pt_debug_free(ptr)
#define realloc(ptr, size) pt_debug_realloc(ptr, size)

#endif

#endif/*__debug_h__*/
