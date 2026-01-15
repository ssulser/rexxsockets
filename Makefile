# -------- detect regina-config ----------
REGINA_CONFIG ?= regina-config

# Fail early if regina-config is missing
ifeq ($(shell command -v $(REGINA_CONFIG) >/dev/null 2>&1; echo $$?),1)
  $(error regina-config not found. Please install Regina Rexx development tools and ensure regina-config is in PATH.)
endif

# Ask regina-config for addon dir (must be non-empty)
ADDONSDIR := $(strip $(shell $(REGINA_CONFIG) --addons 2>/dev/null))
ifeq ($(ADDONSDIR),)
  $(error regina-config --addons returned empty. Please install Regina Rexx dev tools correctly.)
endif

# -------- toolchain ----------
CC      ?= cc
INSTALL ?= install

CFLAGS  ?= -O2 -Wall -Wextra -fPIC $(shell $(REGINA_CONFIG) --cflags)
LDFLAGS ?= $(shell $(REGINA_CONFIG) --libs)

# -------- project ----------
SRC     := rexxsockets.c
OBJ     := rexxsockets.o

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  LIBFILE := librexxsockets.dylib
  SHLIB_LDFLAGS := -dynamiclib
else
  LIBFILE := librexxsockets.so
  SHLIB_LDFLAGS := -shared
endif

INSTALL_TO := $(ADDONSDIR)

.PHONY: all install uninstall print-dirs clean

all: $(LIBFILE)

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -c -o $@ $<

$(LIBFILE): $(OBJ)
	$(CC) $(SHLIB_LDFLAGS) -o $@ $^ $(LDFLAGS)

print-dirs:
	@echo "REGINA_CONFIG = $(REGINA_CONFIG)"
	@echo "ADDONSDIR     = $(ADDONSDIR)"
	@echo "INSTALL_TO    = $(INSTALL_TO)"
	@echo "LIBFILE       = $(LIBFILE)"

install: all
	@echo "Installing $(LIBFILE) to: $(INSTALL_TO)"
	$(INSTALL) -d "$(INSTALL_TO)"
	$(INSTALL) -m 755 "$(LIBFILE)" "$(INSTALL_TO)/$(LIBFILE)"

uninstall:
	@echo "Removing: $(INSTALL_TO)/$(LIBFILE)"
	rm -f "$(INSTALL_TO)/$(LIBFILE)"

clean:
	rm -f *.o *.dylib *.so
