# freertos/Makefile

CC = gcc
CFLAGS = -Wall -Wextra -O0 -g -std=c11 -Wno-implicit-fallthrough -pthread -DPT_SINGLE_THREAD -D_GNU_SOURCE 
# Includes for Proteus and FreeRTOS
INCLUDES = -I. \
           -I../include \
           -I../src \
           -I../deps/hybrid-lock/include \
           -I./FreeRTOS-Kernel/include \
           -I./FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix

# Proteus Core Sources
PROTEUS_SRCS = ../src/core.c ../src/index.c ../src/platform-st.c arena.c platform.c

# FreeRTOS Kernel Sources
FREERTOS_SRCS = FreeRTOS-Kernel/tasks.c \
                FreeRTOS-Kernel/queue.c \
                FreeRTOS-Kernel/list.c \
                FreeRTOS-Kernel/timers.c \
                FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix/port.c \
                FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c

# Test Application
APP_SRCS = main.c

ALL_SRCS = $(PROTEUS_SRCS) $(FREERTOS_SRCS) $(APP_SRCS)

# We do NOT compile a FreeRTOS heap_x.c file because Proteus IS the heap manager!

TARGET = proteus-freertos-sim

all: $(TARGET)

$(TARGET): $(ALL_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) $(ALL_SRCS) -o $(TARGET)
	@echo "[Proteus FreeRTOS]: Simulator compiled successfully -> $(TARGET)"

clean:
	rm -f $(TARGET)
