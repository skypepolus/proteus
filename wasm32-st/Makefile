# ============================================================================
# Proteus Bare-Metal WebAssembly Build System
# ============================================================================

CC       = clang
CFLAGS   = -Oz -Wall -Wextra -std=c11 -fno-builtin -nostdlib \
           -Wno-implicit-fallthrough -DPT_SINGLE_THREAD --target=wasm32
INCLUDES = -I. -I../include -I../src -I../posix-st -I../deps/hybrid-lock/include

# Clang Linker flags for pure WASM
LDFLAGS  = -Wl,--no-entry \
           -Wl,--export=malloc \
           -Wl,--export=free \
           -Wl,--export=calloc \
           -Wl,--export=realloc \
           -Wl,--allow-undefined 

TARGET   = proteus-st.wasm
SRCS     = ../src/core.c ../src/index.c ../src/posix.c ../posix-st/arena.c platform.c
OBJS     = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)
	@echo "[Proteus WASM]: Successfully built bare-metal artifact -> $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
	@echo "[Proteus WASM]: Cleaned."
