# Coding conventions

This file holds the **formatting** machinery (clang-format, install, run, CI
check). Naming, identifiers, headers, ownership, error handling, and C++20
usage rules live in the [C++ engine coding guide](coding-guide/cpp-engine.md)
— that is the source of truth.

## Formatting (clang-format)

The repository’s `.clang-format` file is the source of truth for whitespace,
braces, and include ordering. Highlights: LLVM base, **4-space indent**,
**100-column limit**, pointers/references bound left (`const std::string& id`),
attached braces, one parameter/argument per line when wrapping, namespace-closing
comments.

Install and run (Ubuntu 24.04 ships clang-format 18):

```bash
sudo apt install clang-format

# format everything we own (never third_party/ — see below)
find lib games tests experiments -type f \( -name '*.hpp' -o -name '*.cpp' \) \
  -not -path 'third_party/*' -print0 | xargs -0 clang-format -i

# check only (CI): non-zero exit on any diff
clang-format --dry-run --Werror path/to/file.cpp
```

Vendored code under `third_party/` (e.g. micropather, when it lands) is
**not** reformatted. When that directory is added, drop a
`third_party/.clang-format` containing `DisableFormat: true` so editor-on-save
and recursive runs leave it untouched.

## Scope split

| Concern | Lives in |
|---------|----------|
| Whitespace, braces, includes, indent, columns | this file + `.clang-format` |
| Naming, headers, ownership, error handling, sol2/yaml-cpp patterns, tests, CMake | [coding-guide/cpp-engine.md](coding-guide/cpp-engine.md) |
| Lua scripting + YAML authoring conventions | [coding-guide/lua-game.md](coding-guide/lua-game.md) |
| `what` the engine does | [design docs](design/00-index.md) |
