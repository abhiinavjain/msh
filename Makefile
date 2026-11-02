# msh — build with `make`, test with `make test`.
#
#   make           debug build -> ./msh
#   make asan      AddressSanitizer + UBSan build -> ./msh-asan
#   make test      build both, then run unit tests and the bash-comparison suite
#   make clean

CC      := gcc
CSTD    := -std=c11 -D_POSIX_C_SOURCE=200809L
WARN    := -Wall -Wextra -Werror -Wpedantic -Wshadow -Wstrict-prototypes
CFLAGS  := $(CSTD) $(WARN) -O0 -g
SAN     := -fsanitize=address,undefined -fno-omit-frame-pointer

SRC     := $(wildcard src/*.c)
# Everything except main.c, so unit tests can link against the modules.
LIBSRC  := $(filter-out src/main.c,$(SRC))
HDR     := $(wildcard src/*.h)

.PHONY: all asan test unit compare clean

all: msh

msh: $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(SRC) -o $@

msh-asan: $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(SAN) $(SRC) -o $@

asan: msh-asan

tests/test_lexer: tests/test_lexer.c $(LIBSRC) $(HDR)
	$(CC) $(CFLAGS) $(SAN) -Isrc tests/test_lexer.c $(LIBSRC) -o $@

unit: tests/test_lexer
	./tests/test_lexer

compare: msh-asan
	./tests/compare.sh ./msh-asan

test: unit compare

clean:
	rm -f msh msh-asan tests/test_lexer
