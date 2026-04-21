.PHONY: all test asan-ubsan msan tsan valgrind fuzz format format-file lint lint-fix lint-file help

SOURCES := $(shell find src src/utils include -type f \( -name "*.c" -o -name "*.h" \))

test:
	@mkdir -p build
	@cmake -B build -DECEWO_BUILD_TESTS=ON
	@cmake --build build -j$(nproc)
	@ctest --test-dir build

asan-ubsan:
	@mkdir -p build-asan-ubsan
	@( \
		cmake -B build-asan-ubsan \
			-DCMAKE_BUILD_TYPE=Debug \
			-DCMAKE_C_COMPILER=clang \
			-DECEWO_BUILD_TESTS=ON \
			-DECEWO_ASAN=ON \
			-DECEWO_UBSAN=ON && \
		cmake --build build-asan-ubsan -j$(nproc) && \
		ctest --test-dir build-asan-ubsan --output-on-failure --verbose \
	)

msan:
	@mkdir -p build-msan
	@( \
		cmake -B build-msan \
			-DCMAKE_BUILD_TYPE=Debug \
			-DCMAKE_C_COMPILER=clang \
			-DCMAKE_CXX_COMPILER=clang++ \
			-DECEWO_BUILD_TESTS=ON \
			-DECEWO_MSAN=ON && \
		cmake --build build-msan -j$(nproc) && \
		ctest --test-dir build-msan --output-on-failure --verbose \
	)

tsan:
	@mkdir -p build-tsan
	@( \
		cmake -B build-tsan \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_C_COMPILER=clang \
			-DCMAKE_CXX_COMPILER=clang++ \
			-DECEWO_BUILD_TESTS=ON \
			-DECEWO_TSAN=ON && \
		cmake --build build-tsan -j$(nproc) && \
		ctest --test-dir build-tsan --output-on-failure --verbose \
	)

valgrind:
	@mkdir -p build-valgrind
	@( \
		cmake -B build-valgrind \
			-DCMAKE_BUILD_TYPE=Debug \
			-DECEWO_BUILD_TESTS=ON \
			-DCTEST_MEMORYCHECK_COMMAND=valgrind \
			-DCTEST_MEMORYCHECK_COMMAND_OPTIONS="--leak-check=full;--show-leak-kinds=definite,indirect;--error-exitcode=1" && \
		cmake --build build-valgrind -j$(nproc) && \
		ctest --test-dir build-valgrind \
			--output-on-failure \
			--verbose \
			-T memcheck \
			--timeout 300 \
	)

fuzz:
	@mkdir -p build-fuzz
	@( \
		cmake -B build-fuzz \
			-DCMAKE_BUILD_TYPE=Debug \
			-DCMAKE_C_COMPILER=clang \
			-DECEWO_BUILD_FUZZ=ON && \
		cmake --build build-fuzz -j$(nproc) \
	)
	@echo "Fuzz targets built in build-fuzz/. Run with:"
	@echo "  mkdir -p fuzz/corpus && ./build-fuzz/fuzz-router fuzz/corpus -max_len=4096"
	@echo "  mkdir -p fuzz/corpus && ./build-fuzz/fuzz-route-register fuzz/corpus -max_len=4096"

all: test asan-ubsan msan tsan valgrind

format:
	@clang-format -i $(SOURCES)

format-file:
	@clang-format -i $(FILE)

lint:
	find src -name "*.c" | xargs clang-tidy -p build

lint-fix:
	@echo "Running clang-tidy with auto-fix..."
	@clang-tidy -p build $(SOURCES) --fix-errors

lint-file:
	@test -n "$(FILE)" || (echo "Usage: make lint-file FILE=path/to/file.c" && exit 1)
	@clang-tidy -p build $(FILE)

help:
	@printf "\nAvailable targets:\n\n"
	@printf "Build and run tests:\n"
	@printf "%-40s %s\n" "make test" "Build and run tests"
	@printf "%-40s %s\n" "make asan-ubsan" "Build and run tests with ASAN+UBSAN"
	@printf "%-40s %s\n" "make msan" "Build and run tests with MSAN"
	@printf "%-40s %s\n" "make tsan" "Build and run tests with TSAN"
	@printf "%-40s %s\n" "make valgrind" "Build and run tests with Valgrind"
	@printf "%-40s %s\n" "make fuzz" "Build libFuzzer targets (requires Clang)"
	@printf "%-40s %s\n" "make all" "Run all of them sequentially"
	@printf "\n"
	@printf "Formatting:\n"
	@printf "%-40s %s\n" "make format" "Run clang-format"
	@printf "%-40s %s\n" "make format-file FILE=src/file.c" "Format single file"
	@printf "\n"
	@printf "Linting:\n"
	@printf "%-40s %s\n" "make lint" "Run clang-tidy"
	@printf "%-40s %s\n" "make lint-fix" "Auto-fix issues where possible"
	@printf "%-40s %s\n" "make lint-file FILE=src/file.c" "Check single file"
	@printf "\n"
