BUILD_DIR := build
EXECUTABLES = cloxx cloxxd

default: cloxx

# Compile the C interpreter.
cloxx:
	@ $(MAKE) -f c.make NAME=cloxx MODE=release SOURCE_DIR=src
	@ cp build/cloxx cloxx # For convenience, copy the interpreter to the top level.

# Compile the C interpreter.
debug:
	@ $(MAKE) -f c.make NAME=cloxxd MODE=debug SOURCE_DIR=src
	@ cp build/cloxxd cloxxd # For convenience, copy the interpreter to the top level.

# Remove all build outputs and intermediate files.
clean:
	@echo "Cleaning up..."
	@rm -rf $(BUILD_DIR)
	@for file in $(EXECUTABLES); do \
        if [ -e $$file ]; then rm $$file; fi; \
    done
	@echo "Done."