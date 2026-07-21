# Morpheus Authoring Self-Hosting

Morpheus's builder interface is a `morph_app_api` application module. It uses
the same lifecycle and TinyCC loader as generated applications, but receives a
development-only capability registry that is never attached to generated
client previews or frozen exports.

## Bootstrap and preview

The native shell always starts with the ahead-of-time
`morph_authoring_app_entry()` implementation. This is the known-good bootstrap
and remains available even when editable authoring source does not compile or
initialize.

The recovery controls above the builder UI provide the self-hosting workflow:

1. **Preview authoring source** compiles `src/authoring/morpheus_app.c` with
   TinyCC, validates the application ABI, initializes it against the authoring
   capability registry, and swaps it at the frame boundary.
2. **Accept authoring UI** atomically copies the previewed source into the
   authoring recovery store. Acceptance becomes the candidate restored on the
   next clean launch.
3. **Rollback authoring UI** or **Use bootstrap UI** stages the AOT module and
   transactionally restores its preserved bootstrap state.
4. **Reload authoring source** stages another preview from the editable source
   while a session-accepted module is active.

Compilation, ABI validation, initialization, and migration failures retain the
currently active authoring module.

## Recovery policy

The default recovery directory is:

```text
~/Library/Application Support/dev.morpheus.seed/authoring
```

`accepted.c` is the bounded, atomically written accepted authoring source.
`session.active` is armed after bootstrap initialization and removed only after
clean authoring-module destruction.

On startup Morpheus follows this order:

1. Create the AOT bootstrap.
2. Preserve its neutral rollback state.
3. Check safe-mode and previous-session status.
4. Compile and activate `accepted.c` only after a clean previous exit and when
   safe mode is not requested.
5. Arm the new session marker.

An existing session marker indicates an abnormal authoring exit. Morpheus does
not load accepted authoring source in that launch and leaves the bootstrap
active. A corrupt or incompatible `accepted.c` behaves the same way without
making startup fail.

## Development overrides

- `MORPHEUS_SAFE_MODE=1` forces the known-good bootstrap.
- `MORPHEUS_AUTHORING_UI_SOURCE=/path/to/morpheus_app.c` selects editable
  source for manual preview.
- `MORPHEUS_AUTHORING_STATE_ROOT=/path/to/directory` selects an isolated
  recovery store, primarily for tests.

The bootstrap and trusted kernel remain conventional AOT build products.
Self-authoring does not modify the renderer, compiler, provider
implementations, recovery policy, or process-level services.
