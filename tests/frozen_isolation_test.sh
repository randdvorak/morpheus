#!/bin/sh
set -eu

binary=$1
bundle=$2
source_root=$3
build_root=$4
temporary=$(/usr/bin/mktemp -d "${TMPDIR:-/tmp}/morpheus-frozen-isolation.XXXXXX")
symbols=$temporary.symbols
text=$temporary.strings
libraries=$temporary.libraries
relocated=$temporary/Relocated.app
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

/usr/bin/ditto "$bundle" "$relocated"
binary=$relocated/Contents/MacOS/MorpheusFrozen

/usr/bin/nm "$binary" >"$symbols"
/usr/bin/strings "$binary" >"$text"
/usr/bin/otool -L "$binary" >"$libraries"

if /usr/bin/grep -Eiq \
    'authoring|agent_session|runtime_module|(^|_)tcc_|libtcc|tinycc' "$symbols"; then
    echo "Frozen binary contains development symbols" >&2
    exit 1
fi
if /usr/bin/grep -Fq "$source_root" "$text" ||
   /usr/bin/grep -Fq "$build_root" "$text" ||
   /usr/bin/grep -Eiq 'dev\.morpheus\.authoring|libtcc|tinycc' "$text"; then
    echo "Frozen binary contains a build path or authoring identifier" >&2
    exit 1
fi
if /usr/bin/tail -n +2 "$libraries" | /usr/bin/awk '{print $1}' |
    /usr/bin/grep -Ev '^(/System/Library/|/usr/lib/)' >/dev/null; then
    echo "Frozen binary links a non-system dynamic library" >&2
    exit 1
fi
/usr/bin/find "$relocated" -type f | while IFS= read -r file; do
    relative=${file#"$relocated"/}
    case $relative in
        Contents/Info.plist|Contents/MacOS/MorpheusFrozen|\
        Contents/Resources/export-manifest.json|Contents/Resources/assets/*|\
        Contents/_CodeSignature/*) ;;
        *) echo "Unexpected frozen bundle file: $relative" >&2; exit 1 ;;
    esac
done
/usr/bin/codesign --verify --deep --strict "$relocated"
MORPHEUS_RUNTIME_VALIDATE_ONLY=1 \
MORPHEUS_APP_SUPPORT_DIR=$temporary/state \
    "$binary"
echo "PASS: relocated frozen bundle is isolated and ABI-valid"
