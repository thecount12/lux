# Define compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
LDFLAGS = -lcurl

# Define the executable name
TARGET = my_program

# Define the source files
SRCS = main.c chunk.c memory.c debug.c value.c vm.c compiler.c scanner.c object.c table.c

# Automatically determine object files from source files
OBJS = $(SRCS:.c=.o)

# Define the header files for dependency tracking
DEPS = chunk.h memory.h common.h debug.h value.h vm.h compiler.h scanner.h object.h table.h

# The default target, which builds the executable
all: $(TARGET)

# Rule to link the object files into the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Pattern rule to compile .c files into .o files
# Dependencies on header files are explicitly listed
main.o: main.c chunk.h memory.h common.h debug.h
chunk.o: chunk.c chunk.h common.h debug.h value.h
memory.o: memory.c memory.h common.h debug.h
debug.o: debug.c debug.h memory.h common.h
value.o: value.c value.h memory.h common.h

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

# Phony targets don't create a file of the same name
.PHONY: all clean

# Rule to clean up generated files
clean:
	rm -f $(OBJS) $(TARGET)

