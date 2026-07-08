# t32-asm top-level Makefile

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2

UNAME_S := $(shell uname -s 2>/dev/null)

ifeq ($(OS),Windows_NT)
    EXEEXT := .exe

    GNU_BIN ?= C:/Program Files/GNU/bin
    MKDIR_P ?= "$(GNU_BIN)/mkdir.exe" -p
    RM_RF   ?= "$(GNU_BIN)/rm.exe" -rf
    CP      ?= "$(GNU_BIN)/cp.exe" -f

    PREFIX ?= C:/Program Files/libvm
else
    EXEEXT :=
    MKDIR_P ?= mkdir -p
    RM_RF   ?= rm -rf
    CP      ?= cp -f

    PREFIX ?= /usr/local
endif

TARGET := t32-asm$(EXEEXT)

all: $(TARGET)

$(TARGET): t32-asm.c
	$(CC) $(CFLAGS) t32-asm.c -o $(TARGET)

install: $(TARGET)
	@$(MKDIR_P) "$(PREFIX)/bin"
	@$(CP) "$(TARGET)" "$(PREFIX)/bin/"

clean:
	@$(RM_RF) "$(TARGET)"

.PHONY: all install clean
