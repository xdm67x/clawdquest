# Clawd Quest

A small role-playing game written in C (C99) using SDL3 and OpenGL 3.2 core,
bundled as a native macOS app for Apple Silicon.

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
Mac without SDL3 installed.

## Releases

Releases are automated. Pushing a version tag triggers the
[`Release`](.github/workflows/release.yml) workflow, which builds the app on a
macOS arm64 runner, stamps the tag version into `Info.plist`, packages a DMG,
and publishes a GitHub Release:

```sh
git tag -a v1.0.1 -m "Clawd Quest 1.0.1"
git push origin v1.0.1
```

The DMG is attached to the release as `ClawdQuest-<version>-macOS-arm64.dmg`.

The app is ad-hoc signed, so Gatekeeper shows an "unidentified developer"
warning the first time it is opened on another Mac. Right-click the app and
choose **Open** to bypass it.

## Project layout

```
Makefile                 build, test, bundle, and asset-generation targets
src/                     game sources (C99)
tools/                   bundle metadata and asset generators
  Info.plist             macOS bundle metadata
  mkicon.swift           renders AppIcon.icns
  mkfont.c               renders src/font_atlas.h via freetype
  test_save.c            save-file round-trip tests (run by `make test`)
.github/workflows/       CI: automated releases
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for the commit message convention and
developer commands.
