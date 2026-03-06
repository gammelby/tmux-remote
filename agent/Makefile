BUILD_ARCH    := $(shell uname -m | sed -e 's/aarch64/arm64/' -e 's/x86_64/x64/')
BUILD_OS      := $(shell uname -s | tr '[:upper:]' '[:lower:]' | sed 's/darwin/osx/')
BUILD_TRIPLET := $(BUILD_ARCH)-$(BUILD_OS)
BUILD_DIR     := _build/$(BUILD_TRIPLET)
CMAKE_FLAGS   ?=

.PHONY: all clean configure

all:
	cmake -B $(BUILD_DIR) -G Ninja $(CMAKE_FLAGS)
	cmake --build $(BUILD_DIR)

configure:
	cmake -B $(BUILD_DIR) -G Ninja $(CMAKE_FLAGS)

clean:
	rm -rf $(BUILD_DIR)
