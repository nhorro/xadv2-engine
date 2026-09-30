# C++ code browser

Doxygen complements the narrative [architecture tour](tour/index.md) with a
searchable, cross-linked view of the implementation. It indexes both
`lib/include/engine` and `lib/src`, including undocumented and private symbols.
That makes it useful for following large implementation classes such as
`RoomScene`; it is not an API documentation-completeness report.

## Build

Install Doxygen. Generating the browser does not configure or compile the
engine:

```bash
doxygen Doxyfile
```

Open `.doc/doxygen/html/index.html` in a browser. The output is HTML only; no
PDF/LaTeX toolchain is configured.

A fully provisioned engine build can instead configure with
`-DPAC_BUILD_DOXYGEN=ON` and build the `doxygen` target. The option is off by
default, so normal builds and projects that consume the engine as a subdirectory
do not need Doxygen.

## Scope

- Included: engine public headers and implementation sources.
- Excluded: tests, examples, generated build trees, and vendored dependencies.
- Enabled: source browsing, symbol references, search, and navigation tree.
- Optional: set `HAVE_DOT = YES` in `Doxyfile` when Graphviz is installed to add
  bounded include, inheritance, and collaboration graphs.
- Deliberately disabled even with Graphviz: call/caller graphs, which become too
  dense around the room session and obscure the useful relationships.

Use the architecture tour and the
[interactive room architecture view](room-point-and-click.html) for the meaning
of the boundaries; use Doxygen to jump from those boundaries to exact types,
members, definitions, and include relationships.
