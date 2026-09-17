DIRECTORIES := src
TARGET := Mirage
BUILD_DIR := build
CXX := clang++
ASAN := 0
CXXFLAGS := -O3 -Iinclude -std=c++23
LDFLAGS := -lz

WINDOWS_BUILD := 0
LINUX_BUILD := 0
OSX_BUILD := 0

ARM_BUILD := 0

ifeq ($(ASAN),1)
	CXXFLAGS += -g -fsanitize=address -fsanitize=undefined
	LDFLAGS += -fsanitize=address -fsanitize=undefined
endif

# get platform
ifeq ($(OS),Windows_NT)
	WINDOWS_BUILD := 1
else ifeq ($(shell uname -s),Darwin)
	OSX_BUILD := 1
	ifeq ($(shell uname -m),arm64)
		ARM_BUILD := 1
	endif
else
	LINUX_BUILD := 1
endif

COOPNET_LIB :=

# setup linker flags
ifeq ($(WINDOWS_BUILD),1)
	CXX := g++ # use g++ for for the c++ compiler by default
	LDFLAGS += -Llib/win64 -lcoopnet -ljuice -lws2_32 -liphlpapi -lbcrypt -static
else ifeq ($(OSX_BUILD),1)
	ifeq ($(ARM_BUILD),1)
		LDFLAGS += -Llib/mac_arm -lcoopnet -Wl,-rpath,@executable_path
		COOPNET_LIB += ./lib/mac_arm/libcoopnet.dylib
		COOPNET_LIB += ./lib/mac_arm/libjuice.1.6.2.dylib
	else
		LDFLAGS += -Llib/mac_intel -lcoopnet -Wl,-rpath,@executable_path
		COOPNET_LIB += ./lib/mac_intel/libcoopnet.dylib
		COOPNET_LIB += ./lib/mac_intel/libjuice.1.6.2.dylib
	endif
else
	LDFLAGS += -Llib/linux -lcoopnet -ljuice
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
	cp $(COOPNET_LIB) $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all sign clean
