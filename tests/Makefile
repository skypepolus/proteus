# ============================================================================
# Proteus Multi-Sanitizer Test Automation Matrix Build System
# ============================================================================

CC       = gcc
# Base compilation flags for testing: full debug symbols, no optimization,
# feature macros enabled, and superpages downsized to 4MB for high test coverage.
BASE_CFLAGS = -Wall -Wextra -std=c11 -g -O0 -D_GNU_SOURCE -DDEBUG -DPT_SUPER_PAGE_BYTES=4194304
INCLUDES    = -I../include -I../src -I../deps/hybrid-lock/include
LDFLAGS     = -lpthread

# Core allocator engine source files
CORE_SRCS = ../src/core.c ../src/arena.c ../src/index.c

# Test suite specifications
SRC_INTEGRITY = test_integrity.c
SRC_STRESS    = test_stress.c

# Sanitizer Configuration Matrices
SAN_ASAN            = -fsanitize=address
SAN_TSAN            = -fsanitize=thread
SAN_ASAN_UBSAN      = -fsanitize=address,undefined
SAN_TSAN_UBSAN      = -fsanitize=thread,undefined

# Resulting Executable Targets
BINS_INTEGRITY = integrity_pass_asan integrity_pass_tsan integrity_pass_asan_ubsan integrity_pass_tsan_ubsan
BINS_STRESS    = test_stress_asan test_stress_tsan test_stress_asan_ubsan test_stress_tsan_ubsan
ALL_BINS       = $(BINS_INTEGRITY) $(BINS_STRESS)

.PHONY: all clean run_all run_integrity run_stress

# Default action compiles every test variant
all: $(ALL_BINS)

# ----------------------------------------------------------------------------
# Compilation Rules: Integrity Suite
# ----------------------------------------------------------------------------
integrity_pass_asan: $(CORE_SRCS) $(SRC_INTEGRITY)
	$(CC) $(BASE_CFLAGS) $(SAN_ASAN) $(INCLUDES) $^ $(LDFLAGS) -o $@

integrity_pass_tsan: $(CORE_SRCS) $(SRC_INTEGRITY)
	$(CC) $(BASE_CFLAGS) $(SAN_TSAN) $(INCLUDES) $^ $(LDFLAGS) -o $@

integrity_pass_asan_ubsan: $(CORE_SRCS) $(SRC_INTEGRITY)
	$(CC) $(BASE_CFLAGS) $(SAN_ASAN_UBSAN) $(INCLUDES) $^ $(LDFLAGS) -o $@

integrity_pass_tsan_ubsan: $(CORE_SRCS) $(SRC_INTEGRITY)
	$(CC) $(BASE_CFLAGS) $(SAN_TSAN_UBSAN) $(INCLUDES) $^ $(LDFLAGS) -o $@

# ----------------------------------------------------------------------------
# Compilation Rules: Multi-Threaded Stress Engine Suite
# ----------------------------------------------------------------------------
test_stress_asan: $(CORE_SRCS) $(SRC_STRESS)
	$(CC) $(BASE_CFLAGS) $(SAN_ASAN) $(INCLUDES) $^ $(LDFLAGS) -o $@

test_stress_tsan: $(CORE_SRCS) $(SRC_STRESS)
	$(CC) $(BASE_CFLAGS) $(SAN_TSAN) $(INCLUDES) $^ $(LDFLAGS) -o $@

test_stress_asan_ubsan: $(CORE_SRCS) $(SRC_STRESS)
	$(CC) $(BASE_CFLAGS) $(SAN_ASAN_UBSAN) $(INCLUDES) $^ $(LDFLAGS) -o $@

test_stress_tsan_ubsan: $(CORE_SRCS) $(SRC_STRESS)
	$(CC) $(BASE_CFLAGS) $(SAN_TSAN_UBSAN) $(INCLUDES) $^ $(LDFLAGS) -o $@

# ----------------------------------------------------------------------------
# Test Execution and Automation Targets
# ----------------------------------------------------------------------------

# Runs both structural integrity and multi-threaded stress matrices sequentially
run_all: run_integrity run_stress

run_integrity: $(BINS_INTEGRITY)
	@echo "\n=== [Executing Integrity Matrix] ==="
	@echo "-> Running Integrity (AddressSanitizer)..." && ./integrity_pass_asan
	@echo "-> Running Integrity (ThreadSanitizer)..." && ./integrity_pass_tsan
	@echo "-> Running Integrity (Address + UndefinedSanitizer)..." && ./integrity_pass_asan_ubsan
	@echo "-> Running Integrity (Thread + UndefinedSanitizer)..." && ./integrity_pass_tsan_ubsan
	@echo "=== [Integrity Matrix Completed Successfully] ===\n"

run_stress: $(BINS_STRESS)
	@echo "\n=== [Executing Concurrency Stress Matrix] ==="
	@echo "-> Running Stress (AddressSanitizer)..." && ./test_stress_asan
	@echo "-> Running Stress (ThreadSanitizer)..." && ./test_stress_tsan
	@echo "-> Running Stress (Address + UndefinedSanitizer)..." && ./test_stress_asan_ubsan
	@echo "-> Running Stress (Thread + UndefinedSanitizer)..." && ./test_stress_tsan_ubsan
	@echo "=== [Concurrency Stress Matrix Completed Flawlessly] ===\n"

# ----------------------------------------------------------------------------
# Clean Environment
# ----------------------------------------------------------------------------
clean:
	rm -f $(ALL_BINS)
	@echo "[Proteus Test Matrix]: Compiled test execution footprints cleared."
