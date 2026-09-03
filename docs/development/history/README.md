# Historical design documents

The markdown under [`design/`](design/00-index.md), plus the old C++ and Lua
guides in this directory, is **frozen history**. It documents earlier stages of
engine v2: requirements tags, MVP notes, issue numbers, and specifications that
mixed target and as-built.

Do not add features by extending those pages. Do not treat them as the
source of truth for new work.

The as-built tour lives in [`../tour/`](../tour/index.md); active proposals and
debt tracking live in [`../plans/`](../plans/target-and-debt.md).

Author-facing YAML and Lua references live under `docs/authoring/`.
The archive is context, not an API reference. If current documentation and code
disagree, fix one or both; do not resolve the disagreement by citing this
directory. Leave these files unchanged except for repairs required to keep the
archive readable after its move.
