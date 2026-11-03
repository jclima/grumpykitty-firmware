# Contributing to GrumpyKitty

Pull requests and feature requests are welcome. The cat grumbles, but he reads everything.

## Proposing a feature

Open a request with the [feature request template](https://github.com/jclima/grumpykitty-firmware/issues/new/choose). For anything larger than a small tweak (a new app, a new dependency, behaviour changes), please open an issue first so we can agree on the shape before you spend your weekend on it. Bug reports are gold, especially with a serial log from `make monitor`.

## Submitting a pull request

1. Fork the repository and create a branch from `main`.
2. Keep the change focused; one topic per PR.
3. `make build` must pass with 0 warnings.
4. `tools/host_tests/run.sh` must pass.
5. Add an entry under `[Unreleased]` in [CHANGELOG.md](CHANGELOG.md).
6. In the PR description, say what hardware testing you did, or say plainly that you did none.

## Adding an app

See "Adding an app" in [DEVELOPMENT.md](DEVELOPMENT.md). Icons and other art: [tools/art/README.md](tools/art/README.md).

## Coding notes

- Use the `onOpen`/`onClose` lifecycle: `onClose` is the only teardown, so `onOpen` must re-initialise state.
- Never block in `onRunning`; return quickly.
- The build uses `-zmuldefs`, so keep file-local symbols in anonymous namespaces to avoid silent duplicate-symbol clashes.
- Take colours from `src/ui/ui_theme.h`, not hard-coded values.

## Licensing

Contributions are accepted under the MIT license (see [NOTICE.md](NOTICE.md) and [LICENSE-MIT](LICENSE-MIT)). Do not submit code you cannot license that way. Third-party code keeps its own license and must be listed in [NOTICE.md](NOTICE.md). New source files carry an `SPDX-License-Identifier: MIT` header.

## Security issues

Do not open public issues for vulnerabilities; follow [SECURITY.md](SECURITY.md).

## Be kind

Be respectful and constructive. Grumpy is the cat's job, not yours.
