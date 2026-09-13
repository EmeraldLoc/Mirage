DIRECTORIES := src
TARGET := CoopFakeLobby
BUILD_DIR := build
CXX := clang++
ASAN := 0
CXXFLAGS := -O3 -Iinclude -g
LDFLAGS := -lz

ifeq ($(ASAN),1)
	CXXFLAGS += -g -fsanitize=address -fsanitize=undefined
	LDFLAGS += -fsanitize=address -fsanitize=undefined
endif

ifeq ($(OS),Windows_NT)
    LDFLAGS += -lws2_32 -static
endif

SOURCES := $(wildcard $(addsuffix /*.cpp,$(DIRECTORIES)))
OBJECTS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SOURCES))

all: $(BUILD_DIR) $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean