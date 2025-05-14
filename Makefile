EXEC = pseudo-shell
FLAGS = -g -o
CC = gcc

all: $(EXEC)
$(EXEC): part1.c string_parser.o 
	$(CC) $(FLAGS) $@ $^

main.o: main.c
	$(CC) -c $^

%.o: %.c %.h
	$(CC) -c $<

clean:
	rm -f *.o $(EXEC)
