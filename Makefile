CC = gcc
CFLAGS = -Wall -Wextra -g -I. -I./src

# Detect operating system
UNAME_S := $(shell uname -s)
OS_NAME := $(shell echo $(UNAME_S) | tr '[A-Z]' '[a-z]')

# Platform-specific flags
ifeq ($(findstring mingw32_nt,$(OS_NAME)),mingw32_nt)
    # MinGW32 environment
    LDFLAGS = -lpthread -lws2_32 -lole32 -luserenv
    CFLAGS += -D_WIN32 -DMINGW32
    
    # Use MinGW32 paths
    CFLAGS += -I$(MINGW_PREFIX)/include
    LDFLAGS += -L$(MINGW_PREFIX)/lib
else ifeq ($(findstring mingw64_nt,$(OS_NAME)),mingw64_nt)
    # MinGW64 environment
    LDFLAGS = -lpthread -lws2_32 -lole32 -luserenv
    CFLAGS += -D_WIN32 -DMINGW64
    
    # Use MinGW64 paths
    CFLAGS += -I/mingw64/include
    LDFLAGS += -L/mingw64/lib
else ifeq ($(findstring msys_nt,$(OS_NAME)),msys_nt)
    # MSYS2 environment
    LDFLAGS = -lpthread -lws2_32 -lole32 -luserenv
    CFLAGS += -D_WIN32 -DMSYS
    
    # Use MinGW64 paths
    CFLAGS += -I/mingw64/include
    LDFLAGS += -L/mingw64/lib
else
    # Linux or other Unix-like systems
    LDFLAGS = -lpthread
    CFLAGS += -DUNIX
endif

# OpenSSL support
ifeq ($(findstring mingw32_nt,$(OS_NAME)),mingw32_nt)
    # MinGW32 environment
    MINGW_OPENSSL_DIR := $(MINGW_PREFIX)/ssl
    ifneq ($(wildcard $(MINGW_OPENSSL_DIR)/lib/libssl.a),)
        CFLAGS += -I$(MINGW_OPENSSL_DIR)/include -DHAVE_OPENSSL
        LDFLAGS += -L$(MINGW_OPENSSL_DIR)/lib -lssl -lcrypto
    else
        CFLAGS += -DNO_OPENSSL
    endif
else ifeq ($(findstring mingw64_nt,$(OS_NAME)),mingw64_nt)
    # MinGW64 environment
    MINGW_OPENSSL_DIR := /mingw64/ssl
    ifneq ($(wildcard $(MINGW_OPENSSL_DIR)/lib/libssl.a),)
        CFLAGS += -I$(MINGW_OPENSSL_DIR)/include -DHAVE_OPENSSL
        LDFLAGS += -L$(MINGW_OPENSSL_DIR)/lib -lssl -lcrypto
    else
        CFLAGS += -DNO_OPENSSL
    endif
else ifeq ($(findstring msys_nt,$(OS_NAME)),msys_nt)
    # MSYS2 environment
    MINGW_OPENSSL_DIR := /mingw64/ssl
    ifneq ($(wildcard $(MINGW_OPENSSL_DIR)/lib/libssl.a),)
        CFLAGS += -I$(MINGW_OPENSSL_DIR)/include -DHAVE_OPENSSL
        LDFLAGS += -L$(MINGW_OPENSSL_DIR)/lib -lssl -lcrypto
    else
        CFLAGS += -DNO_OPENSSL
    endif
else
    # Linux or other Unix-like systems
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
        else
            CFLAGS += -DNO_OPENSSL
        endif
    endif
endif
SRC_DIR = src
OBJ_DIR = obj
SKILL_DIR = skills

