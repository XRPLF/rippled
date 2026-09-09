# AGENTS.md — rpc

See the repo-level [AGENTS.md](../../../AGENTS.md) for general build/test/style guidance, and [README.md](./README.md) for the RPC subsystem design.

Any change to publicly-visible API behavior — RPC/WebSocket fields, parameters, or error conditions, or transaction/signing behavior surfaced through the API even from outside this directory — needs an entry in [`API-CHANGELOG.md`](../../../API-CHANGELOG.md) under `## Unreleased` (`### Additions`, `### Deprecations`, etc. as appropriate).
