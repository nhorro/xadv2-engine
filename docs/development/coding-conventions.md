# Coding conventions

This file documents the formatting machinery used by the current tree. The
repository's code, tests, and `.clang-format` settle details not covered here.

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
find lib tests examples -type f \( -name '*.hpp' -o -name '*.cpp' \) \
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
| Runtime concepts and dependencies | [as-built architecture tour](tour/index.md) |
| Lua scripting and YAML data | [authoring documentation](../authoring/index.md) |
| `what` the engine does | [as-built architecture tour](tour/index.md) |
