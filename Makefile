BUILD_DIR := build
EXECUTABLES = cloxx cloxxd

TOOL_SOURCES := tool/pubspec.lock $(shell find tool -name '*.dart')
TEST_SNAPSHOT := $(BUILD_DIR)/test.dart.snapshot

default: cloxx

# Compile the C interpreter.
cloxx:
	@ $(MAKE) -f c.make NAME=cloxx MODE=release SOURCE_DIR=src 
	@ cp build/cloxx cloxx # For convenience, copy the interpreter to the top level.

# Compile the C interpreter. We set SNIPPET=true, allowing unused-functions.
debug:
	@ $(MAKE) -f c.make NAME=cloxxd MODE=debug SOURCE_DIR=src SNIPPET=true
	@ cp build/cloxxd cloxxd # For convenience, copy the interpreter to the top level.

# Remove all build outputs and intermediate files.
clean:
	@echo "Cleaning up..."
	@rm -rf $(BUILD_DIR)
	@for file in $(EXECUTABLES); do \
        if [ -e $$file ]; then rm $$file; fi; \
    done
	@echo "Done."

# Run the c tests against the cloxxd interpreter
test: debug $(TEST_SNAPSHOT)
	@ dart $(TEST_SNAPSHOT) c cloxxd

$(TEST_SNAPSHOT): $(TOOL_SOURCES)
	@ mkdir -p build
	@ echo "Compiling Dart snapshot..."
	@ dart --snapshot=$@ --snapshot-kind=app-jit tool/bin/test.dart c cloxxd >/dev/null