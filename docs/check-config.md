# Validate a configuration before restarting

Run `xrpld --check-config` to check configuration without starting a server:

```bash
xrpld --conf /etc/xrpld/xrpld.cfg --check-config
```

The command exits with **0** on success or **1** with a diagnostic on standard
error if validation fails. Without `--conf`, it uses the normal configuration
search paths, including the legacy `rippled.cfg` filename. A missing or unreadable
configuration is an error. Use `--standalone` to check standalone settings.

Validation reuses the startup parsers for configuration values, server ports,
TLS credentials, validator keys and manifests, cluster and overlay settings,
amendment votes, SQLite settings, node-store and online-deletion options, transaction queues,
and performance logging. It reads referenced validator files and TLS files,
including gRPC credentials, and checks validator-list URLs without fetching
remote lists. Relative paths have the same meaning as during normal startup.

It does not create data or log directories, open databases, execute startup RPC
commands, bind ports, or connect to peers. RPC, unit-test, ledger-loading and
database-maintenance modes cannot be combined with `--check-config`.

For example, a systemd service can check its configuration before starting:

```ini
ExecStartPre=/usr/bin/xrpld --conf /etc/xrpld/xrpld.cfg --check-config
```

Run the check as the same user and from the same working directory as the
service. Success does not guarantee that runtime resources are available:
database integrity, write permissions, free disk space, port availability and
remote services are not tested. In particular, the check can run while another
node is using the configured databases and ports.
