# Issue #6180: Fee Voting via CLI/RPC

## Summary

This change adds a new `fee_vote` command that allows reading and updating fee vote targets at runtime (without restarting `rippled`).

Issue: https://github.com/XRPLF/rippled/issues/6180

## Problem

Before this change, fee voting targets were configured from startup config only. Operators could not adjust fee vote targets live through CLI/RPC.

## Solution

Added a new RPC/CLI handler: `fee_vote`.

Behavior:

- Read current fee vote targets:
  - `fee_vote`
- Update targets (admin only):
  - `fee_vote <reference_fee> <account_reserve> <owner_reserve>`
  - values are applied immediately and returned in the response.

## Interface

### Request

- Command: `fee_vote`
- Optional fields:
  - `reference_fee`
  - `account_reserve`
  - `owner_reserve`

### Validation

- Each field must be a non-negative integer.
- `account_reserve` and `owner_reserve` are bounded to `<= 4294967295`.

### Permissions

- Read (`fee_vote` with no fields): allowed.
- Update (any field present): admin-only (`rpcNO_PERMISSION` for non-admin).

## Implementation Notes

### New files

- `src/xrpld/rpc/handlers/FeeVote.cpp`
  - Implements `doFeeVote`.
- `src/test/rpc/FeeVoteRPC_test.cpp`
  - Covers read/update, permissions, and validation behavior.

### Updated files

- `src/xrpld/app/misc/FeeVote.h`
  - Added mutable target API:
    - `setTarget(...)`
    - `getTarget() const`
- `src/xrpld/app/misc/FeeVoteImpl.cpp`
  - Added synchronization for live target updates.
  - Consensus paths now read from a thread-safe target snapshot.
- `src/xrpld/app/consensus/RCLConsensus.h`
- `src/xrpld/app/consensus/RCLConsensus.cpp`
  - Added passthrough methods for fee vote get/set.
- `include/xrpl/server/NetworkOPs.h`
- `src/xrpld/app/misc/NetworkOPs.cpp`
  - Added public runtime API used by RPC layer.
- `src/xrpld/rpc/handlers/Handlers.h`
- `src/xrpld/rpc/detail/Handler.cpp`
  - Registered `fee_vote` RPC handler.
- `src/xrpld/rpc/detail/RPCCall.cpp`
  - Added CLI argument parser for `fee_vote`.
- `src/xrpld/app/main/Main.cpp`
  - Added CLI help entry.
- `include/xrpl/protocol/jss.h`
  - Added required JSON field keys.
- `src/test/rpc/RPCCall_test.cpp`
  - Added parser coverage for `fee_vote`.

## Build and Test Evidence

### Build

```bash
cd .build
cmake --build . --target xrpld --parallel 8
```

### Targeted unit tests

```bash
./xrpld --unittest FeeVoteRPC,RPCCall --unittest-jobs 1 --quiet
```

Result: passed (`0 failures`).

## Local Smoke Tests (Standalone)

Environment:

- Standalone node (`--standalone`)
- Config: `/tmp/xrpld/xrpld.cfg`
- RPC endpoint: `127.0.0.1:5005`

### 1) Read current fee vote target

```bash
./.build/xrpld --conf /tmp/xrpld/xrpld.cfg fee_vote
```

Observed result:

- `status: success`
- returned current tuple (`reference_fee`, `account_reserve`, `owner_reserve`)

### 2) Update fee vote target

```bash
./.build/xrpld --conf /tmp/xrpld/xrpld.cfg fee_vote 11 222 33
```

Observed result:

- `status: success`
- returned:
  - `reference_fee: 11`
  - `account_reserve: 222`
  - `owner_reserve: 33`

### 3) Read after update (persistence in running process)

```bash
./.build/xrpld --conf /tmp/xrpld/xrpld.cfg fee_vote
```

Observed result:

- Returned `11 / 222 / 33`, confirming live update was applied.

### 4) Invalid input handling

```bash
./.build/xrpld --conf /tmp/xrpld/xrpld.cfg fee_vote foo 222 33
```

Observed result:

- `status: error`
- `error: invalidParams`
- message indicates `reference_fee` must be a non-negative integer.

## Notes

- This feature updates the runtime fee vote target and is aimed at operational convenience.
- `--standalone` remains isolated from public XRPL networks and is suitable for local testing.
