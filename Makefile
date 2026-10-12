# Quest Log — C (SDL3 + OpenGL 3.2 core)
#
# make        build build/questlog
# make font   regenerate src/font_atlas.h (needs freetype)
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
else
  GL_LIBS := -lGL
endif

all: $(BUILD)/questlog

$(BUILD)/questlog: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(SDL3_LIBS) $(GL_LIBS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SDL3_CFLAGS) -c -o $@ $<

test: $(BUILD)/test_save
	$(BUILD)/test_save

$(BUILD)/test_save: tools/test_save.c src/game.c src/json.c src/game.h src/json.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SDL3_CFLAGS) -Isrc -o $@ tools/test_save.c src/game.c src/json.c $(SDL3_LIBS)

font:
	$(CC) -std=c99 -O2 -Wall -o $(BUILD)/mkfont tools/mkfont.c \
	  -I/opt/homebrew/include/freetype2 -L/opt/homebrew/opt/freetype/lib -lfreetype
	$(BUILD)/mkfont /System/Library/Fonts/Helvetica.ttc src/font_atlas.h

clean:
	rm -rf $(BUILD)

.PHONY: all font test clean
