# Auto-detect OS
UNAME_S := $(shell uname -s)

# Define compiler and flags
CC = gcc

# OS-specific flags
ifeq ($(UNAME_S),Linux)
    # Linux: OpenSSL typically in standard paths
    CFLAGS = -Wall -Wextra -std=c99 -g
    LDFLAGS = -lcurl -lcrypto
endif
ifeq ($(UNAME_S),Darwin)
    # macOS: Use Homebrew OpenSSL, compile for x86_64
    CFLAGS = -Wall -Wextra -std=c99 -g -arch x86_64 -I/usr/local/opt/openssl@3/include
    LDFLAGS = -arch x86_64 -L/usr/local/opt/openssl@3/lib -lcurl -lcrypto
endif

# Define the executable name
TARGET = lux

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

