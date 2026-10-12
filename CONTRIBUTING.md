# Contributing to Clawd Quest

## Getting started

```sh
brew install sdl3
make test   # run the save-file tests
make        # build the game
```

Run the game binary directly with `./build/clawdquest`.

## Commands

| Command           | What it does                                            |
| ----------------- | ------------------------------------------------------- |
| `make`           | Compile the game to `build/clawdquest`                 |
| `make app`       | Bundle `dist/ClawdQuest.app` (icon, SDL3, codesigning)  |
| `make test`      | Build and run the save-file test suite                  |
| `make clean`     | Delete `build/`                                          |
| `make font`      | Regenerate `src/font_atlas.h` (needs Homebrew `freetype`) |
| `make icon`      | Regenerate `build/AppIcon.icns` (needs `swift`)          |

Before submitting a change, `make test` must pass and `make app` must produce
a bundle that launches.

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
- `refactor`: code change that neither fixes nor adds anything
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
