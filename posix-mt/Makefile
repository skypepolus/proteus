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
    OZ          = -Oz
else
    # Linux default settings
	CC          = gcc
    SHARED_EXT  = so
    SHARED_FLAG = -shared
    OZ          = -Os
endif

# Dynamic Shared Object Targets (Auto-selecting extension)
TARGET_CMPCTCR_SO = libproteus-crz.$(SHARED_EXT)
TARGET_CMPCTMT_SO = libproteus-mtz.$(SHARED_EXT)
TARGET_BENCHCR_SO = libproteus-cr3.$(SHARED_EXT)
TARGET_BENCHMT_SO = libproteus-mt3.$(SHARED_EXT)
TARGET_DEBUG_SO = libproteus-mt-debug.$(SHARED_EXT)

# Static Archive Targets
TARGET_CMPCTCR_A = libproteus-crz.a
TARGET_CMPCTMT_A = libproteus-mtz.a
TARGET_BENCHCR_A = libproteus-cr3.a
TARGET_BENCHMT_A = libproteus-mt3.a
TARGET_DEBUG_A  = libproteus-mt-debug.a

# Source Enumerations
SRCS = ../src/core.c ../src/index.c ../src/watermark.c ../src/posix.c
PRTS = arena.c platform.c
OBJS_CMPCTCR = $(SRCS:../src/%.c=src/%.cmpctcr.o) $(PRTS:%.c=%.cmpctcr.o)
OBJS_CMPCTMT = $(SRCS:../src/%.c=src/%.cmpctmt.o) $(PRTS:%.c=%.cmpctmt.o)
OBJS_BENCHCR = $(SRCS:../src/%.c=src/%.benchcr.o) $(PRTS:%.c=%.benchcr.o)
OBJS_BENCHMT = $(SRCS:../src/%.c=src/%.benchmt.o) $(PRTS:%.c=%.benchmt.o)
OBJS_DEBUG = $(SRCS:../src/%.c=src/%.debug.o) $(PRTS:%.c=%.debug.o)

.PHONY: all cmpctcr cmpctmt benchcr benchmt debug clean init-deps

# Default action builds all dynamic and static profiles simultaneously
all: cmpctcr cmpctmt benchcr benchmt debug
	ls -l *.$(SHARED_EXT)
	ls -l *.a

# ----------------------------------------------------------------------------
# Profile 1: Compact / Core Engine (-Oz, Hidden Internals)
# ----------------------------------------------------------------------------
cmpctcr: $(TARGET_CMPCTCR_SO) $(TARGET_CMPCTCR_A)

$(TARGET_CMPCTCR_SO): $(OBJS_CMPCTCR)
	$(CC) $(SHARED_FLAG) $(OZ) -flto -fno-strict-aliasing $(OBJS_CMPCTCR) -o $(TARGET_CMPCTCR_SO)
	@echo "[Proteus Build]: Created Benchmark Shared Object -> $(TARGET_CMPCTCR_SO)"

$(TARGET_CMPCTCR_A): $(OBJS_CMPCTCR)
	ar rcs $(TARGET_CMPCTCR_A) $(OBJS_CMPCTCR)
	@echo "[Proteus Build]: Created Benchmark Static Archive -> $(TARGET_CMPCTCR_A)"

src/%.cmpctcr.o: ../src/%.c
	mkdir -p src
	$(CC) $(CFLAGS) $(OZ) -DPT_CORE -flto -fno-strict-aliasing -fvisibility=hidden $(INCLUDES) -c $< -o $@

%.cmpctcr.o: %.c
	$(CC) $(CFLAGS) $(OZ) -DPT_CORE -flto -fno-strict-aliasing -fvisibility=hidden $(INCLUDES) -c $< -o $@
ifeq ($(UNAME_S),Darwin)
# ----------------------------------------------------------------------------
# Profile 2: Compact / Multi-Thread Engine (-Oz, Hidden Internals)
# ----------------------------------------------------------------------------
cmpctmt: $(TARGET_CMPCTMT_SO) $(TARGET_CMPCTMT_A)

