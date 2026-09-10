# Auto-detect OS
UNAME_S := $(shell uname -s)

# Define compiler and flags
CC ?= cc
OPT ?= -O2

# Database configuration (set to 1 to enable, 0 to disable)
# By default, all disabled to keep Lux lightweight
USE_SQLITE ?= 0
USE_POSTGRES ?= 0
USE_MYSQL ?= 0
USE_ORACLE ?= 0

# Oracle Instant Client paths (override with ORACLE_HOME=/path/to/instantclient)
ORACLE_HOME ?= /opt/oracle/instantclient_21_1

# Database flags
DB_FLAGS =
DB_LDFLAGS =

# _DEFAULT_SOURCE exposes POSIX APIs (popen, fork, waitpid, etc.) under -std=c99.
# Default flags for unknown POSIX systems.
CFLAGS = -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE -g $(OPT) $(DB_FLAGS)
LDFLAGS = -lcurl -lcrypto -lm $(DB_LDFLAGS)

ifeq ($(USE_SQLITE),1)
    DB_FLAGS += -DDB_SQLITE
    SQLITE_CFLAGS := $(shell pkg-config --cflags sqlite3 2>/dev/null)
    SQLITE_LIBS := $(shell pkg-config --libs sqlite3 2>/dev/null)
    ifneq ($(SQLITE_CFLAGS),)
        DB_FLAGS += $(SQLITE_CFLAGS)
    endif
    ifneq ($(SQLITE_LIBS),)
        DB_LDFLAGS += $(SQLITE_LIBS)
    else
        DB_LDFLAGS += -lsqlite3
    endif
endif

ifeq ($(USE_POSTGRES),1)
    DB_FLAGS += -DDB_POSTGRES
    # Debian/Ubuntu install libpq-fe.h under /usr/include/postgresql, not /usr/include.
    PG_CFLAGS := $(shell pkg-config --cflags libpq 2>/dev/null)
    PG_LIBS := $(shell pkg-config --libs libpq 2>/dev/null)
    ifneq ($(PG_CFLAGS),)
        DB_FLAGS += $(PG_CFLAGS)
    else ifneq ($(wildcard /usr/include/postgresql/libpq-fe.h),)
        DB_FLAGS += -I/usr/include/postgresql
    endif
    ifneq ($(PG_LIBS),)
        DB_LDFLAGS += $(PG_LIBS)
    else
        DB_LDFLAGS += -lpq
    endif
endif

ifeq ($(USE_MYSQL),1)
    DB_FLAGS += -DDB_MYSQL
    MYSQL_CFLAGS := $(shell mysql_config --cflags 2>/dev/null)
    MYSQL_LIBS := $(shell mysql_config --libs 2>/dev/null)
    ifeq ($(MYSQL_CFLAGS),)
        MYSQL_CFLAGS := $(shell pkg-config --cflags mysqlclient 2>/dev/null)
        MYSQL_LIBS := $(shell pkg-config --libs mysqlclient 2>/dev/null)
    endif
    ifneq ($(MYSQL_CFLAGS),)
        DB_FLAGS += $(MYSQL_CFLAGS)
    endif
    ifneq ($(MYSQL_LIBS),)
        DB_LDFLAGS += $(MYSQL_LIBS)
    else
        DB_LDFLAGS += -lmysqlclient
    endif
endif

ifeq ($(USE_ORACLE),1)
    # Zip Instant Client: $ORACLE_HOME/sdk/include/oci.h
    # Some layouts: $ORACLE_HOME/include/oci.h
    # RPM/deb: /usr/include/oracle/<ver>/client64/oci.h
    ifneq ($(wildcard $(ORACLE_HOME)/sdk/include/oci.h),)
        ORACLE_INC := $(ORACLE_HOME)/sdk/include
        ORACLE_LIB := $(ORACLE_HOME)
    else ifneq ($(wildcard $(ORACLE_HOME)/include/oci.h),)
        ORACLE_INC := $(ORACLE_HOME)/include
        ifneq ($(wildcard $(ORACLE_HOME)/lib/libclntsh.so*),)
            ORACLE_LIB := $(ORACLE_HOME)/lib
        else
            ORACLE_LIB := $(ORACLE_HOME)
        endif
    else
        ORACLE_INC := $(firstword $(wildcard /usr/include/oracle/*/client64 /usr/include/oracle/*/client))
        ORACLE_LIB := $(firstword $(wildcard /usr/lib/oracle/*/client64/lib /usr/lib/oracle/*/client/lib))
    endif
    ifeq ($(wildcard $(ORACLE_INC)/oci.h),)
        $(error Oracle Instant Client SDK not found (oci.h). Install Basic+SDK, or set ORACLE_HOME. Search: find /usr /opt $(HOME) -name oci.h 2>/dev/null)
    endif
    DB_FLAGS += -DDB_ORACLE -I$(ORACLE_INC)
    DB_LDFLAGS += -L$(ORACLE_LIB) -lclntsh -Wl,-rpath,$(ORACLE_LIB)
