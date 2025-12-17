CC=gcc
CFLAGS=-D_POSIX_C_SOURCE=200809L -Wall -Wextra -std=c11 -g

OBJS=main.o prompt.o utils.o logger.o history.o parser.o executor.o path.o builtins.o maid_client.o

maidux: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(OBJS) maidux

.PHONY: clean
