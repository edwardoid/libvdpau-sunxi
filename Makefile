TARGET = libvdpau_fbturbo.so.1
SRC:= $(wildcard *.c)
CC = gcc
CFLAGS ?= -Wall -O3
LDFLAGS ?=


MAKEFLAGS += -rR --no-print-directory

DEP_CFLAGS = -MD -MP -MQ $@
LIB_CFLAGS = -fpic
LIB_LDFLAGS = -shared -Wl,-soname,$(TARGET)

OBJ = $(addsuffix .o,$(basename $(SRC)))
DEP = $(addsuffix .d,$(basename $(SRC)))

MODULEDIR = $(shell pkg-config --variable=moduledir vdpau)

ifeq ($(MODULEDIR),)
MODULEDIR=/usr/lib/vdpau
endif

.PHONY: clean all install

all: $(TARGET)
$(TARGET): $(OBJ)
	$(CC) $(LIB_LDFLAGS) $(LDFLAGS) $(OBJ) -o $@

clean:
	rm -f $(OBJ)
	rm -f $(DEP)
	rm -f $(TARGET)

install: $(TARGET)
	install -D $(TARGET) $(DESTDIR)$(MODULEDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(MODULEDIR)/$(TARGET)

%.o: %.c
	$(CC) $(DEP_CFLAGS) $(LIB_CFLAGS) $(CFLAGS) -c $< -o $@

include $(wildcard $(DEP))
