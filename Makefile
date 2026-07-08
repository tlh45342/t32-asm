# t32-asm top-level Makefile 

#
CC      := gcc

# GNU coreutils shim (matches libvm style). Override if needed.
GNU_BIN ?= C:/Program Files/GNU/bin
MKDIR_P ?= "$(GNU_BIN)/mkdir.exe" -p
RM_RF   ?= "$(GNU_BIN)/rm.exe" -rf
CP      ?= "$(GNU_BIN)/cp.exe" -f

BIN_DIR   := bin
BUILD_DIR := build

T32ASM := t32-asm.exe

all: $(T32ASM)

# Install prefix (override: make PREFIX="C:/Program Files/libvm" install)
PREFIX ?= C:/Program Files/libvm

$(T32ASM): t32-asm.c
	$(CC) t32-asm.c -o t32-asm.exe
	
install: $(T32ASM)
	@$(MKDIR_P) "$(PREFIX)/bin"
	@$(CP) "$(T32ASM)" "$(PREFIX)/bin/"