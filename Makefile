build: main.c
	$(CC) main.c -g -o mx-fzf -Wall -Wextra -pedantic -std=c99 -lsqlite3 -Wno-unused-parameter

install: build
	cp mx-fzf /usr/local/bin
