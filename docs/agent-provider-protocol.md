# External coding-agent protocol

Morpheus invokes a coding-agent provider asynchronously with four positional
arguments:

```text
provider RUN_DIRECTORY PROMPT_PATH DIAGNOSTICS_PATH RESPONSE_PATH
```

The provider may read `request.txt`, the per-attempt `prompt.txt` and
`diagnostics.txt`, `candidate.c`, and `app_api.h` inside the run directory.
`PROMPT_PATH` is the exact composed prompt for the current attempt. It must
restrict its source changes to
`candidate.c`, write a human-readable final response to `RESPONSE_PATH`, and
exit with status zero when generation succeeds. Morpheus owns compilation,
activation, preview, acceptance, and rollback; a provider never writes the
accepted `generated/app.c` directly.

Each run is retained under `generated/agent/runs/` with the original request,
source before the change, per-attempt diagnostics, provider output, candidate
snapshots, build results, a unified patch, and the final accepted, rejected, or
failed outcome. Compiler or activation failures are passed back through the
next attempt's diagnostics file, up to three attempts.

The provider is selected with the `MORPHEUS_AGENT_PROVIDER` CMake cache value or
the runtime environment variable of the same name.

The default adapter, `tools/morpheus-codex-agent`, uses `codex exec` in
non-interactive, ephemeral, workspace-write mode with approvals disabled and
ambient user configuration/rules ignored. Its
sandbox root is anchored to the individual run directory with a local Git
repository, so it cannot modify the accepted application or the host
repository. Set `MORPHEUS_CODEX_EXECUTABLE` when the
Codex CLI is installed somewhere other than the ChatGPT application bundle.

Generated code is always treated as a preview first. A user must explicitly
accept it before Morpheus updates durable source and creates a revision.
Rejecting recompiles the pre-run source and restores the state captured before
preview activation.

## Ollama provider

`tools/morpheus-ollama-agent` connects to Ollama's native API at
`http://localhost:11434` by default. Select **Provider: Ollama** in Morpheus and
optionally enter an installed model name. A blank model selects the first model
returned by `/api/tags`. Start the service with `ollama serve` and install a
coding-capable model with `ollama pull <model>`.

The adapter sends a non-streaming `/api/chat` request with temperature zero and
a JSON schema requiring complete `source` and `summary` strings. The candidate
is replaced atomically only after that structured response validates. The raw
request and response remain in the run directory for diagnosis.

The version-controlled system prompt is
`tools/morpheus-ollama-system-prompt.txt`. The adapter appends the exact
per-run `app_api.h` to it, so the model receives the authoritative host
capabilities instead of guessing or asking which API is available. Set
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
