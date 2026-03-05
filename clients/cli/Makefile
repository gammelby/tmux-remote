BUILD_TRIPLET := $(shell uname -m)-$(shell uname -s | tr '[:upper:]' '[:lower:]')
BUILD_DIR     := _build/$(BUILD_TRIPLET)

.PHONY: all clean configure

all:
	cmake -B $(BUILD_DIR) -G Ninja
	cmake --build $(BUILD_DIR)

configure:
	cmake -B $(BUILD_DIR) -G Ninja

clean:
	rm -rf $(BUILD_DIR)
