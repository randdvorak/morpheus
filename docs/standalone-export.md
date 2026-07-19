# Standalone macOS Export

Morpheus can freeze accepted generated C into a conventional macOS application
that does not contain TinyCC, coding agents, revision controls, llama.cpp, or
source/build-tree paths.

From the repository root:

```sh
tools/morpheus-export /path/to/MyApp.app
```

The default input is the active project selected in Morpheus. A specific
accepted revision may be provided as the second argument:

```sh
tools/morpheus-export /path/to/MyApp.app projects/my-app/revisions/00000004/app.c
```

The exporter refuses to replace an existing destination. Display metadata can
be configured without editing source:

```sh
MORPHEUS_EXPORT_NAME="FX Rates" \
MORPHEUS_EXPORT_BUNDLE_ID="com.example.fx-rates" \
MORPHEUS_EXPORT_VERSION="1.0.0" \
tools/morpheus-export /path/to/FXRates.app
```

The command performs a Clang ahead-of-time build, copies generated assets into
`Contents/Resources/assets`, writes a versioned export manifest, copies the
bundle to the requested destination, and applies and verifies an ad-hoc
hardened-runtime signature. Developer ID signing and notarization remain a
distribution step.

At runtime, mutable state is stored atomically under
`~/Library/Application Support/<bundle-id>/state.bin`, outside the signed
bundle. Tests can override the parent location with
`MORPHEUS_APP_SUPPORT_DIR`. The state envelope records its format and app ABI;
the generated app remains responsible for versioning and migrating its payload.

The first frozen profile exposes HTTP, JSON, and image capabilities. It links
SDL, Nuklear, yyjson, and stb into the executable and dynamically links only
macOS system libraries and frameworks. It honors the same optional
`morph_app_render_mode` export as the development host, so full-window Nuklear
applications are not placed inside an extra host-owned window after export.