endif

# OS-specific flags
ifeq ($(UNAME_S),Linux)
    # Linux: OpenSSL typically in standard paths
    CFLAGS = -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE -g $(OPT) $(DB_FLAGS)
    LDFLAGS = -lcurl -lcrypto -lm $(DB_LDFLAGS)
endif
ifeq ($(UNAME_S),Darwin)
    # macOS: Use Homebrew OpenSSL, compile for x86_64
    CFLAGS = -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE -g $(OPT) -arch x86_64 -I/usr/local/opt/openssl@3/include $(DB_FLAGS)
    LDFLAGS = -arch x86_64 -L/usr/local/opt/openssl@3/lib -lcurl -lcrypto -lm $(DB_LDFLAGS)
endif
ifeq ($(UNAME_S),OpenBSD)
    # OpenBSD: use ports-installed curl and OpenSSL under /usr/local.
    CFLAGS = -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE -g $(OPT) -I/usr/local/include $(DB_FLAGS)
    LDFLAGS = -L/usr/local/lib -lcurl -lcrypto -lm $(DB_LDFLAGS)
endif

# Define the executable name
TARGET = lux
FMT_TARGET = luxfmt
TEST_TARGET = luxtest

# Define the source files
SRCS = main.c chunk.c memory.c debug.c value.c vm.c compiler.c scanner.c object.c table.c dict.c dict_native.c file_native.c template_render.c markdown.c
FMT_SRC = luxfmt.c
TEST_SRC = luxtest.c

# Automatically determine object files from source files
OBJS = $(SRCS:.c=.o)
FMT_OBJ = $(FMT_SRC:.c=.o)
TEST_OBJ = $(TEST_SRC:.c=.o)

# Define the header files for dependency tracking
DEPS = chunk.h memory.h common.h debug.h value.h vm.h compiler.h scanner.h object.h table.h dict.h template_render.h markdown.h

# The default target, which builds the executable
all: $(TARGET)

# Build the formatter utility
$(FMT_TARGET): $(FMT_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

# Build the test runner utility
$(TEST_TARGET): $(TEST_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

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

# Build the luxcheck utility
LUXCHECK_TARGET = luxcheck
LUXCHECK_SRC = luxcheck.c
LUXCHECK_OBJ = $(LUXCHECK_SRC:.c=.o)

$(LUXCHECK_TARGET): $(LUXCHECK_OBJ) compiler.o scanner.o object.o memory.o value.o chunk.o table.o dict.o dict_native.o file_native.o template_render.o markdown.o vm.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Build the luxlint utility
LUXLINT_TARGET = luxlint
LUXLINT_SRC = luxlint.c
LUXLINT_OBJ = $(LUXLINT_SRC:.c=.o)

$(LUXLINT_TARGET): $(LUXLINT_OBJ) compiler.o scanner.o object.o memory.o value.o chunk.o table.o dict.o dict_native.o file_native.o template_render.o markdown.o vm.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Phony targets don't create a file of the same name
.PHONY: all clean $(FMT_TARGET) $(TEST_TARGET) $(LUXCHECK_TARGET) $(LUXLINT_TARGET)

# Rule to clean up generated files
clean:
	rm -f $(OBJS) $(FMT_OBJ) $(TEST_OBJ) $(LUXCHECK_OBJ) $(LUXLINT_OBJ) $(TARGET) $(FMT_TARGET) $(TEST_TARGET) $(LUXCHECK_TARGET) $(LUXLINT_TARGET)

