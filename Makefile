CC = gcc
CFLAGS = -Wall -Wextra
DEBUG_FLAGS = -ggdb
RELEASE_FLAGS = -march=native -O3 -fPIC

all: debug


clean:
	rm -f trie

debug:
	$(MAKE) clean
	$(CC) $(CFLAGS) $(DEBUG_FLAGS) -o trie main.c

release:
	$(MAKE) clean
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) -o trie main.c

trie: main.c
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: debug release .FORCE
