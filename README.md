# Clawd Pet

Clawd is a desktop pet: the little pixel-art guy from Clawd Quest now lives
directly on your macOS desktop, in a small transparent, borderless,
always-on-top window written in C (C99) using SDL3 and OpenGL 3.2 core.

- **Drag him anywhere** — grab him and the window follows the cursor.
- **Click him** to open the quest card above him: level, XP, streak stats,
  the quest list, and an add bar.
- Click a quest to run its chronometer (Clawd gets to work), check it off to feed
  him an apple, `x` to delete it.
- While a chronometer runs he works; left alone for five minutes he dozes
  off (zZz); plop him back down and he wakes up dizzy.
- He remembers his spot on the desktop between runs (`~/.clawdquest/pet.pos`)
  and your quests and XP (`~/.clawdquest/store.json`).

## Requirements

- macOS with Xcode command line tools (`xcode-select --install`)
- [Homebrew](https://brew.sh)
- SDL3: `brew install sdl3`
- Optional: `freetype` (rebuild the font atlas), `swift` (rebuild the app icon)

## Build

```sh
make        # build build/clawdquest and run it with ./build/clawdquest
make app    # bundle dist/ClawdQuest.app (macOS)
make test   # build and run the save-file test suite
make clean  # remove build artifacts
```

`make app` bundles the binary, the app icon, and Homebrew's `libSDL3.0.dylib`
into `dist/ClawdQuest.app` and ad-hoc codesigns it, so the app runs on any
Mac without SDL3 installed. The bundle sets `LSUIElement`, so Clawd sits on
the desktop without a Dock icon or menu bar.

## Releases

Releases are automated. Pushing a version tag triggers the
[`Release`](.github/workflows/release.yml) workflow, which builds the app on a
macOS arm64 runner, stamps the tag version into `Info.plist`, packages a DMG,
and publishes a GitHub Release:

```sh
git tag -a v1.0.1 -m "Clawd Pet 1.0.1"
git push origin v1.0.1
```

The DMG is attached to the release as `ClawdQuest-<version>-macOS-arm64.dmg`.

The app is ad-hoc signed, so Gatekeeper shows an "unidentified developer"
warning the first time it is opened on another Mac. Right-click the app and
choose **Open** to bypass it.

## Project layout

```
Makefile                 build, test, bundle, and asset-generation targets
src/                     pet sources (C99)
  pet.c/.h               Clawd: 2D pixel variants, shadow, sparks, apple, zZz
  ui.c/.h                the quest card + pops/toasts/confetti
  game.c/.h              quest rules, XP, persistence
  main.c                 window, drag-to-move, panel toggle
tools/                   bundle metadata and asset generators
  Info.plist             macOS bundle metadata
  mkicon.swift           renders AppIcon.icns
  mkfont.c               renders src/font_atlas.h via freetype
  test_save.c            save-file round-trip tests (run by `make test`)
.github/workflows/       CI: automated releases
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for the commit message convention and
developer commands.
