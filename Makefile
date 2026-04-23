# ── Configuration ─────────────────────────────────────────────
CC        := clang
PROJECT   := PD_KD
BIN_DIR   := bin
OBJ_DIR   := obj

# Choose frontend: ncurses or raylib (default is ncurses)
FRONTEND ?= ncurses

# Core logic files that are ALWAYS compiled
#CORE_SOURCES := main.c network.c
CORE_SOURCES := main.c

# The specific frontend file
FE_SOURCE    := frontend_$(FRONTEND).c

# Combine them for the build
SOURCES      := $(CORE_SOURCES) $(FE_SOURCE)
OBJECTS      := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SOURCES))

# ── Compiler Flags ────────────────────────────────────────────
CFLAGS_COMMON  := -std=c17 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -D_POSIX_C_SOURCE=200809L
CFLAGS_RELEASE := $(CFLAGS_COMMON) -O2
CFLAGS_DEBUG   := $(CFLAGS_COMMON) -g -O0 -fsanitize=address,undefined
LDFLAGS        :=

# Dynamic Library Linking based on the selected frontend
ifeq ($(FRONTEND),ncurses)
    LDLIBS := -lncurses
else ifeq ($(FRONTEND),raylib)
    LDLIBS := -lraylib -lm -lpthread -ldl -lrt -lX11
endif

# Default to release
CFLAGS := $(CFLAGS_RELEASE)

# ── Targets (DO NOT DELETE THESE) ─────────────────────────────
.PHONY: all run debug debug-build clean

all: $(BIN_DIR)/$(PROJECT)
	@echo "✓ Build successful [$(FRONTEND)]: $(BIN_DIR)/$(PROJECT)"

# Link the executable
$(BIN_DIR)/$(PROJECT): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

# Compile object files
$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Create directories
$(BIN_DIR) $(OBJ_DIR):
	mkdir -p $@

# Build + Run
run: all
	@./$(BIN_DIR)/$(PROJECT)

# Debug build
debug-build: CFLAGS := $(CFLAGS_DEBUG)
debug-build: clean $(BIN_DIR)/$(PROJECT)
	@echo "✓ Debug build successful: $(BIN_DIR)/$(PROJECT)"

# Debug build + Run
debug: CFLAGS := $(CFLAGS_DEBUG)
debug: clean $(BIN_DIR)/$(PROJECT)
	@./$(BIN_DIR)/$(PROJECT)

clean:
	rm -rf $(BIN_DIR) $(OBJ_DIR)
