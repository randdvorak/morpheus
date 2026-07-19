# External coding-agent protocol

Morpheus invokes a coding-agent provider asynchronously with four positional
arguments:

```text
provider RUN_DIRECTORY PROMPT_PATH DIAGNOSTICS_PATH RESPONSE_PATH
```

The provider may read `request.txt`, the per-attempt `prompt.txt` and
`diagnostics.txt`, `candidate.c`, `app_api.h`, and `sdk.h` inside the run directory.
`PROMPT_PATH` is the exact composed prompt for the current attempt. It must
restrict its source changes to
`candidate.c`, write a human-readable final response to `RESPONSE_PATH`, and
exit with status zero when generation succeeds. Morpheus owns compilation,
activation, preview, acceptance, and rollback; a provider never writes the
accepted `generated/app.c` directly.

Each run is retained under the active project's `agent/runs/` directory (or the
configured legacy workspace) with the original request, source before the
change, per-attempt diagnostics, provider output, candidate snapshots, build
results, a unified patch, and the final accepted, rejected, or failed outcome.
Compiler or activation failures are passed back through the next attempt's
diagnostics file, up to three attempts.

The provider is selected with the `MORPHEUS_AGENT_PROVIDER` CMake cache value or
the runtime environment variable of the same name.

The default adapter, `tools/morpheus-codex-agent`, uses `codex exec` in
non-interactive, ephemeral mode with approvals disabled and ambient user
configuration/rules ignored. Repository discovery is anchored to the
individual run directory with a local Git repository. An outer macOS sandbox
denies reads of protected home locations while allowing Codex's authentication
and bootstrap files. Codex's internal command sandbox is bypassed because macOS
does not permit that nested sandbox helper to initialize inside the outer
sandbox. The provider prompt restricts source changes to `candidate.c`, but the
provider remains trusted development code rather than a complete write-isolation
boundary. Set `MORPHEUS_CODEX_EXECUTABLE` when the Codex CLI is installed
somewhere other than the ChatGPT application bundle.

Generated code is always treated as a preview first. A user must explicitly
accept it before Morpheus updates durable source and creates a revision.
Rejecting recompiles the pre-run source and restores the state captured before
preview activation.

## Nuklear module access

`morph_host.nuklear` exposes the host-owned `struct nk_context`, and runtime
compilation registers every public `nk_*` function enabled by Morpheus's
Nuklear configuration. Modules include `morpheus/app_api.h`, which supplies the
matching official Nuklear types and declarations. This keeps one context and
one Metal renderer while allowing generated code to use the complete widget,
layout, styling, window, popup, chart, tree, group, menu, query, and canvas API.

Existing modules render inside the host-owned **Generated Application** window.
A module that wants to create arbitrary Nuklear windows exports:

```c
unsigned int morph_app_render_mode(void)
{
    return MORPHEUS_RENDER_NUKLEAR_WINDOWS;
}
```

In that mode, `render_ui` must balance every `nk_begin` with `nk_end`. Context
initialization, input, conversion, clearing, and shutdown remain host-owned.

Seed modules also have TinyCC headers and an explicit freestanding C runtime
subset. Morpheus bridges allocation (`malloc`, `calloc`, `realloc`, `free`),
memory/string operations, numeric conversion, and `qsort`/`bsearch` into the
host process while retaining `-nostdlib`. Modules must bound allocations and
release owned memory from `destroy`; other libc, SDL, OS, filesystem, network,
and time APIs are unavailable.

The first SDK service is asynchronous HTTP through `host->http`. Use
`morph_http_get` or `morph_http_post_json`, poll with `morph_http_poll` from
`update`, and release completed or abandoned requests with
`morph_http_cancel`. The host implements the facade with the target platform's
native asynchronous networking stack and bounds each response at 1 MiB;
`render_ui` must never wait on network I/O.

JSON is available through the opaque `morph_json_*` facade. Parse text with
`morph_json_parse`, borrow values from the returned document, and release the
document only after all of its values are no longer used. Query values by type;
typed getters fail rather than coercing incompatible JSON values. For output,
create a `morph_json_builder`, attach each mutable value only once, set one root,
and call `morph_json_serialize`. Mutable values belong to their builder. The
serialized `morph_json_buffer` is the only transferred allocation and must be
released with `morph_json_buffer_free`. Generated modules must not retain parsed
values after freeing their document or mutable values after freeing their
builder.

Generated or decoded images use the opaque `morph_image_*` facade. Encoded
bytes and URLs remain available through `morph_image_load_memory` and
`morph_image_load_url`. Procedural raster applications should keep a bounded,
tightly packed RGBA8 buffer, recompute it outside `render_ui` only when its
inputs change, and upload it with `morph_image_load_rgba`. The returned image is
immediately ready and can replace tens of thousands of per-frame Nuklear canvas
commands with one cached image draw. Expensive recomputation should be divided
into bounded row or work-item batches across updates while the previous image
remains visible. Every image ID must be released.

Providers should keep each frame bounded and responsive. In particular, they
must avoid large per-frame primitive grids and repeated image, simulation, or
mathematical computation in `render_ui`.

## Ollama provider

`tools/morpheus-ollama-agent` connects to Ollama's native API at
`http://localhost:11434` by default. Select **Provider: Ollama** in Morpheus and
optionally enter an installed model name. A blank model selects the first
installed non-cloud model returned by `/api/tags` (falling back to the first
model only when no local model exists). Start the service with `ollama serve` and install a
coding-capable model with `ollama pull <model>`.

The adapter sends a non-streaming `/api/chat` request with temperature zero and
a JSON schema requiring complete `source` and `summary` strings. The candidate
is replaced atomically only after that structured response validates. The raw
request and response remain in the run directory for diagnosis.

Transient curl failures, including cloud 5xx responses and request timeouts,
are retried twice with a bounded 120-second retry window before the attempt is
reported as failed.

If a model ignores the schema and returns a fenced or raw complete C source
response, the adapter safely extracts it when `morph_app_entry` is present;
arbitrary prose is still rejected.

The version-controlled system prompt is
`tools/morpheus-ollama-system-prompt.txt`. The adapter appends the exact
per-run `app_api.h` and `sdk.h` to it, so the model receives the authoritative
host capabilities instead of guessing or asking which API is available. Set
`MORPHEUS_OLLAMA_SYSTEM_PROMPT` to use a different prompt file.

Configuration environment variables:

- `MORPHEUS_AGENT_BACKEND=ollama` starts Morpheus with Ollama selected.
- `MORPHEUS_OLLAMA_MODEL` selects a model without using the UI field.
- `MORPHEUS_OLLAMA_URL` overrides the default localhost server.
- `MORPHEUS_OLLAMA_KEEP_ALIVE` defaults to `10m`.
- `MORPHEUS_OLLAMA_TIMEOUT` defaults to 600 seconds.
- `MORPHEUS_OLLAMA_SYSTEM_PROMPT` overrides the system-prompt file.

See Ollama's [API introduction](https://docs.ollama.com/api/introduction),
[model listing](https://docs.ollama.com/api/tags), and
[structured outputs](https://docs.ollama.com/capabilities/structured-outputs)
documentation for the underlying server behavior.
