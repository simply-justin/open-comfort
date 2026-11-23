# Contributing to OpenComfort

Thank you for helping improve OpenComfort! This guide explains how to propose changes and what we expect from a contribution.

## Ways to contribute

- **Report a bug.** Open an issue with your board, ESP-IDF version, AC model, steps to reproduce, and the relevant serial log.
- **Add an air conditioner.** Write a profile. See [AC Profiles](docs/profiles.md#adding-a-new-air-conditioner).
- **Improve the firmware or docs.** Open a pull request.

For security issues, follow [SECURITY.md](SECURITY.md) instead of opening a public issue.

## Development workflow

1. Fork the repository and create a branch from `main`, for example `feat/uart-telemetry` or `fix/spi-mode`.
2. Set up the toolchain as described in [Getting Started](docs/getting-started.md).
3. Make your change and make sure `idf.py build` passes without new warnings.
4. Test on real hardware whenever your change touches a bus, a module, or a profile. Say in the PR what you tested and on which hardware.
5. Update the documentation in the same pull request when behaviour, configuration, or the API changes.
6. Open a pull request with a clear description of **what** changed and **why**.

### Commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/), as the existing history does:

```text
feat: add UART telemetry polling
fix: clear power bit when turning the unit off
docs: document CAN bus settings
```

## Coding guidelines

- Follow the existing C++17 / ESP-IDF style and file layout under `main/src/`.
- Respect the architecture boundaries described in [Architecture](docs/architecture.md):
  - Module logic stays behind `IModule` (`bootstrap()`, `isBooted()`, `run()`).
  - Hardware access goes through `IBus`. New transports are added in `BusFactory`.
  - New module types are added through `ModuleConfig` and `ModuleFactory`.
  - HTTP, Wi-Fi, and Matter code stays out of modules.
  - Vendor-specific behaviour belongs in profiles, not in C++.
- Validate all external input (HTTP bodies, manifests, profiles) and return clear error messages.
- Write code that is safe for the hardware. Never send a frame you haven't fully built and validated.
- Document hardware pins, electrical assumptions, and safety constraints for every new integration.
- Do not include third-party code unless its licence is compatible and stated.

## Contribution licence terms

OpenComfort is dual-licensed (see [LICENSE.md](LICENSE.md) and [COMMERCIAL_LICENSE.md](COMMERCIAL_LICENSE.md)). By submitting a contribution (a pull request, issue, code, or documentation), you agree that:

1. Your contribution is licensed under the project's public licence (GPLv3, non-commercial terms).
2. The project maintainer, [COMPANY], may include your contribution in commercially licensed versions of OpenComfort.
3. You have the right to submit the contribution, and it does not infringe any third-party rights.

These terms keep OpenComfort open for personal use while allowing sustainable commercial development.

## Questions

Open a [GitHub Discussion or issue](https://github.com/simply-justin/open-comfort/issues), or contact [EMAIL].
