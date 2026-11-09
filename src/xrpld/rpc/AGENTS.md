# AGENTS.md — rpc

See [README.md](./README.md) for the RPC subsystem design.

Any change to publicly-visible API behavior — RPC/WebSocket fields, parameters, or error conditions, or transaction/signing behavior surfaced through the API even from outside this directory — needs an entry in [`API-CHANGELOG.md`](../../../API-CHANGELOG.md) under a `## Unreleased` section (`### Additions`, `### Bugfixes`, `### Deprecations`, etc. as appropriate). If that section is absent, create it above the latest `## XRP Ledger server version x.y.z` heading; it is renamed at release.
