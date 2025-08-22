CC = gcc
AR = ar
CFLAGS = -Ilib_src -I../ -O0 -g
LIB_SRC_DIR = lib_src
LIB_DIR = lib
#BIN_DIR = bin

OWNER := $(shell whoami)


LIB_OBJS = $(patsubst $(LIB_SRC_DIR)/%.c, $(LIB_DIR)/%.o, $(wildcard $(LIB_SRC_DIR)/*.c))
LIB = $(LIB_DIR)/libjserver.a
SERVER_SRC = main_server.c
SERVER_BIN = server

all: $(SERVER_BIN)

# Create static library if needed
$(LIB): $(LIB_OBJS)
	@echo "Creating static library: $@"
	$(AR) rcs $@ $^

# Compile object files for the library
$(LIB_DIR)/%.o: $(LIB_SRC_DIR)/%.c
	@mkdir -p $(LIB_DIR)
	$(CC) -c $< -DOWNER=\"$(OWNER)\" -o $@ $(CFLAGS)

# Build server binary and link with static library
$(SERVER_BIN): $(SERVER_SRC) $(LIB)
#	@mkdir -p $(BIN_DIR)
	$(CC) $< -L$(LIB_DIR) -ljserver $(CFLAGS) -o $@

clean:
	rm -rf $(LIB_DIR)/*.o $(LIB) $(SERVER_BIN)
