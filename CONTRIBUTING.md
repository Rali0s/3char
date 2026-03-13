# Contributing

Thanks for contributing to 3TTY.

## Development Setup

1. Install dependencies (Qt6 + CMake + compiler toolchain + optional libssh).
2. Configure and build:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build -j
```

3. Run tests:

```bash
ctest --test-dir build --output-on-failure
```

## Pull Request Guidelines

- Keep changes focused and scoped.
- Include build/test results in the PR description.
- Update docs for user-facing behavior changes.
- Avoid introducing regressions in terminal interaction, scheduler flows, or profile persistence.

## Coding Notes

- C++20 standard.
- Prefer clear, minimal interfaces.
- Preserve cross-platform behavior (macOS/Linux/Windows).
