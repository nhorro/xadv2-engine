# Contributing to xadv2-engine

Thank you for your interest in contributing to xadv2-engine. This project is a remake of the Extraordinary Adventures point-and-click engine, and we welcome improvements, bug fixes, documentation updates, and new examples.

## Workflow Overview

1. Fork the repository.
2. Clone your fork locally.
3. Create a feature branch from `develop` for your work.
4. Make small, focused changes.
5. Test your changes locally.
6. Open a pull request against the `develop` branch of the main repository.
7. Add a clear description and link any relevant issue.

## Local Setup

1. Clone the repository:
   ```bash
   git clone https://github.com/<your-username>/xadv2-engine.git
   cd xadv2-engine
   ```
2. Create a branch for your work from `develop`:
   ```bash
   git checkout develop
   git pull origin develop
   git checkout -b feature/your-change
   ```
3. Build and test changes using the project tools available in the repository. If there is a specific build or test command for the engine, use it here.

## Branching and Commit Guidelines

- Use a descriptive branch name, for example:
  - `feature/add-scripting-example`
  - `fix/renderer-crash`
  - `docs/update-data-format`
- Keep commits small and focused.
- Write clear commit messages describing the intention of the change.

## Code Contributions

- Follow the [coding conventions](docs/coding-conventions.md): C++ formatting is
  enforced by `.clang-format` (run it before committing); naming and file layout
  rules are documented there.
- Prefer simple, readable solutions.
- If your change affects documentation, update or add docs under `docs/sources/`.
- If relevant, add or update tests in `tests/`.

## Documentation Contributions

Documentation is important for this project.

- The main project overview is in `README.md`.
- Design and architecture content lives under `docs/sources/design/`.
- If you add a new engine feature or data format, document it alongside the code.

## Pull Request Process

1. Push your branch to your fork.
2. Open a pull request against the upstream `develop` branch.
3. Include a short summary of changes and any testing performed.
4. Link any related issue or design discussion.
5. Be responsive to review feedback and update your branch as needed.

## Issues and Discussions

- Use GitHub Issues to report bugs, request features, or ask questions.
- Provide enough context to reproduce a bug or explain a feature request.
- When submitting a bug report, include platform details and steps to reproduce.

## General Tips

- Prefer incremental improvements rather than large, sweeping changes.
- Keep the repository organized and easy to navigate.
- Collaborate through issues and PR comments when you are unsure about the best approach.

## License

By contributing, you agree that your contributions will be made under the terms of the repository license.
