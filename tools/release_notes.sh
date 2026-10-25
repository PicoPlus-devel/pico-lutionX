#!/bin/sh
# Print the CHANGELOG.md section of one release, for the GitHub release text.
#
#   tools/release_notes.sh v0.1-alpha > release-notes.md
#
# The section runs from the heading "## <tag>" to the next "## " heading. Fails when
# the section is missing or empty, so a release is never published without notes.
TAG="$1"
if [ -z "$TAG" ]; then
    echo "usage: $0 <tag>" >&2
    exit 1
fi
cd "$(dirname "$0")/.." || exit 1

NOTES=$(awk -v tag="$TAG" '
    /^## / { if (found) exit; found = ($0 == "## " tag); next }
    found  { print }
' CHANGELOG.md)

if [ -z "$(printf '%s' "$NOTES" | tr -d '[:space:]')" ]; then
    echo "CHANGELOG.md has no section \"## $TAG\"" >&2
    exit 1
fi
printf '%s\n' "$NOTES"
