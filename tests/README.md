# Integration tests

This directory contains integration tests for the project. These tests are run
against the `libxrpl` library or `xrpld` binary to verify they are working as
expected.

The configuration-check CLI tests use only the Python standard library and a
built `xrpld` executable:

```bash
python3 tests/check_config.py .build/xrpld
```

They are also registered as `check_config` with CTest when `xrpld` and `tests`
are enabled. They check exit codes, diagnostics, referenced local files, and
that validation leaves existing files unchanged and does not bind configured
ports. A standalone lifecycle test validates a configuration, starts a node,
closes a ledger over RPC, checks the configuration while the node is running,
stops it, verifies validation leaves the persisted state untouched, and reloads
the saved ledger after restarting.
