CC     = gcc
CFLAGS = -O2 -Wall -Wextra

rapl: rapl.c
	$(CC) $(CFLAGS) -o $@ $< -lm

clean:
	rm -f rapl

.PHONY: clean
