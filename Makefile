# -------- detect regina-config ----------
REGINA_CONFIG ?= regina-config

CC      ?= cc
CFLAGS  ?= -O2 -fPIC $(shell $(REGINA_CONFIG) --cflags)
LDFLAGS ?= $(shell $(REGINA_CONFIG) --libs)
INSTALL ?= install

# Deine Library
LIBNAME := librexxsockets
DYLIB   := $(LIBNAME).dylib

# Addons-Verzeichnis über regina-config (Homebrew oder native install)
# Falls regina-config fehlt oder --addons leer ist: Fallback
ADDONSDIR := $(shell \
  if command -v $(REGINA_CONFIG) >/dev/null 2>&1; then \
    d=`$(REGINA_CONFIG) --addons 2>/dev/null`; \
    if [ -n "$$d" ]; then echo "$$d"; fi; \
  fi)

# Fallback (wenn wirklich nichts gefunden)
# -> hier kannst du deinen “default” setzen, oder hart abbrechen lassen
ifeq ($(strip $(ADDONSDIR)),)
  $(error "regina-config not found or --addons returned empty. Please install Regina REXX dev tools.")
endif

# Zielpfad
INSTALL_ADDONSDIR := $(ADDONSDIR)

.PHONY: all install uninstall print-dirs clean

all: $(DYLIB)

# Beispiel-Buildregel (bitte an dein Projekt anpassen)
$(DYLIB): rexxsockets.o
	$(CC) -dynamiclib -o $@ $^ $(LDFLAGS)

rexxsockets.o: rexxsockets.c
	$(CC) $(CFLAGS) -c -o $@ $<

print-dirs:
	@echo "REGINA_CONFIG = $(REGINA_CONFIG)"
	@echo "ADDONSDIR     = $(ADDONSDIR)"
	@echo "INSTALL_TO    = $(INSTALL_ADDONSDIR)"

install: all
	@echo "Installing $(DYLIB) to: $(INSTALL_ADDONSDIR)"
	$(INSTALL) -d "$(INSTALL_ADDONSDIR)"
	$(INSTALL) -m 755 "$(DYLIB)" "$(INSTALL_ADDONSDIR)/$(DYLIB)"
	# ln -sf "$(DYLIB)" "$(INSTALL_ADDONSDIR)/$(LIBNAME).so"

uninstall:
	@echo "Removing: $(INSTALL_ADDONSDIR)/$(DYLIB)"
	rm -f "$(INSTALL_ADDONSDIR)/$(DYLIB)"

clean:
	rm *.dylib *.a *.o
