/* wasm32-st/platform.c */
#include "platform.h"
#include "primitives.h"
#include "arena.h"
#include "index.h"
#include <stddef.h>
#include <stdint.h>

int errno;

// Symbol provided automatically by the LLVM/Clang WASM linker
extern unsigned char __heap_base;
// Track the top of the WASM linear memory
static uint8_t* __heap_brk = (uint8_t*)&__heap_base;

// Replaces the POSIX brk() system call
int brk(void *addr)
{
	if((uint8_t*)addr < (uint8_t*)&__heap_base) {
		return -1;
	} else if((uint8_t*)(__builtin_wasm_memory_size(0) << 16) < (uint8_t*)addr) {
		uintptr_t delta = (uintptr_t)addr - (uintptr_t)(__builtin_wasm_memory_size(0) << 16);
		delta = (delta + PT_INDEX_WATERMARK_BYTES - 1) & ~((uintptr_t)PT_INDEX_WATERMARK_BYTES - 1);
		if((size_t)(-1) == __builtin_wasm_memory_grow(0, delta >> 16)) {
			return -1;
		}
	}
	__heap_brk = (uint8_t*)addr;
	return 0;
}

void *sbrk(intptr_t increment)
{
	if(__heap_brk + increment < &__heap_base) {
		return NULL;
	} else if((uint8_t*)(__builtin_wasm_memory_size(0) << 16) < __heap_brk + increment) {
		uintptr_t delta = __heap_brk + increment - (uint8_t*)(__builtin_wasm_memory_size(0) << 16);
		delta = (delta + PT_INDEX_WATERMARK_BYTES - 1) & ~((uintptr_t)PT_INDEX_WATERMARK_BYTES - 1);
		if((size_t)(-1) == __builtin_wasm_memory_grow(0, delta >> 16)) {
			return NULL;
		}
	}
	__heap_brk += increment;
	return __heap_brk;
}

// Minimal bare-metal string utilities for core.c
void* memset(void* dest, int val, size_t len) {
    unsigned char* ptr = dest;
    while (len-- > 0) *ptr++ = val;
    return dest;
}

void* memcpy(void* dest, const void* src, size_t len) {
    char* d = dest;
    const char* s = src;
    while (len--) *d++ = *s++;
    return dest;
}

