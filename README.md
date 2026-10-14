# Clawd Pet

Clawd is a desktop pet to not procrastinate and do the work. The little mascot
guy from Claude Code now lives directly on your macOS desktop, in a small
transparent, borderless, always-on-top window written in Rust using winit
(windowing) and wgpu (graphics — Metal on macOS).

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

- macOS with a Rust toolchain (`rustup`; https://rustup.rs)

No system graphics or audio libraries are needed: wgpu drives Metal and cpal
drives CoreAudio, both linked directly by Cargo.

## Build

```sh
cargo build        # debug build -> target/debug/clawdquest
cargo run          # build and launch
cargo test         # run the save-file test suite
cargo xtask app    # bundle dist/ClawdQuest.app (macOS)
cargo xtask dmg    # bundle + ClawdQuest-<version>-macOS-arm64.dmg
cargo clean        # remove build artifacts
```

`cargo xtask app` builds the release binary, bundles it (with the app icon and
`Info.plist`) into `dist/ClawdQuest.app`, and ad-hoc codesigns it. The bundle
sets `LSUIElement`, so Clawd sits on the desktop without a Dock icon or menu
bar. `cargo xtask` is a small Rust helper in `xtask/`.

## Releases

Releases are automated. Pushing a version tag triggers the
[`Release`](.github/workflows/release.yml) workflow, which builds the app on a
macOS arm64 runner, stamps the tag version into the bundle, packages a DMG, and
publishes a GitHub Release:

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
Cargo.toml               dependencies, release profile, and the workspace
xtask/                   the Rust build helper (`cargo xtask app|dmg`)
assets/AppIcon.icns      the app-bundle icon
src/                     the app sources
  main.rs                window, drag-to-move, panel toggle, event loop
  gfx.rs                 the wgpu renderer (pipelines, atlases, draw replay)
  shaders/               WGSL: sprite quads, flat color, SDF rounded rects
  pet.rs                 Clawd: mood sprite, shadow, sparks, apple, zZz
  ui.rs                  the quest card + pops/toasts/confetti
  font.rs                atlas text rendering (src/assets/font.*)
  game.rs                quest rules, XP, persistence (+ save-file tests)
  audio.rs               beep synth via cpal
  fx.rs                  the effects game rules trigger on the UI/pet
  assets/                embedded base64 atlases (font + sprite)
icons/                   source SVGs for the mood-icon sprite atlas
.github/workflows/       CI: automated releases
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for the commit message convention and
developer commands.
