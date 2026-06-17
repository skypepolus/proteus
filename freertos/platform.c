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

static uint8_t* __heap_brk = ucHeap;

int brk(void *addr)
{
	if((uint8_t*)addr < ucHeap) {
		return -1;
	}
	if(ucHeap + sizeof(ucHeap) < (uint8_t*)addr) {
		return -1;
	}
	__heap_brk = (uint8_t*)addr;
	return 0;
}

void *sbrk(intptr_t increment)
{
	if(0 == brk((void*)((uintptr_t)__heap_brk + increment)))
		return (void*)((uintptr_t)__heap_brk - increment);
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
