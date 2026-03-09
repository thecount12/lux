# Auto-detect OS
UNAME_S := $(shell uname -s)

# Define compiler and flags
CC = gcc

# Database configuration (set to 1 to enable, 0 to disable)
# By default, all disabled to keep Lux lightweight
USE_SQLITE ?= 0
USE_POSTGRES ?= 0
USE_MYSQL ?= 0
USE_ORACLE ?= 0

# Oracle Instant Client paths (adjust as needed)
ORACLE_HOME ?= /opt/oracle/instantclient_21_1
ORACLE_INC := $(ORACLE_HOME)/sdk/include
ORACLE_LIB := $(ORACLE_HOME)

# Database flags
DB_FLAGS =
DB_LDFLAGS =

ifeq ($(USE_SQLITE),1)
    DB_FLAGS += -DDB_SQLITE
    DB_LDFLAGS += -lsqlite3
endif

ifeq ($(USE_POSTGRES),1)
    DB_FLAGS += -DDB_POSTGRES
    DB_LDFLAGS += -lpq
endif

ifeq ($(USE_MYSQL),1)
    DB_FLAGS += -DDB_MYSQL
    DB_LDFLAGS += -lmysqlclient
endif

ifeq ($(USE_ORACLE),1)
    DB_FLAGS += -DDB_ORACLE -I$(ORACLE_INC)
    DB_LDFLAGS += -L$(ORACLE_LIB) -lclntsh
endif

# OS-specific flags
ifeq ($(UNAME_S),Linux)
    # Linux: OpenSSL typically in standard paths
    CFLAGS = -Wall -Wextra -std=c99 -g $(DB_FLAGS)
    LDFLAGS = -lcurl -lcrypto $(DB_LDFLAGS)
endif
ifeq ($(UNAME_S),Darwin)
    # macOS: Use Homebrew OpenSSL, compile for x86_64
    CFLAGS = -Wall -Wextra -std=c99 -g -arch x86_64 -I/usr/local/opt/openssl@3/include $(DB_FLAGS)
    LDFLAGS = -arch x86_64 -L/usr/local/opt/openssl@3/lib -lcurl -lcrypto $(DB_LDFLAGS)
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

