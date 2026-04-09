CC = gcc
CFLAGS = -Wall -Wextra -g -I. -I./src

# Platform-specific flags
ifeq ($(OS),Windows_NT)
    LDFLAGS = -lpthread -lws2_32 -lole32
    CFLAGS += -D_WIN32
else
    LDFLAGS = -lpthread
endif

# OpenSSL support
OPENSSL_AVAILABLE := $(shell pkg-config --exists openssl 2>/dev/null && echo yes || echo no)
ifeq ($(OPENSSL_AVAILABLE), yes)
    CFLAGS += $(shell pkg-config --cflags openssl) -DHAVE_OPENSSL
    LDFLAGS += $(shell pkg-config --libs openssl)
else
    # Try homebrew OpenSSL paths (macOS)
    ifneq ($(wildcard /usr/local/opt/openssl@3/lib/libssl.*),)
        CFLAGS += -I/usr/local/opt/openssl@3/include -DHAVE_OPENSSL
        LDFLAGS += -L/usr/local/opt/openssl@3/lib -lssl -lcrypto
    else ifneq ($(wildcard /opt/homebrew/opt/openssl@3/lib/libssl.*),)
        CFLAGS += -I/opt/homebrew/opt/openssl@3/include -DHAVE_OPENSSL
        LDFLAGS += -L/opt/homebrew/opt/openssl@3/lib -lssl -lcrypto
    else ifneq ($(wildcard /usr/local/opt/openssl@1.1/lib/libssl.*),)
        CFLAGS += -I/usr/local/opt/openssl@1.1/include -DHAVE_OPENSSL
        LDFLAGS += -L/usr/local/opt/openssl@1.1/lib -lssl -lcrypto
    endif
endif
SRC_DIR = src
OBJ_DIR = obj
SKILL_DIR = skills

# Exclude old files in src/ that have been moved to subdirectories
SOURCES = $(filter-out $(SRC_DIR)/agent.c $(SRC_DIR)/channels.c $(SRC_DIR)/discord.c $(SRC_DIR)/telegram.c $(SRC_DIR)/websocket.c $(SRC_DIR)/gateway.c $(SRC_DIR)/cJSON.c $(SRC_DIR)/config.c $(SRC_DIR)/log.c $(SRC_DIR)/plugin.c $(SRC_DIR)/thread_pool.c, $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/*/*.c))
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))

# Skill sources (for building .so plugins)
SKILL_SOURCES = $(wildcard $(SRC_DIR)/tool/skill_*.c)
SKILL_TARGETS = $(patsubst $(SRC_DIR)/tool/skill_%.c, $(SKILL_DIR)/skill_%.so, $(SKILL_SOURCES))

TARGET = catclaw

# Check for curl availability
ifeq ($(OS),Windows_NT)
    # Windows with MSYS2
    LDFLAGS += -L/mingw64/lib -lcurl
    CFLAGS += -I/mingw64/include -DHAVE_CURL
else
    # Linux or other Unix-like systems
    # Check if curl is available
    CURL_AVAILABLE := $(shell command -v curl-config >/dev/null 2>&1 && echo yes || echo no)
    ifeq ($(CURL_AVAILABLE), yes)
        LDFLAGS += -lcurl
        CFLAGS += $(shell curl-config --cflags 2>/dev/null) -DHAVE_CURL
    else
        CFLAGS += -DNO_CURL
    endif
endif


all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

# Build skill plugins (.so)
build-skills: $(SKILL_TARGETS)

$(SKILL_DIR)/skill_%.so: $(SRC_DIR)/tool/skill_%.c
	@mkdir -p $(SKILL_DIR)
	$(CC) -shared -fPIC -o $@ $<

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)


run: $(TARGET)
	./$(TARGET)

gdb: $(TARGET)
	gdb -x  gdb.gdb ./catclaw

lldb: $(TARGET)
	lldb -s lldb.lldb ./catclaw

clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(SKILL_DIR)

.PHONY: all clean build-skills