$(TARGET_CMPCTMT_SO): $(OBJS_CMPCTMT)
	$(CC) $(SHARED_FLAG) $(OZ) -flto -fno-strict-aliasing $(OBJS_CMPCTMT) -o $(TARGET_CMPCTMT_SO)
	@echo "[Proteus Build]: Created Benchmark Shared Object -> $(TARGET_CMPCTMT_SO)"

$(TARGET_CMPCTMT_A): $(OBJS_CMPCTMT)
	ar rcs $(TARGET_CMPCTMT_A) $(OBJS_CMPCTMT)
	@echo "[Proteus Build]: Created Benchmark Static Archive -> $(TARGET_CMPCTMT_A)"

src/%.cmpctmt.o: ../src/%.c
	mkdir -p src
	$(CC) $(CFLAGS) $(OZ) -flto -fno-strict-aliasing -fvisibility=hidden $(INCLUDES) -c $< -o $@

%.cmpctmt.o: %.c
	$(CC) $(CFLAGS) $(OZ) -flto -fno-strict-aliasing -fvisibility=hidden $(INCLUDES) -c $< -o $@
endif
# ----------------------------------------------------------------------------
# Profile 3: Optimized / Core Engine (-O3, Hidden Internals)
# ----------------------------------------------------------------------------
benchcr: $(TARGET_BENCHCR_SO) $(TARGET_BENCHCR_A)

$(TARGET_BENCHCR_SO): $(OBJS_BENCHCR)
	$(CC) $(SHARED_FLAG) -O3 -flto -fno-strict-aliasing $(OBJS_BENCHCR) -o $(TARGET_BENCHCR_SO)
	@echo "[Proteus Build]: Created Benchmark Shared Object -> $(TARGET_BENCHCR_SO)"

$(TARGET_BENCHCR_A): $(OBJS_BENCHCR)
	ar rcs $(TARGET_BENCHCR_A) $(OBJS_BENCHCR)
	@echo "[Proteus Build]: Created Benchmark Static Archive -> $(TARGET_BENCHCR_A)"

src/%.benchcr.o: ../src/%.c
	mkdir -p src
	$(CC) $(CFLAGS) -O3 -DPT_CORE -flto -fno-strict-aliasing -fvisibility=hidden $(INCLUDES) -c $< -o $@

%.benchcr.o: %.c
	$(CC) $(CFLAGS) -O3 -DPT_CORE -flto -fno-strict-aliasing -fvisibility=hidden $(INCLUDES) -c $< -o $@
ifeq ($(UNAME_S),Darwin)
# ----------------------------------------------------------------------------
# Profile 4: Optimized / Multi-Thread Engine (-O3, Hidden Internals)
# ----------------------------------------------------------------------------
benchmt: $(TARGET_BENCHMT_SO) $(TARGET_BENCHMT_A)

$(TARGET_BENCHMT_SO): $(OBJS_BENCHMT)
	$(CC) $(SHARED_FLAG) -O3 -flto -fno-strict-aliasing $(OBJS_BENCHMT) -o $(TARGET_BENCHMT_SO)
	@echo "[Proteus Build]: Created Benchmark Shared Object -> $(TARGET_BENCHMT_SO)"

$(TARGET_BENCHMT_A): $(OBJS_BENCHMT)
	ar rcs $(TARGET_BENCHMT_A) $(OBJS_BENCHMT)
	@echo "[Proteus Build]: Created Benchmark Static Archive -> $(TARGET_BENCHMT_A)"

src/%.benchmt.o: ../src/%.c
	mkdir -p src
	$(CC) $(CFLAGS) -O3 -flto -fno-strict-aliasing -fvisibility=hidden $(INCLUDES) -c $< -o $@

%.benchmt.o: %.c
	$(CC) $(CFLAGS) -O3 -flto -fno-strict-aliasing -fvisibility=hidden $(INCLUDES) -c $< -o $@
endif
# ----------------------------------------------------------------------------
# Profile 5: High-Visibility Debug Engine (-g, -O0, Full GDB Access, No Interpose)
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
