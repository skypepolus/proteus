# ============================================================================
# Proteus Quad-Target Shared Object & Static Archive Build System (GCC Edition)
# ============================================================================

CC       = gcc
CFLAGS   = -Wall -Wextra -std=c11 -fPIC -D_GNU_SOURCE
INCLUDES = -Iinclude -Isrc -Ideps/hybrid-lock/include

# Dynamic Shared Object Targets
TARGET_BENCH_SO = libproteus.so
TARGET_DEBUG_SO = libproteus_debug.so

# Static Archive Targets
TARGET_BENCH_A  = libproteus.a
TARGET_DEBUG_A  = libproteus_debug.a

# Source Enumerations
SRCS = src/core.c src/arena.c src/index.c
OBJS_BENCH = $(SRCS:src/%.c=src/%.bench.o)
OBJS_DEBUG = $(SRCS:src/%.c=src/%.debug.o)

.PHONY: all bench debug clean init-deps

# Default action builds all dynamic and static profiles simultaneously
all: bench debug

# ----------------------------------------------------------------------------
# Profile 1: Benchmark / Production Engine (-O3, Hidden Internals)
# ----------------------------------------------------------------------------
bench: $(TARGET_BENCH_SO) $(TARGET_BENCH_A)

$(TARGET_BENCH_SO): $(OBJS_BENCH)
	$(CC) -shared -O3 -flto -fno-strict-aliasing $(OBJS_BENCH) -lpthread -o $(TARGET_BENCH_SO)
	@echo "[Proteus Build]: Created Benchmark Shared Object -> $(TARGET_BENCH_SO)"

$(TARGET_BENCH_A): $(OBJS_BENCH)
	ar rcs $(TARGET_BENCH_A) $(OBJS_BENCH)
	@echo "[Proteus Build]: Created Benchmark Static Archive -> $(TARGET_BENCH_A)"

src/%.bench.o: src/%.c
	$(CC) $(CFLAGS) -O3 -flto -fno-strict-aliasing -fvisibility=hidden $(INCLUDES) -c $< -o $@

# ----------------------------------------------------------------------------
# Profile 2: High-Visibility Debug Engine (-g, -O0, Full GDB Access, No Interpose)
# ----------------------------------------------------------------------------
debug: $(TARGET_DEBUG_SO) $(TARGET_DEBUG_A)

$(TARGET_DEBUG_SO): $(OBJS_DEBUG)
	$(CC) -shared -g -O0 $(OBJS_DEBUG) -lpthread -o $(TARGET_DEBUG_SO)
	@echo "[Proteus Build]: Created Debugging Shared Object -> $(TARGET_DEBUG_SO)"

$(TARGET_DEBUG_A): $(OBJS_DEBUG)
	ar rcs $(TARGET_DEBUG_A) $(OBJS_DEBUG)
	@echo "[Proteus Build]: Created Debugging Static Archive  -> $(TARGET_DEBUG_A)"

src/%.debug.o: src/%.c
	$(CC) $(CFLAGS) -g -O0 -DDEBUG -DPT_SUPER_PAGE_BYTES=4194304 -fvisibility=default $(INCLUDES) -c $< -o $@

# ----------------------------------------------------------------------------
# Submodule Dependency Initialization
# ----------------------------------------------------------------------------
init-deps:
	git submodule update --init --recursive

# ----------------------------------------------------------------------------
# Clean Build Environment
# ----------------------------------------------------------------------------
clean:
	rm -f src/*.o *.so *.a proteus_stress_* tests/integrity_pass
	@echo "[Proteus Clean]: Build footprints cleared successfully."
