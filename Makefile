EXEC = pseudo-shell
FLAGS = -g -o
CC = gcc

all: $(EXEC)
$(EXEC): main.o string_parser.o 
	$(CC) $(FLAGS) $@ $^

main.o: main.c
	$(CC) -c $^

%.o: %.c %.h
	$(CC) -c $<

clean:
	rm -f *.o $(EXEC)