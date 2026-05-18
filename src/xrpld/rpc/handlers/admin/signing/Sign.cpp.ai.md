# `Sign.cpp` — `doSign` RPC Handler

`Sign.cpp` implements the `doSign` RPC handler — the server-side entry point for the `sign` JSON-RPC command. Its role is to accept a raw transaction object and a signing secret, produce a signed transaction blob, and return it to the caller without submitting it to the network. The file is intentionally minimal: it enforces access control, tags the resource cost, delegates all cryptographic and structural work to `RPC::transactionSign`, and appends a deprecation notice before returning.

## Access Control: Two Paths to Permission

The first thing `doSign` does is decide whether the caller is even allowed to sign. The check is:

```cpp
if (context.role != Role::ADMIN && !context.app.config().canSign())
    return RPC::make_error(rpcNOT_SUPPORTED, "Signing is not supported by this server.");
```

This reflects a deliberate security policy. XRPL validators and public API nodes are not signing oracles: they should never accept raw key material from untrusted clients. The gate has two independent conditions that must both fail before access is denied. Admin connections (typically authenticated via a local port or credentials) bypass the config check entirely, since the operator is considered trusted. Non-admin callers are only permitted if the server has been explicitly configured with `[signing_support] = 1` — setting `signingEnabled_` to `true` in `Config`. That field defaults to `false`, so signing over a public endpoint is opt-in, not opt-out. This design reflects a deliberate hardening decision: a server inadvertently exposed to the internet will refuse to act as a signing oracle by default.

## Resource Burden

Before delegating, the handler marks the request as `Resource::feeHeavyBurdenRPC`. This feeds the server's rate-limiting and resource-metering system. Signing is computationally non-trivial (key derivation, serialization, ECDSA/Ed25519 signing), so it is correctly classified as a heavier burden than simple read queries. This prevents a client from hammering the signing endpoint without consequence.

## `fail_hard` Handling

The `fail_hard` flag is extracted defensively:

```cpp
NetworkOPs::FailHard const failType = NetworkOPs::doFailHard(
    context.params.isMember(jss::fail_hard) && context.params[jss::fail_hard].asBool());
```

The short-circuit `isMember()` check guards against accessing a missing field, converting its absence to `false`. This is slightly more careful than the parallel `doSignFor` handler in `SignFor.cpp`, which reads `context.params[jss::fail_hard].asBool()` directly. Since `doSign` only signs without submitting, `fail_hard` is passed through to `transactionSign` where it influences how errors in the signing pipeline are treated rather than network submission behaviour.

## Delegation to `transactionSign`

All substantive work — parsing `tx_json`, resolving the key from `secret` (or `seed` / `seed_hex` / `passphrase` variants), auto-filling fields like `Sequence` and `Fee` from the validated ledger, serializing the transaction, and computing the cryptographic signature — is handled by `RPC::transactionSign` in `detail/TransactionSign.h`. The handler passes `params` by value (as the function signature requires for local modification), the current API version, the `failType`, the caller's role, and the age of the most recently validated ledger. The ledger age matters because `transactionSign` will reject signing requests if the server's view of the ledger is stale, preventing the issuance of transactions based on outdated state.

## Deprecation Annotation

Regardless of success or failure, `doSign` appends a `deprecated` field to every response:

```cpp
ret[jss::deprecated] =
    "This command has been deprecated and will be "
    "removed in a future version of the server. Please "
    "migrate to a standalone signing tool.";
```

This follows the broader XRPL project direction of moving signing responsibility out of `rippled` entirely. The security argument is straightforward: a server node should not handle secret keys. Standalone tools such as `xrpl.js`, `xrpl-py`, or hardware wallets sign locally without the key ever leaving the client. The deprecation message is always present — not conditional on the caller's role — signalling that even admin callers should migrate.

## Relationship to Sibling Handlers

The `signing/` subdirectory contains three related handlers: `Sign.cpp`, `SignFor.cpp`, and `ChannelAuthorize.cpp`. `Sign.cpp` and `SignFor.cpp` are structurally near-identical, sharing the same access-control check, resource fee, and deprecation message. The difference is that `SignFor` calls `transactionSignFor`, which appends a signature to a transaction without replacing existing signatures — used for multi-signing flows where a designated signer adds their signature on behalf of another account. `Sign.cpp` is the simpler single-signer case. Both are deprecated in favour of client-side signing.