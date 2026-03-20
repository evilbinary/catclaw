CC = gcc-13
CFLAGS = -Wall -Wextra -O2 -I. -I./src

# Platform-specific flags
ifeq ($(OS),Windows_NT)
    LDFLAGS = -lpthread -lws2_32
else
    LDFLAGS = -lpthread
endif
SRC_DIR = src
OBJ_DIR = obj

# Exclude old files in src/ that have been moved to subdirectories
SOURCES = $(filter-out $(SRC_DIR)/skill_weather.c $(SRC_DIR)/agent.c $(SRC_DIR)/channels.c $(SRC_DIR)/discord.c $(SRC_DIR)/telegram.c $(SRC_DIR)/websocket.c $(SRC_DIR)/gateway.c $(SRC_DIR)/ai_model.c $(SRC_DIR)/cJSON.c $(SRC_DIR)/config.c $(SRC_DIR)/log.c $(SRC_DIR)/plugin.c $(SRC_DIR)/thread_pool.c, $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/*/*.c))
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))

TARGET = catclaw

# Check for curl availability
ifeq ($(OS),Windows_NT)
    # Windows with MSYS2
    LDFLAGS += -L/E/soft/msys2/mingw64/lib -lcurl
    CFLAGS += -I/E/soft/msys2/mingw64/include -DHAVE_CURL
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

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

gdb: $(TARGET)
	gdb -x  gdb.gdb ./catclaw

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean
