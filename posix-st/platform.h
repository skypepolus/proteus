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

#define hybrid_try(lock) (1)
#define hybrid_lock(lock, spin) do { } while(0)
#define hybrid_unlock(lock) do { } while(0)

#define PT_SINGLE_THREAD PT_SINGLE_THREAD

#define PT_SUPER_PAGE_WORDS      ((word_t)(UINTPTR_MAX / PT_WORD_SIZE_BYTES))

#define PT_INDEX_WATERMARK_BYTES	(64 * 1024)

#endif/*__platform_h__*/