# Exclude old files in src/ that have been moved to subdirectories
SOURCES = $(filter-out $(SRC_DIR)/agent.c $(SRC_DIR)/channels.c $(SRC_DIR)/discord.c $(SRC_DIR)/telegram.c $(SRC_DIR)/websocket.c $(SRC_DIR)/gateway.c $(SRC_DIR)/cJSON.c $(SRC_DIR)/config.c $(SRC_DIR)/log.c $(SRC_DIR)/plugin.c $(SRC_DIR)/thread_pool.c, $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/*/*.c))
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))

# Skill sources (for building plugins)
SKILL_SOURCES = $(wildcard $(SRC_DIR)/tool/skill_*.c)

# Set plugin extension based on platform
ifeq ($(findstring mingw32_nt,$(OS_NAME)),mingw32_nt)
    SKILL_EXT := .dll
else ifeq ($(findstring mingw64_nt,$(OS_NAME)),mingw64_nt)
    SKILL_EXT := .dll
else ifeq ($(findstring msys_nt,$(OS_NAME)),msys_nt)
    SKILL_EXT := .dll
else
    SKILL_EXT := .so
endif

SKILL_TARGETS = $(patsubst $(SRC_DIR)/tool/skill_%.c, $(SKILL_DIR)/skill_%.$(SKILL_EXT), $(SKILL_SOURCES))

TARGET = catclaw

# Set target extension for Windows
ifeq ($(findstring mingw32_nt,$(OS_NAME)),mingw32_nt)
    TARGET := $(TARGET).exe
else ifeq ($(findstring mingw64_nt,$(OS_NAME)),mingw64_nt)
    TARGET := $(TARGET).exe
else ifeq ($(findstring msys_nt,$(OS_NAME)),msys_nt)
    TARGET := $(TARGET).exe
endif

# Check for curl availability
ifeq ($(findstring mingw32_nt,$(OS_NAME)),mingw32_nt)
    # MinGW32 environment
    MINGW_CURL_DIR := $(MINGW_PREFIX)
    ifneq ($(wildcard $(MINGW_CURL_DIR)/lib/libcurl.a),)
        CFLAGS += -I$(MINGW_CURL_DIR)/include -DHAVE_CURL
        LDFLAGS += -L$(MINGW_CURL_DIR)/lib -lcurl
    else
        CFLAGS += -DNO_CURL
    endif
else ifeq ($(findstring mingw64_nt,$(OS_NAME)),mingw64_nt)
    # MinGW64 environment
    MINGW_CURL_DIR := /mingw64
    ifneq ($(wildcard $(MINGW_CURL_DIR)/lib/libcurl.a),)
        CFLAGS += -I$(MINGW_CURL_DIR)/include -DHAVE_CURL
        LDFLAGS += -L$(MINGW_CURL_DIR)/lib -lcurl
    else
        CFLAGS += -DNO_CURL
    endif
else ifeq ($(findstring msys_nt,$(OS_NAME)),msys_nt)
    # MSYS2 environment
    MINGW_CURL_DIR := /mingw64
    ifneq ($(wildcard $(MINGW_CURL_DIR)/lib/libcurl.a),)
        CFLAGS += -I$(MINGW_CURL_DIR)/include -DHAVE_CURL
        LDFLAGS += -L$(MINGW_CURL_DIR)/lib -lcurl
    else
        CFLAGS += -DNO_CURL
    endif
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

$(SKILL_DIR)/skill_%.$(SKILL_EXT): $(SRC_DIR)/tool/skill_%.c
	@mkdir -p $(SKILL_DIR)
ifeq ($(findstring mingw32_nt,$(OS_NAME)),mingw32_nt)
	$(CC) -shared -o $@ $< $(CFLAGS) $(LDFLAGS)
else ifeq ($(findstring mingw64_nt,$(OS_NAME)),mingw64_nt)
	$(CC) -shared -o $@ $< $(CFLAGS) $(LDFLAGS)
else ifeq ($(findstring msys_nt,$(OS_NAME)),msys_nt)
	$(CC) -shared -o $@ $< $(CFLAGS) $(LDFLAGS)
else
	$(CC) -shared -fPIC -o $@ $<
endif

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)


run: $(TARGET)
	./$(TARGET)

gdb: $(TARGET)
ifeq ($(findstring mingw32_nt,$(OS_NAME)),mingw32_nt)
	gdb -x  gdb.gdb ./catclaw.exe
else ifeq ($(findstring mingw64_nt,$(OS_NAME)),mingw64_nt)
	gdb -x  gdb.gdb ./catclaw.exe
else ifeq ($(findstring msys_nt,$(OS_NAME)),msys_nt)
	gdb -x  gdb.gdb ./catclaw.exe
else
	gdb -x  gdb.gdb ./catclaw
endif

lldb: $(TARGET)
ifeq ($(findstring mingw32_nt,$(OS_NAME)),mingw32_nt)
	lldb -s lldb.lldb ./catclaw.exe
else ifeq ($(findstring mingw64_nt,$(OS_NAME)),mingw64_nt)
	lldb -s lldb.lldb ./catclaw.exe
else ifeq ($(findstring msys_nt,$(OS_NAME)),msys_nt)
	lldb -s lldb.lldb ./catclaw.exe
else
	lldb -s lldb.lldb ./catclaw
endif

clean:
ifeq ($(findstring mingw32_nt,$(OS_NAME)),mingw32_nt)
	rm -rf $(OBJ_DIR) $(SKILL_DIR)
	rm -f catclaw.exe
else ifeq ($(findstring mingw64_nt,$(OS_NAME)),mingw64_nt)
	rm -rf $(OBJ_DIR) $(SKILL_DIR)
	rm -f catclaw.exe
else ifeq ($(findstring msys_nt,$(OS_NAME)),msys_nt)
	rm -rf $(OBJ_DIR) $(SKILL_DIR)
	rm -f catclaw.exe
else
	rm -rf $(OBJ_DIR) $(SKILL_DIR)
	rm -f catclaw
endif

.PHONY: all clean build-skills