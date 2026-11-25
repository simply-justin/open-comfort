# Contributing to OpenComfort

Thank you for helping improve OpenComfort. This guide explains how to propose changes and what a contribution needs.

## Ways to contribute

- **Report a bug.** Open an issue with your board, ESP-IDF version, AC model, steps to reproduce and the relevant serial log. Remove credentials and tokens from logs first.
- **Add an air conditioner.** Write a profile. See [AC Profiles](docs/profiles.md#adding-an-air-conditioner).
- **Improve the firmware or docs.** Open a pull request.

Report security problems privately, as described in [SECURITY.md](SECURITY.md#reporting-a-vulnerability).

## Development workflow

1. Fork the repository and branch from `main`, for example `feat/uart-profile` or `fix/mqtt-reconnect`.
2. Set up ESP-IDF v5.5.1 as described in [Getting Started](docs/getting-started.md).
3. Make your change and check that `idf.py build` passes without new warnings and `./scripts/run-unit-tests.sh` passes.
4. Test on an ESP32-S3 when the change touches a bus, a module, a connector or a profile. State in the pull request what you tested and on which hardware. Do not claim hardware behaviour you have not tested.
5. Update the documentation in the same pull request when behaviour, configuration or an API changes.
6. Open a pull request that explains what changed and why.

### Commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/), as the history does:

```text
feat: add CAN profile support
fix: bound MQTT reconnect back-off
docs: document the profile catalog
```

## Coding guidelines

- Follow the existing C++ style and use ESP-IDF APIs.
- Check every ESP-IDF return value that can affect safe operation.
- Make ownership and task lifetimes explicit. Avoid unbounded waits and blocking work in event handlers or FreeRTOS tasks.
- Keep reusable modules free of board-specific pins and transport settings. Those belong in the board definition and validated profiles.
- Connectors submit commands to `RuntimeController`; they never touch a bus directly.
- Register new source files in `main/CMakeLists.txt`.
- Document changes to pins, bus settings, partitions or persisted configuration.
- Never commit `sdkconfig`, `build/`, credentials, private keys, serial device paths or captures that contain secrets.
- Do not include third-party code unless its licence is compatible and stated.

## Contribution licence terms

OpenComfort is dual-licensed (see [LICENSE.md](LICENSE.md) and [COMMERCIAL_LICENSE.md](COMMERCIAL_LICENSE.md)). By submitting a contribution (a pull request, issue, code or documentation) you agree that:

1. Your contribution is licensed under the project's public licence (GPLv3, non-commercial terms).
2. The project maintainer, [COMPANY], may include your contribution in commercially licensed versions of OpenComfort.
3. You have the right to submit the contribution and it does not infringe third-party rights.

These terms keep OpenComfort open for personal use while allowing sustainable commercial development.

## Questions

Open an [issue](https://github.com/simply-justin/open-comfort/issues) or contact [EMAIL].
