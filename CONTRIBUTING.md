# Contributing to Clawd Quest

## Getting started

```sh
cargo test    # run the save-file tests
cargo build   # build the game
```

Run the game binary directly with `./target/debug/clawdquest`, or `cargo run`.
Building requires only a Rust toolchain (`rustup`; no system dependencies —
wgpu links Metal, cpal links CoreAudio).

## Commands

| Command           | What it does                                         |
| ----------------- | ---------------------------------------------------- |
| `cargo build`     | Compile the game to `target/debug/clawdquest`        |
| `cargo run`       | Build and launch the game                            |
| `cargo test`      | Run the save-file test suite                         |
| `cargo xtask app` | Bundle `dist/ClawdQuest.app` (icon, codesigning)     |
| `cargo xtask dmg` | Bundle, then package a macOS arm64 DMG               |
| `cargo clean`     | Delete `target/`                                     |

Before submitting a change, `cargo test` must pass and `cargo xtask app` must
produce a bundle that launches.

## Commit messages

This project uses [Conventional Commits](https://www.conventionalcommits.org/).
Each commit message starts with a type, optionally followed by a scope in
parentheses and a colon, then a short imperative summary:

```
<type>(<scope>): <summary>

<optional body>

<optional footer>
```

Types:

- `feat`: a new feature
- `fix`: a bug fix
- `docs`: documentation only (README, comments)
- `refactor`: code change that neither fixes a bug nor adds anything
- `perf`: change that improves performance
- `test`: adding or updating tests
- `build`: build system or external dependency changes
- `ci`: CI workflow changes
- `chore`: maintenance that fits none of the above

Examples:

```
feat(ui): add hover highlight to quest cards
fix(save): migrate legacy store.json path on first launch
docs: document release process in README
ci(release): pin actions/checkout by commit SHA
```

A `BREAKING CHANGE:` footer, or a `!` after the type/scope (`feat!:`), marks a
change that breaks compatibility.

## Releasing

Releases are tagged, not hand-built. Bump nothing by hand — the workflow
stamps the tag version into the bundle. Push a tag and CI does the rest:

```sh
git tag -a v1.2.0 -m "Clawd Quest 1.2.0"
git push origin v1.2.0
```

See [README.md](README.md) for what the release workflow produces.
