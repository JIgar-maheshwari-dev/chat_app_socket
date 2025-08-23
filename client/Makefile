CC = gcc
AR = ar
CFLAGS = -Ilib_src -I../common_inc/ -Iinc/ -O0
LIB_SRC_DIR = lib_src
LIB_DIR = lib
#BIN_DIR = bin

OWNER := $(shell whoami)

LIB_OBJS = $(patsubst $(LIB_SRC_DIR)/%.c, $(LIB_DIR)/%.o, $(wildcard $(LIB_SRC_DIR)/*.c))
LIB = $(LIB_DIR)/libjclient.a
CLIENT_SRC = main_client.c
CLIENT_BIN = client

all: $(CLIENT_BIN)

$(LIB): $(LIB_OBJS)
	@echo "Creating static library: $@"
	$(AR) rcs $@ $^

$(LIB_DIR)/%.o: $(LIB_SRC_DIR)/%.c
	@mkdir -p $(LIB_DIR)
	$(CC) -c $< -DOWNER=\"$(OWNER)\" -o $@ $(CFLAGS)

$(CLIENT_BIN): $(CLIENT_SRC) $(LIB)
#	@mkdir -p $(BIN_DIR)
	$(CC) $< -L$(LIB_DIR) -ljclient $(CFLAGS) -o $@ 

clean:
	rm -rf $(LIB_DIR)/*.o $(LIB) $(CLIENT_BIN)
