# ==============================================================================
# Makefile for the Proteus Elastic Memory Engine (macOS/Clang)
# ==============================================================================

CC = clang

# Compilation Flags:
# -O3: Aggressive compiler loop-unrolling and macro inlining
# -Iinclude: Exposes proteus's public headers
# -Ideps/hybrid-lock/include: Directly exposes hybrid_lock.h from the submodule
CFLAGS = -Wall -Wextra -O3 -std=c11 -Iinclude -Isrc -Ideps/hybrid-lock/include
LDFLAGS = -pthread

SRC = src/core.c src/arena.c src/index.c
OBJ = $(SRC:.c=.o)
LIB = libproteus.a

all: $(LIB)

$(LIB): $(OBJ)
	ar rcs $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Convenience rule to sync submodules if building on a fresh clone
init-deps:
	git submodule update --init --recursive

clean:
	rm -f src/*.o $(LIB)

.PHONY: all clean init-deps
