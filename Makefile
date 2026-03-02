CC = gcc
CFLAGS = -Wall -Wextra -O2

# Platform-specific flags
ifeq ($(OS),Windows_NT)
    LDFLAGS = -lpthread -lws2_32
else
    LDFLAGS = -lpthread
endif
SRC_DIR = src
OBJ_DIR = obj

SOURCES = $(filter-out $(SRC_DIR)/skill_weather.c, $(wildcard $(SRC_DIR)/*.c))
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))

TARGET = catclaw

# Check if curl is available
CURL_AVAILABLE := $(shell $(CC) -lcurl -o NUL 2>&1 && echo yes || echo no)

ifeq ($(CURL_AVAILABLE), yes)
	LDFLAGS += -lcurl
	CFLAGS += -DHAVE_CURL
else
	CFLAGS += -DNO_CURL
endif

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean
