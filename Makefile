#
# xrick/Makefile — Linux build
#
# Builds the game into ./build (binary + data).
# Uses SDL 1.2 API (provided by sdl12-compat on modern systems).
#

BUILD   := build
OBJDIR  := $(BUILD)/obj
SRCDIR  := src

CC      := gcc
CFLAGS  := -O2 -Wall -Iinclude -MMD -MP $(shell sdl-config --cflags)
LDFLAGS := $(shell sdl-config --libs) -lz

SOURCES := $(wildcard $(SRCDIR)/*.c)
OBJECTS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))
DEPS    := $(OBJECTS:.o=.d)

.PHONY: all clean run

all: $(BUILD)/xrick $(BUILD)/data.zip

$(BUILD):
	mkdir -p $(BUILD)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/sysvid.o: $(SRCDIR)/sysvid.c $(SRCDIR)/sysvid_crt.e | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/xrick: $(OBJECTS) | $(BUILD)
	$(CC) -o $@ $^ $(LDFLAGS)

-include $(DEPS)

$(BUILD)/data.zip: data.zip | $(BUILD)
	cp $< $@

run: all
	cd $(BUILD) && ./xrick

clean:
	rm -rf $(BUILD)
