# t32-asm Makefile
#
# Windows:
#   make
#   make test
#   make install
#
# Linux/macOS:
#   make
#   make test
#   make install
#
# Default install locations:
#   Windows: C:/Program Files/libvm/bin
#   Linux:   ~/.local/bin
#   macOS:   ~/.local/bin

PROJECT := t32-asm
SRC_DIR := src
BUILD_DIR := build

ifeq ($(OS),Windows_NT)
	CC := gcc
	EXEEXT := .exe

	GNU_BIN ?= C:/Program Files/GNU/bin
	MKDIR_P ?= "$(GNU_BIN)/mkdir.exe" -p
	RM_RF ?= "$(GNU_BIN)/rm.exe" -rf
	CP ?= "$(GNU_BIN)/cp.exe" -f

	PREFIX ?= C:/Program Files/libvm
	PYTHON ?= python
else
	CC ?= cc
	EXEEXT :=

	MKDIR_P ?= mkdir -p
	RM_RF ?= rm -rf
	CP ?= cp -f

	PREFIX ?= $(HOME)/.local
	PYTHON ?= python3
endif

CFLAGS ?= -Wall -Wextra -Wpedantic -O2

TARGET := $(PROJECT)$(EXEEXT)
OBJECT := $(BUILD_DIR)/t32-asm.o

all: $(TARGET)

$(TARGET): $(OBJECT)
	$(CC) $(OBJECT) -o $(TARGET)

$(OBJECT): $(SRC_DIR)/t32-asm.c
	@$(MKDIR_P) "$(BUILD_DIR)"
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	$(PYTHON) tests/run_tests.py

install: $(TARGET)
	@$(MKDIR_P) "$(PREFIX)/bin"
	@$(CP) "$(TARGET)" "$(PREFIX)/bin/$(TARGET)"
	@echo "Installed $(TARGET) to $(PREFIX)/bin"

uninstall:
	@$(RM_RF) "$(PREFIX)/bin/$(TARGET)"

clean:
	@$(RM_RF) "$(BUILD_DIR)"
	@$(RM_RF) "$(TARGET)"
	@$(RM_RF) "tests/out"

rebuild: clean all

.PHONY: all test install uninstall clean rebuild
