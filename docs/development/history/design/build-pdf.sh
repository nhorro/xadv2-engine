#!/usr/bin/env bash
#
# Build a single PDF from the archived design docs (00–06).
#
# Pipeline: concatenate the markdown -> `marked` (Markdown->HTML) -> wrap with
# print CSS -> headless Chrome `--print-to-pdf`. Migration notes and review
# proposals are intentionally excluded.
#
# Requirements:
#   - npx (Node.js)            — fetches/runs `marked`
#   - Google Chrome / Chromium — prints the PDF
# Optional:
#   - pdfinfo (poppler-utils)  — prints the page count on success
#
# Usage:
#   docs/development/history/design/build-pdf.sh
#   MARKED_PKG=marked@12 docs/development/history/design/build-pdf.sh
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$here"

out="extraordinary-adventures-engine-design.pdf"
docs=(
  00-index.md
  01-engine-requirements.md
  02-architecture-overview.md
  03-2d-game-concepts.md
  04-point-and-click-concepts.md
  05-scripting-api.md
  06-data-formats.md
)

# --- locate tools ---
command -v npx >/dev/null || { echo "error: npx (Node.js) not found" >&2; exit 1; }
chrome=""
for b in google-chrome google-chrome-stable chromium chromium-browser; do
  if command -v "$b" >/dev/null; then chrome="$b"; break; fi
done
[ -n "$chrome" ] || { echo "error: no Chrome/Chromium found for PDF printing" >&2; exit 1; }
marked_pkg="${MARKED_PKG:-marked@latest}"

build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT

# --- 1. concatenate: cover page + each doc on a fresh page ---
{
  echo '# Extraordinary Adventures Engine'
  echo
  echo '## Historical Engine Design Document'
  echo
  printf '_Generated %s from `docs/development/history/design/` (00–06)_\n' "$(date +%Y-%m-%d)"
  echo
  for f in "${docs[@]}"; do
    [ -f "$f" ] || { echo "error: missing $f" >&2; exit 1; }
    echo
    echo '<!-- PAGEBREAK -->'
    echo
    cat "$f"
    echo
  done
} > "$build/combined.md"

# --- 2. markdown -> html body ---
npx --yes "$marked_pkg" -i "$build/combined.md" -o "$build/body.html"

# --- 3. wrap with print CSS; turn page-break markers into divs ---
cat > "$build/full.html" <<'HTML_HEAD'
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Extraordinary Adventures Engine — Design</title>
<style>
  @page { size: A4; margin: 18mm 16mm 20mm 16mm; }
  html { -webkit-print-color-adjust: exact; print-color-adjust: exact; }
  body {
    font-family: "DejaVu Sans", "Noto Sans", Helvetica, Arial, sans-serif;
    font-size: 10.5pt; line-height: 1.45; color: #1a1a1a; margin: 0;
  }
  h1, h2, h3, h4 { line-height: 1.2; page-break-after: avoid; color: #111; }
  h1 { font-size: 21pt; border-bottom: 2px solid #444; padding-bottom: 4px; margin-top: 0.2em; }
  h2 { font-size: 15pt; margin-top: 1.4em; border-bottom: 1px solid #ccc; padding-bottom: 2px; }
  h3 { font-size: 12.5pt; margin-top: 1.1em; }
  h4 { font-size: 11pt; margin-top: 1em; }
  p { orphans: 3; widows: 3; }
  a { color: #1a4d8f; text-decoration: none; }
  code {
    font-family: "DejaVu Sans Mono", "Noto Mono", Consolas, monospace;
    font-size: 9pt; background: #f3f3f3; padding: 1px 3px; border-radius: 3px;
    word-break: break-word;
  }
  pre {
    font-family: "DejaVu Sans Mono", "Noto Mono", Consolas, monospace;
    font-size: 8.6pt; line-height: 1.35; background: #f6f8fa;
    border: 1px solid #e0e0e0; border-radius: 5px; padding: 9px 11px;
    white-space: pre-wrap; word-break: normal; overflow-wrap: anywhere;
    page-break-inside: avoid;
  }
  pre code { background: none; padding: 0; font-size: inherit; word-break: normal; }
  table {
    border-collapse: collapse; width: 100%; margin: 0.8em 0;
    font-size: 9pt; page-break-inside: avoid;
  }
  th, td {
    border: 1px solid #cfcfcf; padding: 4px 7px; text-align: left;
    vertical-align: top; overflow-wrap: anywhere;
  }
  th { background: #eef1f4; font-weight: 600; }
  tr:nth-child(even) td { background: #fafbfc; }
  blockquote {
    margin: 0.8em 0; padding: 2px 12px; border-left: 4px solid #c7c7c7;
    color: #444; background: #fafafa;
  }
  ul, ol { padding-left: 1.4em; }
  li { margin: 0.15em 0; }
  hr { border: none; border-top: 1px solid #ddd; margin: 1.4em 0; }
  .page-break { page-break-before: always; }
</style>
</head>
<body>
HTML_HEAD
sed 's|<!-- PAGEBREAK -->|<div class="page-break"></div>|g' "$build/body.html" >> "$build/full.html"
printf '\n</body>\n</html>\n' >> "$build/full.html"

# --- 4. print to PDF (Chrome sandbox warnings on stderr are harmless) ---
"$chrome" --headless=new --disable-gpu --no-sandbox --no-pdf-header-footer \
  --print-to-pdf="$here/$out" "file://$build/full.html" 2>/dev/null || true

[ -s "$here/$out" ] || { echo "error: PDF was not produced" >&2; exit 1; }

pages="$(pdfinfo "$here/$out" 2>/dev/null | awk '/^Pages/{print $2" pages"}' || true)"
echo "wrote $out ($(du -h "$here/$out" | cut -f1)${pages:+, $pages})"
