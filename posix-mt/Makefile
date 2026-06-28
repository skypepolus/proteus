# ============================================================================
# Proteus Quad-Target Shared Object & Static Archive Build System
# ============================================================================

CFLAGS   = -Wall -Wextra -std=c11 -fPIC -D_GNU_SOURCE -DPT_POSIX -Wno-implicit-fallthrough -pthread
INCLUDES = -I. -I../include -I../src -I../deps/hybrid-lock/include

# ----------------------------------------------------------------------------
# OS Detection & Platform-Specific Flags
# ----------------------------------------------------------------------------
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    # macOS settings
	CC          = clang
    SHARED_EXT  = dylib
    SHARED_FLAG = -dynamiclib
else
    # Linux default settings
	CC          = gcc
    SHARED_EXT  = so
    SHARED_FLAG = -shared
endif

# Dynamic Shared Object Targets (Auto-selecting extension)
TARGET_BENCH_SO = libproteus-mt.$(SHARED_EXT)
TARGET_DEBUG_SO = libproteus-mt-debug.$(SHARED_EXT)

# Static Archive Targets
TARGET_BENCH_A  = libproteus-mt.a
TARGET_DEBUG_A  = libproteus-mt-debug.a

# Source Enumerations
SRCS = ../src/core.c ../src/index.c ../src/watermark.c ../src/posix.c
PRTS = arena.c platform.c
OBJS_BENCH = $(SRCS:../src/%.c=src/%.bench.o) $(PRTS:%.c=%.bench.o)
OBJS_DEBUG = $(SRCS:../src/%.c=src/%.debug.o) $(PRTS:%.c=%.debug.o)

.PHONY: all bench debug clean init-deps

# Default action builds all dynamic and static profiles simultaneously
all: bench debug

# ----------------------------------------------------------------------------
# Profile 1: Benchmark / Production Engine (-O3, Hidden Internals)
# ----------------------------------------------------------------------------
bench: $(TARGET_BENCH_SO) $(TARGET_BENCH_A)

$(TARGET_BENCH_SO): $(OBJS_BENCH)
	$(CC) $(SHARED_FLAG) -O3 -flto -fno-strict-aliasing $(OBJS_BENCH) -o $(TARGET_BENCH_SO)
	@echo "[Proteus Build]: Created Benchmark Shared Object -> $(TARGET_BENCH_SO)"

$(TARGET_BENCH_A): $(OBJS_BENCH)
	ar rcs $(TARGET_BENCH_A) $(OBJS_BENCH)
	@echo "[Proteus Build]: Created Benchmark Static Archive -> $(TARGET_BENCH_A)"

src/%.bench.o: ../src/%.c
	mkdir -p src
	$(CC) $(CFLAGS) -O3 -flto -fno-strict-aliasing -fvisibility=hidden $(INCLUDES) -c $< -o $@

%.bench.o: %.c
	$(CC) $(CFLAGS) -O3 -flto -fno-strict-aliasing -fvisibility=hidden $(INCLUDES) -c $< -o $@

# ----------------------------------------------------------------------------
# Profile 2: High-Visibility Debug Engine (-g, -O0, Full GDB Access, No Interpose)
# ----------------------------------------------------------------------------
debug: $(TARGET_DEBUG_SO) $(TARGET_DEBUG_A)

$(TARGET_DEBUG_SO): $(OBJS_DEBUG)
	$(CC) $(SHARED_FLAG) -g -O0 $(OBJS_DEBUG) -o $(TARGET_DEBUG_SO)
	@echo "[Proteus Build]: Created Debugging Shared Object -> $(TARGET_DEBUG_SO)"

$(TARGET_DEBUG_A): $(OBJS_DEBUG)
	ar rcs $(TARGET_DEBUG_A) $(OBJS_DEBUG)
	@echo "[Proteus Build]: Created Debugging Static Archive  -> $(TARGET_DEBUG_A)"

src/%.debug.o: ../src/%.c
	$(CC) $(CFLAGS) -g -O0 -DDEBUG -DPT_DEFAULT_SUPER_PAGE_BYTES=4194304 $(INCLUDES) -c $< -o $@

%.debug.o: %.c
	$(CC) $(CFLAGS) -g -O0 -DDEBUG -DPT_DEFAULT_SUPER_PAGE_BYTES=4194304 $(INCLUDES) -c $< -o $@

# ----------------------------------------------------------------------------
# Clean Build Environment
# ----------------------------------------------------------------------------
clean:
	rm -f *.o src/*.o *.so *.dylib *.a
	@echo "[Proteus Clean]: Build footprints cleared successfully."
