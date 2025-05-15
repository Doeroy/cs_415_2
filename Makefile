# Compiler and Compiler Flags
CC = gcc
# CFLAGS: -g for debugging, -Wall for all warnings, -Wextra for more warnings, -std=c99 to use a C standard
CFLAGS = -g -Wall -Wextra -std=c99
# LDFLAGS: Linker flags (if any, not typically needed for this project yet)
LDFLAGS =

# Source files for the string parser
STRING_PARSER_SRC = string_parser.c
STRING_PARSER_OBJ = $(STRING_PARSER_SRC:.c=.o) # string_parser.o
STRING_PARSER_HEADER = string_parser.h

# List of main executables required by the project
# Add part5 here if you are implementing the extra credit
TARGETS = part1 part2 part3 part4 # part5

# The 'all' target is the default target when you just type 'make'
# It will build all specified TARGETS
all: $(TARGETS)

# Rule to create an object file from a .c file
# This is a general pattern rule
%.o: %.c $(STRING_PARSER_HEADER)
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to link the partX executables
# Each partX executable depends on its own .c file and string_parser.o
part1: part1.c $(STRING_PARSER_OBJ)
	$(CC) $(CFLAGS) part1.c $(STRING_PARSER_OBJ) -o part1 $(LDFLAGS)

part2: part2.c $(STRING_PARSER_OBJ)
	$(CC) $(CFLAGS) part2.c $(STRING_PARSER_OBJ) -o part2 $(LDFLAGS)

part3: part3.c $(STRING_PARSER_OBJ)
	$(CC) $(CFLAGS) part3.c $(STRING_PARSER_OBJ) -o part3 $(LDFLAGS)

part4: part4.c $(STRING_PARSER_OBJ)
	$(CC) $(CFLAGS) part4.c $(STRING_PARSER_OBJ) -o part4 $(LDFLAGS)

# Optional: Rule for part5 (extra credit)
# If you create part5.c, 'make part5' or 'make all' (if part5 is in TARGETS) will build it.
part5: part5.c $(STRING_PARSER_OBJ)
	$(CC) $(CFLAGS) part5.c $(STRING_PARSER_OBJ) -o part5 $(LDFLAGS)

# --- Optional targets for compiling iobound and cpubound for your testing ---
# These are NOT part of the 'all' target as per project instructions.
# You can build them by typing 'make iobound' or 'make cpubound'.
iobound: iobound.c
	$(CC) $(CFLAGS) iobound.c -o iobound $(LDFLAGS)

cpubound: cpubound.c
	$(CC) $(CFLAGS) cpubound.c -o cpubound $(LDFLAGS)

# The 'clean' target removes all compiled files (object files and executables)
clean:
	rm -f $(TARGETS) $(STRING_PARSER_OBJ) iobound cpubound part5 # Add part5 here if you want 'make clean' to remove it

# .PHONY declares targets that are not actual files.
# This prevents 'make' from getting confused if a file named 'all' or 'clean' exists.
.PHONY: all clean iobound cpubound
