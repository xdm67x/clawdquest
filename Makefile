# Quest Log — C (SDL3 + OpenGL 3.2 core)
#
# make        build build/clawdquest
# make app    bundle dist/ClawdQuest.app (macOS)
# make font   regenerate src/font_atlas.h (needs freetype)
# make icon   regenerate build/AppIcon.icns (needs swift)
# make clean

CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra
BUILD   := build

SRC := src/main.c src/audio.c src/font.c src/game.c src/json.c src/scene.c src/ui.c
OBJ := $(SRC:%.c=$(BUILD)/%.o)

# Homebrew paths when pkg-config is unavailable (macOS default here).
SDL3_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null || echo -I/opt/homebrew/include)
SDL3_LIBS   := $(shell pkg-config --libs sdl3 2>/dev/null || echo -L/opt/homebrew/lib -lSDL3)

UNAME := $(shell uname)
ifeq ($(UNAME),Darwin)
  GL_LIBS := -framework OpenGL
  # Leave room in the Mach-O header so install_name_tool can patch load
  # commands when bundling SDL3 into the .app (see `make app`).
  LDFLAGS += -Wl,-headerpad_max_install_names
else
  GL_LIBS := -lGL
endif

all: $(BUILD)/clawdquest

$(BUILD)/clawdquest: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(SDL3_LIBS) $(GL_LIBS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SDL3_CFLAGS) -c -o $@ $<

test: $(BUILD)/test_save
	$(BUILD)/test_save

$(BUILD)/test_save: tools/test_save.c src/game.c src/json.c src/game.h src/json.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SDL3_CFLAGS) -Isrc -o $@ tools/test_save.c src/game.c src/json.c $(SDL3_LIBS)

app: all $(BUILD)/AppIcon.icns
	@rm -rf $(APP)
	@mkdir -p $(dir $(APP_BIN)) $(dir $(APP_LIB)) $(APP)/Contents/Resources
	cp $(BUILD)/clawdquest $(APP_BIN)
	cp $(BUILD)/AppIcon.icns $(APP)/Contents/Resources/AppIcon.icns
	cp $(SDL3_DYLIB) $(APP_LIB)
	install_name_tool -id @rpath/libSDL3.0.dylib $(APP_LIB)
	install_name_tool -add_rpath @executable_path/../Frameworks $(APP_BIN)
	install_name_tool -change $(SDL3_DYLIB) @rpath/libSDL3.0.dylib $(APP_BIN)
	cp tools/Info.plist $(APP)/Contents/Info.plist
	codesign --force --sign - $(APP_LIB)
	codesign --force --sign - $(APP)
	@echo "Built $(APP)"

# Homebrew's install id for libSDL3 — the path the linker records in the binary.
SDL3_DYLIB := /opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib
APP     := dist/ClawdQuest.app
APP_BIN := $(APP)/Contents/MacOS/clawdquest
APP_LIB := $(APP)/Contents/Frameworks/libSDL3.0.dylib

$(BUILD)/AppIcon.icns: tools/mkicon.swift
	@mkdir -p $(dir $@)
	swift $< $(BUILD)/AppIcon.iconset
	iconutil -c icns $(BUILD)/AppIcon.iconset -o $@

font:
	$(CC) -std=c99 -O2 -Wall -o $(BUILD)/mkfont tools/mkfont.c \
	  -I/opt/homebrew/include/freetype2 -L/opt/homebrew/opt/freetype/lib -lfreetype
	$(BUILD)/mkfont /System/Library/Fonts/Helvetica.ttc src/font_atlas.h

clean:
	rm -rf $(BUILD)

.PHONY: all app font test clean
