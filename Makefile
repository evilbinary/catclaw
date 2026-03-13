CC = gcc
CFLAGS = -Wall -Wextra -O2 -I. -I./src

# Platform-specific flags
ifeq ($(OS),Windows_NT)
    LDFLAGS = -lpthread -lws2_32
else
    LDFLAGS = -lpthread
endif
SRC_DIR = src
OBJ_DIR = obj

SOURCES = $(filter-out $(SRC_DIR)/skill_weather.c, $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/*/*.c))
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
    CURL_AVAILABLE := $(shell $(CC) -lcurl -o /dev/null 2>&1 && echo yes || echo no)
    ifeq ($(CURL_AVAILABLE), yes)
        LDFLAGS += -lcurl
        CFLAGS += -DHAVE_CURL
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
