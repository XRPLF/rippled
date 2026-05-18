# `LogRotate.cpp` — `logrotate` RPC Handler

This file implements `doLogRotate`, the handler behind the `logrotate` admin RPC command. Its entire body is two lines: one to rotate the performance log, and one to rotate the application log and return the result. The simplicity is intentional — all complexity lives in the subsystems it delegates to.

## What it does

`doLogRotate` invokes `rotate()` on two distinct logging systems exposed through the `Application` object:

1. **`context.app.getPerfLog().rotate()`** — triggers log rotation on the performance log, a separate structured file tracking job queue timings and operational metrics. `PerfLog::rotate()` is a pure virtual method defined in `PerfLog.h`, meaning the concrete implementation varies but the interface contract is stable.

2. **`context.app.getLogs().rotate()`** — rotates the main application log. The `Logs` class owns an inner `Logs::File` object that keeps the log file open for the process lifetime. The `rotate()` method (backed by `File::closeAndReopen()`) closes the current file descriptor and reopens it at the same path. This is the standard pattern for interoperating with external log management tools like `logrotate(8)`, which rename or truncate the file before signaling the process to reopen it. The `rotate()` call returns a `std::string` status message, which `RPC::makeObjectValue` wraps into the JSON response sent back to the caller.

## Access control and registration

The handler is registered in `Handler.cpp` as:
```cpp
{"logrotate", byRef(&doLogRotate), Role::ADMIN, NO_CONDITION}
```

`Role::ADMIN` means the RPC dispatch framework rejects any caller that has not been granted admin access before this function is ever reached. `NO_CONDITION` means no particular ledger state is required. The handler itself performs no validation — it assumes the framework has already enforced the access boundary.

The sibling `LogLevel.cpp` follows the same pattern for the `log_level` command, and both live under `handlers/admin/log/` as a natural grouping of log-management admin operations.