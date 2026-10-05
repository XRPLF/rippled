# API Changelog

This changelog is intended to list all updates to the [public API methods](https://xrpl.org/public-api-methods.html).

For info about how [API versioning](https://xrpl.org/request-formatting.html#api-versioning) works, including examples, please view the [XLS-22d spec](https://github.com/XRPLF/XRPL-Standards/discussions/54). For details about the implementation of API versioning, view the [implementation PR](https://github.com/XRPLF/rippled/pull/3155). API versioning ensures existing integrations and users continue to receive existing behavior, while those that request a higher API version will experience new behavior.

The API version controls the API behavior you see. This includes what properties you see in responses, what parameters you're permitted to send in requests, and so on. You specify the API version in each of your requests. When a breaking change is introduced to the `xrpld` API, a new version is released. To avoid breaking your code, you should set (or increase) your version when you're ready to upgrade.

The [commandline](https://xrpl.org/docs/references/http-websocket-apis/api-conventions/request-formatting/#commandline-format) sends `api_version` 1 unless the request names one. `kApiCommandLineVersion` is stamped on a request naming none, and a static assert holds it at or below the highest non-beta version, so the default cannot reach 3; the `json` and `json2` commands pass a caller-supplied `api_version` through as given. The command line is intended for ad-hoc usage by humans, not programs or automated scripts. The command line is not meant for use in production code.

For a log of breaking changes, see the **API Version [number]** headings. In general, breaking changes are associated with a particular API Version number. For non-breaking changes, scroll to the **XRP Ledger version [x.y.z]** headings. Non-breaking changes are associated with a particular XRP Ledger (`xrpld`) release.

## API Version 3 (Beta)

API version 3 is currently a beta API. It requires `[beta_rpc_api]` set to `1` in the xrpld configuration to use; the section takes one value line, and a bare header leaves the flag off. See [API-VERSION-3.md](API-VERSION-3.md) for the full list of changes in API version 3.

From this version, replies take the JSON-RPC 2.0 envelope on both transports, with the deviations [API-VERSION-3.md](API-VERSION-3.md) lists, a top-level array is accepted as a batch, and subscription messages are sent as notifications. The `ripplerpc` field no longer selects the reply envelope and is ignored.

A request that is rejected before it reaches a method is answered with a JSON-RPC error object carrying `jsonrpc`, `id` and `error`, whether it was sent alone or as an entry in a batch; the `id` is the request's own, or `null` where the `id` itself was the unusable member. The HTTP status is the one the matching rejection reports at earlier versions; the two rejections version 3 adds, an unusable `id` and a `method` disagreeing with the one inside `params`, answer 400. On the JSON-RPC transport three classes of rejection stay a plain text body, because the server cannot tell which envelope the client speaks: a body it could not parse or that exceeds the size limit, a body that parses to `{}` or `null` and so carries no request, and a request whose `api_version`, where the server reads it, names a version it does not serve, unless it is an entry of a batch. That place is inside `params` for a lone JSON-RPC request, a top-level value naming such a version being ignored there and the request served as version 1. The WebSocket transport never answers plain text: a message naming such a version at its top level, where that transport reads it, is answered in the legacy frame those versions have always used, and a frame the server cannot read is answered with the `jsonInvalid` frame, at every version. An empty array `[]` is not in the second class: only a version 3 client sends an array, so it is answered with a `-32600` error object and `Request is empty`. A batch entry is not in the third: an entry of a top-level array naming such a version is answered in place with a `-32606` error object in the array's envelope, and an entry of a `"method": "batch"` request is answered with an error object in the body's envelope, so neither takes the body down with it. A version 3 client must therefore tolerate a non-JSON body for the three classes.

A JSON-RPC (HTTP) request may also name `api_version` at its top level, beside `method` and `id`, and may send `params` as the object itself rather than an array holding it. Neither reaches the WebSocket transport, whose message is one flat object that has always carried `api_version` at its top level and whose members are the parameters themselves, so a `params` member there is not unpacked. The `params` object is the specification's own spelling; `api_version` is an XRPL extension, which the specification does not define, placed beside the members it does. Both are additive: for a lone request a top-level `api_version` is honored only when it names version 3 or above, since a lower one has been answered as version 1 for years. An entry of either batch form honors any version it names at its top level: a `"method": "batch"` entry carries its parameters at its own top level, so that is where its version sits, and one that carries a by-name `params` object is read for `api_version` and credentials from that object instead; an array entry is read the same way so that the two forms agree, and an entry naming a served version other than the body's refuses the body, while one naming a version the server does not serve is answered on its own.

## API Version 2

API version 2 is available in `xrpld` version 2.0.0 and later. See [API-VERSION-2.md](API-VERSION-2.md) for the full list of changes in API version 2.

## API Version 1

This version is supported by all `xrpld` versions. For WebSocket and HTTP JSON-RPC requests, it is currently the default API version used when no `api_version` is specified.

## Unreleased

### Breaking changes

- The `ripplerpc` request field, which selects the shape of the JSON-RPC reply envelope, is now validated, and a value that is not exactly `"1.0"`, `"2.0"` or `"3.0"` is rejected. Two things it does not reach: a WebSocket session, which reads the field only to echo it back, and API version 3, where the field selects nothing and so is neither read nor checked, `ripplerpc: "banana"` and no `ripplerpc` at all being served alike. A request sending `"2"`, `" 2.0"`, `"2.00"`, `"02.0"`, `"2.0.0"`, `"10.0"` or any other text, such as `"abc"` or `"x2"`, gets a working reply today. After this ships, such a request sent alone gets HTTP 400 with `ripplerpc is not a supported version`, charged as a malformed request, and such an entry of a `"method": "batch"` body gets that error in its own reply while the batch answers 200. A client that spells the version loosely must therefore be corrected to one of the three exact values, or omit the field. Previously the value was compared as a string, which both accepted values that name no version and ordered multi-digit versions incorrectly: `"abc"` and `"x2"` sorted above `"3.0"` and so selected version 3, and `"10.0"` sorted below `"2.0"` and so selected version 1. Requests that send one of the three supported values, or omit the field, are unaffected.
- `subscribe`: every subscription a connection holds must name the same `api_version`. The first `subscribe` establishes it, and a later one naming a different version is refused with `apiVersionConflict`, registering nothing; the message names the version the connection's subscriptions are served at. The HTTP status is 400 where the envelope derives it from the error, with `ripplerpc: "3.0"` or at API version 3, and 200 with `ripplerpc` `"1.0"` or `"2.0"`, as for every error; a WebSocket frame carries no status. A `subscribe` refused for another reason, a stream name the server does not have for one, still fixes that version, since the registrations it makes are spread through the handler. A connection that subscribed one stream at `api_version` 2 and another at 1, in two calls, was previously served both at version 1, the later call's, so the first stream's shape changed under it. The version is fixed for the connection's life: a client that wants another version opens another connection. On the webhook path the subscriber is keyed on its `url` alone, so the refusal reaches a second admin because of a first admin's version, and the message names the version the url's subscriptions are served at, and no remedy: releasing a url means unsubscribing its streams, which would remove that first admin's subscription, and the server cannot tell the two callers apart. `API-VERSION-3.md` describes the procedure and that risk. Naming the url alone does not release it, a subscriber a stream map still holds not being evicted. This reaches every API version, since version 1 and version 2 content is not interchangeable either: version 2 renames `transaction` to `tx_json` and hoists `hash`, so a version 1 client handed version 2 content finds no `transaction` member.
- `batch`: An entry of a `"method": "batch"` request that names `api_version` both inside `params` and at its own top level is now served at the version inside `params`. It was previously served at the top-level version whenever the one inside `params` resolved to version 1, so an entry asking for version 1 explicitly and something else at the top level changes which version answers it. A lone request can name both values too, and for it the version inside `params` has always won; reversing that precedence was confined to a batch entry.
- `batch`: every entry of one batch request is now served at the same API version. An entry may name the version the body names, or name none and inherit it, and a body whose entries name two different versions is refused whole with `Batch entries name different versions` at HTTP 400, charged as a malformed request. **This is visible to a version 1 or 2 client:** a `"method": "batch"` body could mix versions across its entries, each entry being dispatched at its own, and the difference is observable because the reply shape changes with the version. Two bodies change. One that mixes versions is now refused instead of answered. One where some entries name a version and others name none now serves all of them at the named version, where an entry naming none was served at version 1; the same applies to a version named on the request's own top level, which the entries now inherit. A body whose entries all name the same version, or name none at all, is unaffected. A `"method": "batch"` body naming at its own top level a version the server does not serve is refused with `invalid_API_version` at HTTP 400, where it was previously served at version 1 as if it had named none. No test pinned the mixing behavior and the line that produced it was a fallback about where the field sits rather than a decision that entries may differ, so this is a correction rather than a removal of a feature: a per-entry version made the entry cap above unanswerable, since a cap can only ask about a body that has one version.

### Additions

- A JSON-RPC (HTTP) request may send its parameters as an object, `"params": {"account": "r..."}`, as well as the array of one object that was already accepted. This reaches every API version: such a request was previously rejected with HTTP 400 and `params unparsable`, and is now served. A request that already sends the array form is unaffected. An entry of a `"method": "batch"` request that carries a by-name `params` object is read for `api_version` and credentials from that object. Previously such an entry had its `api_version` read from its top level and its credentials ignored, since credentials were read only from an array-form `params`.
- A lone JSON-RPC (HTTP) request naming `api_version` 3 or above at its own top level, beside `method` and `id`, is now answered at that version. The member is an XRPL extension, which the JSON-RPC 2.0 specification does not define, placed beside the members it does. It was previously answered at version 1, the version being read from inside `params` alone; a WebSocket request, which is one flat object, has always been read there. This is deliberate: reading the version beside the specification's own members is what lets a version 3 client send a specification-shaped request. A top-level value below version 3, or one the server does not serve, is still ignored for a lone request, since `"api_version": 2` there has been answered as version 1 for years and honoring it now would change a shipped reply shape. Only API version 3 is reachable this way, and it is gated behind `[beta_rpc_api]`.

### Bugfixes

- A request echoed back in an error reply now has every credential-bearing field masked: `admin_password`, `admin_user`, `passphrase`, `password`, `secret`, `seed`, `seed_hex`, `url_password`, `url_username` and `username`. Nesting no longer matters, so a credential inside `params` is masked too. The same masking is applied to every request and reply written to the log, and it covers six further names that only a reply carries: `master_key`, `master_seed`, `master_seed_hex`, `validation_key`, `validation_private_key` and `validation_seed`, which is how `wallet_propose` and `validation_create` used to write a live private key to the log. A request or reply written to the log is truncated at 10,000 characters.
- The command line client no longer prints a credential the operator did not type. A failing command echoes the request it built under `request_sent`, which carries the `admin_password` the client copies out of `[port_rpc]` in the config, so `./xrpld account_info rBogus` printed that password to stdout and into any captured output. `request_sent` is now masked. The `rpc` member beside it, which echoes the arguments as they were typed, is unchanged. The command line client also no longer writes an unparsed `json` or `ripple_path_find` argument to its trace log before parsing it, where a `secret` inside that argument could not be masked; it logs the parsed request instead, masked. The reply it receives is logged the same way, parsed and masked, where the raw body was written before, a `validation_create` answer included.
- A WebSocket frame that does not parse, or exceeds the request size limit, is answered `{"type": "error", "error": "jsonInvalid", "size": <bytes>}`. The frame's body is reported by size rather than echoed back in a `value` member, since a body that does not parse has no fields to mask. A client that read `value` gets `size` instead.
- Four error codes that named no HTTP status of their own, and so answered 200 on a reply reporting an error, now name one: `actMalformed`, `alreadyMultisig` and `alreadySingleSig` answer 400, and `actNotFound` answers 404. **Only API version 3 reports these four.** A request sending `ripplerpc: "3.0"` at API version 1 or 2 still receives 200 for all four, as it always has, so `account_info` on a malformed account or one the ledger does not hold answers 200 for those clients exactly as before.
- `submit`, `simulate`, `transaction_entry`, `ledger_entry` and `ledger_accept`: Errors from these methods now include `error_code` and `error_message` alongside the `error` token, as every other method already did. Each error now answers the status its code names: 400 for a malformed request, 404 for `transactionNotFound`, 500 for an internal failure, and 501 for `notYetImplemented` and `notStandAlone`. That status change reaches only a request sending `ripplerpc: "3.0"`, or API version 3, which are the envelopes that derive the status from the error. With `ripplerpc` `"1.0"` the status stays 200 and the two new members appear beside `error`; with `"2.0"` the status stays 200, `error_code` appears, and the `code` and `message` members carry the code and the message rather than null, since that envelope copies them from `error_code` and `error_message` and drops `error_message`. From API version 3 the twenty-one `malformed*` tokens `ledger_entry` names each report the code they own instead of 31. See [API-VERSION-3.md](API-VERSION-3.md).
- A reply reporting HTTP 402 or 502 now carries a status line. Those two statuses named no case in the switch that writes one, so such a reply began with a header instead and did not parse as an HTTP response at all. Both are reachable at any API version with `ripplerpc: "3.0"`, which derives the status from the error code: 402 through `highFee` from `sign`, `sign_for` or `submit` with a low `fee_mult_max`, and 502 through `dbDeserialization` from `tx`. The eleven statuses that already named a case report the same phrase they always have.
- An error reply to a request sending `ripplerpc: "2.0"` or `"3.0"` no longer carries a stray `"error_message": null` beside the error it reports. The member appeared only when the `Server` log partition was set to debug or lower, because the log statement read `error_message` after the reply had renamed it to `message`, and reading it put it back as null. So the reply a client received depended on the server's log level, and the log line itself printed an empty message. Both are fixed.
- A body the server rejects before it reads a request out of it now says what was wrong. A body over the size limit answers `Request is too large`, and a body that parses to `{}` or `null` answers `Request is empty`; both previously answered `Unable to parse request: ` with nothing after the colon, the parser having recorded no error for them. Any other body that is not an array does not parse, which includes one that is only whitespace and one whose top-level value is a string, number or boolean; it answers `Unable to parse request: ` followed by the parser's own reason, as it did before. The status is 400 for all three, as before, and this reaches every API version. A top-level array, the one other body nothing can read a request from, is answered with a JSON error object instead, which the entry on top-level arrays below describes.
- `batch`: An entry that is not identified through a secure gateway no longer clears the connection's `X-User` and forwarded-for values for the entries after it, so every entry of one body reports the role and username it would have reported on its own.
- `batch`: Every entry of a `"method": "batch"` request is now charged against the sender's resource allowance, including one rejected before it reaches a handler, and the request stops at the first entry the connection is too loaded to serve. A reply array can therefore be shorter than the request array, and its last element depends on where the batch stopped. An entry that is not a JSON object, or names an API version the server does not serve, is charged before its role is known and answered with its own rejection; if that charge took the connection over the drop threshold, the batch stops there and that rejection is the last answer. Every other entry met over the threshold is answered `Server is overloaded` and nothing follows. A client should read any reply array shorter than its request as a connection over the drop threshold, whatever the last element says. A well-behaved client is unaffected; one that sends thousands of entries in a single body no longer gets every one of them answered.
- A body the server rejects before it reads a request out of it is now charged against the sender's resource allowance, as a malformed request already was. Ten conditions were free: a body over the size limit, one that does not parse, one carrying no document, one that is neither a JSON object nor an array, a `"method": "batch"` naming no entry array, and six ways a top-level array is not a batch this server can serve. The answer to the first five is unchanged; an array's answer changes as the entry on top-level arrays below describes, the one generic rejection it received having become six. A well-behaved client is unaffected; one that repeats such a body exhausts its allowance and is refused the next request a handler would have served. A WebSocket frame that does not parse, or exceeds the request size limit, is charged the same way, and a connection that sends only such frames is closed once it crosses the drop threshold.
- `subscribe`, `path_find`: A subscription is now served at the API version its own `subscribe` call named, rather than at the version of the most recent `subscribe` or `path_find` on the same connection. A `path_find` no longer changes the version anything that connection has subscribed is served at. An ordinary request is unaffected: it names its own version and is answered at it, so a connection subscribed at `api_version` 3 still calls `account_info` at version 1 and reads the version 1 shape.
- `batch`: A batch request served at API version 3 or above is now refused when it holds more than 100 entries, before any entry is dispatched. The cap reads the one version the body is served at, so no ordering of the entries evades it. A body served below version 3 is unaffected and stays bounded only by the request size limit.
- A top-level JSON array, which is the JSON-RPC 2.0 batch form accepted from API version 3, is now rejected with a JSON-RPC error object rather than a plain text body when it cannot be served: when it is empty, holds more than 100 entries, holds nothing that could be a request, names no API version of 3 or above that the server serves, or names two different versions the server serves. An array naming no served version of 3 or above is refused for requiring version 3, or, where every version it names is one the server does not serve, with `invalid_API_version`. One entry naming a version the server does not serve does not reject an array in which another entry names a served version: it is answered on its own, inside the reply array, with the same `-32606` code. This reaches every API version, since no earlier version accepts an array at all: a client that sent one received HTTP 400 with `Unable to parse request: ` and now receives the same status with a JSON body. The message also names the version rather than the shape where the version is what is wrong, which is what a client with `[beta_rpc_api]` disabled receives.

## XRP Ledger server version 3.5.0

Version 3.5.0 is not yet released.

### Additions in 3.5.0

- `subscribe`, `unsubscribe`: Added an optional `mpt_issuances` request field, an array of MPT issuance IDs (hex strings). Subscribers receive the same `transaction` message as the `transactions` stream for each validated transaction whose metadata affects a subscribed issuance. MPT issuance subscriptions count toward the per-connection subscription limit. An empty array, a non-array value, or an invalid ID returns `invalidParams`. ([#5671](https://github.com/XRPLF/rippled/pull/5671))
- `ledger_entry`: Add full support for checks, NFT offers, payment channels, and signer lists. ([#6319](https://github.com/XRPLF/rippled/pull/6319))

### Bugfixes in 3.5.0

- `channel_authorize`: The `channel_id` field now returns an `invalidParams` error if the value is not a string. [#7582](https://github.com/XRPLF/rippled/pull/7582)
- `channel_verify`: The `channel_id` and `signature` fields now return an `invalidParams` error if the value is not a string. [#7582](https://github.com/XRPLF/rippled/pull/7582)

### Bugfixes in 3.5.0

- `feature`: The admin-only `vetoed` field now returns `invalidParams` unless its value is a boolean. [#7583](https://github.com/XRPLF/rippled/pull/7583)

## XRP Ledger server version 3.4.0

Version 3.4.0 is not yet released. These changes are available in the 3.4.0 beta releases.

### Additions in 3.4.0

- `ledger`: `nftoken_id`, `nftoken_ids`, and `offer_id` are now included in transaction metadata when transactions are expanded (`expand`, or admin-only `full`), matching the `tx`, `account_tx`, and `subscribe` (`transactions` stream) responses. ([#5706](https://github.com/XRPLF/rippled/pull/5706))

### Bugfixes in 3.4.0

- `sign`, `sign_for`, `submit`: `signature_target` now returns `invalidParams` unless it names `CounterpartySignature` or `SponsorSignature`. It previously accepted any inner object field, such as `Book` or `NFToken`, and signed into it.
- `sign`, `sign_for`, `submit`, `submit_multisigned`: With `fixCleanup3_4_0` enabled, a signature in `CounterpartySignature` or `SponsorSignature` covers a different prefix than the transaction's own signature, so a signature can no longer be moved from one of those roles into another. Clients that build these signatures themselves must use the new prefixes: `CPT` and `CPM` (single- and multi-signing) for `CounterpartySignature`, and `SPN` and `SPM` for `SponsorSignature`.
- `get_aggregate_price`: Duplicate entries in the `oracles` request array are now ignored. [#6586](https://github.com/XRPLF/rippled/pull/6586)
- `vault_info`: Errors now identify what the request got wrong instead of reporting every failure as the unregistered token `malformedRequest`, and the `error`, `error_code` and `error_message` fields now agree with each other. An invalid `vault_id` or `seq` returns `invalidParams`, an invalid `owner` returns `actMalformed`, and a request that mixes `vault_id` with `owner`/`seq` or supplies neither returns `invalidParams` with a message naming the accepted combinations. [#8015](https://github.com/XRPLF/rippled/pull/8015)
- `vault_info`: A well-formed all-zero `vault_id` now returns `entryNotFound` instead of being rejected as malformed, and `entryNotFound` responses now include `error_code` and `error_message`. Clients that request `ripplerpc` 3.0 or above therefore receive HTTP 400 with that error rather than HTTP 200. [#8015](https://github.com/XRPLF/rippled/pull/8015)
- `vault_info`: `vault_id` and `owner` must now be strings, matching how `ledger_entry` reads the same fields. An object or an array in either field previously produced an internal error, and a number was silently converted to its decimal text; `vault_id` now returns `invalidParams` and `owner` returns `actMalformed`. [#8015](https://github.com/XRPLF/rippled/pull/8015)
- `gateway_balances`: The `account` and `ident` fields now return an `invalidParams` error if the value is not a string, instead of an `internal` error. [#7655](https://github.com/XRPLF/rippled/pull/7655)
- `account_lines`: The `peer` field now returns an error if the value is not a string. [#7728](https://github.com/XRPLF/rippled/pull/7728)
- `ledger`: `delivered_amount` is now included in the metadata of successful `AccountDelete` transactions when transactions are expanded (`expand`, or admin-only `full`). Previously it was only added for `Payment` and `CheckCash`, which made `ledger` inconsistent with `tx` and `account_tx`. [#5706](https://github.com/XRPLF/rippled/pull/5706)
- `noripple_check`: The `transactions` field is no longer included in error responses; it is still returned (possibly as an empty array) whenever `transactions` is `true` and the request succeeds. A malformed `account` is now rejected before the ledger is looked up, so that error response no longer carries the `ledger_hash`, `ledger_index`, and `validated` fields ([#6303](https://github.com/XRPLF/rippled/pull/6303)).
- `transaction_entry`: An object or an array in `tx_hash` now returns `malformedRequest`, like any other value that is not a hex hash, instead of an `internal` error.

## XRP Ledger server version 3.3.0

[Version 3.3.0](https://github.com/XRPLF/rippled/releases/tag/3.3.0) was released on Aug 6, 2026.

### Additions in 3.3.0

- `account_tx`: Added an optional `delegate` request object to filter delegated transactions. The object requires `delegate_filter`, which must be either `actor` for transactions owned by the requested account but signed by another account, or `authorizer` for transactions signed by the requested account on behalf of another account. The optional `counter_party` account narrows the results to a specific signer/delegate for `actor` or a specific owner/delegator for `authorizer`. Malformed `delegate`, `delegate_filter`, and `counter_party` values return standard invalid field errors, and invalid account IDs return `actMalformed`. When paginating delegate-filtered queries, a marker from a delegate-filtered query includes a `delegate` flag and is only valid for follow-up requests that also supply `delegate` (mixing marker conventions returns `invalidParams`). Because filtering is applied after the ledger scan, a page may contain fewer results than `limit` (possibly zero) while still returning a marker, so callers must continue until no marker is present. ([#6126](https://github.com/XRPLF/rippled/pull/6126))

## XRP Ledger server version 3.2.1

[Version 3.2.1](https://github.com/XRPLF/rippled/releases/tag/3.2.1) was released on Aug 1, 2026.

This release contains bug fixes only and no API changes.

## XRP Ledger server version 3.2.0

[Version 3.2.0](https://github.com/XRPLF/rippled/releases/tag/3.2.0) was released on Jun 16, 2026.

### Additions in 3.2.0

- `ledger_entry`, `account_objects`: The `Delegate` ledger entry now includes an optional `DestinationNode` field, which stores the index into the authorized account's owner directory. This field is present on entries created after bidirectional directory tracking was introduced and may appear in RPC responses for those entries. ([#6681](https://github.com/XRPLF/rippled/pull/6681))
- `server_definitions`: Added the following new sections to the response ([#6321](https://github.com/XRPLF/rippled/pull/6321)):
  - `TRANSACTION_FORMATS`: Describes the fields and their optionality for each transaction type, including common fields shared across all transactions.
  - `LEDGER_ENTRY_FORMATS`: Describes the fields and their optionality for each ledger entry type, including common fields shared across all ledger entries.
  - `TRANSACTION_FLAGS`: Maps transaction type names to their supported flags and flag values.
  - `LEDGER_ENTRY_FLAGS`: Maps ledger entry type names to their flags and flag values.
  - `ACCOUNT_SET_FLAGS`: Maps AccountSet flag names (asf flags) to their numeric values.

### Bugfixes in 3.2.0

- Peer Crawler: The `port` field in `overlay.active[]` now consistently returns an integer instead of a string for outbound peers. [#6318](https://github.com/XRPLF/rippled/pull/6318)
- `ping`: The `ip` field is no longer returned as an empty string for proxied connections without a forwarded-for header. It is now omitted, consistent with the behavior for identified connections. [#6730](https://github.com/XRPLF/rippled/pull/6730)
- gRPC `GetLedgerDiff`: Fixed error message that incorrectly said "base ledger not validated" when the desired ledger was not validated. [#6730](https://github.com/XRPLF/rippled/pull/6730)
- `account_channels`: The `destination_account` field now returns an error if the value is not a string. [#6529](https://github.com/XRPLF/rippled/pull/6529)
- `subscribe`: The `taker` field in the `books` array now returns an error if the value is not a string. [#6529](https://github.com/XRPLF/rippled/pull/6529)
- `account_info`: The `urlgravatar` field now uses HTTPS instead of HTTP. [#6529](https://github.com/XRPLF/rippled/pull/6529)
- `ledger`: The `full`, `accounts`, `transactions`, `expand`, `binary`, `owner_funds`, and `queue` fields now return an error if the value is not a boolean. [#6529](https://github.com/XRPLF/rippled/pull/6529)
- `ledger_data`: The `binary` field now returns an error if the value is not a boolean. [#6529](https://github.com/XRPLF/rippled/pull/6529)
- `submit`: The `fail_hard` field now returns an error if the value is not a boolean. [#6529](https://github.com/XRPLF/rippled/pull/6529)
- `subscribe`: The `taker` field in the `books` array now returns `actMalformed` instead of `badIssuer` if the value is not a valid account. [#6529](https://github.com/XRPLF/rippled/pull/6529)
- Fixed a bug in `Forwarded` HTTP header parsing where the extracted IP address could be incorrect when no comma or semicolon delimiter follows the address. This could cause the server to misidentify a client's IP address when operating behind a reverse proxy. [#6529](https://github.com/XRPLF/rippled/pull/6529)

## XRP Ledger server version 3.1.3

[Version 3.1.3](https://github.com/XRPLF/rippled/releases/tag/3.1.3) was released on May 8, 2026.

This release contains bug fixes only and no API changes.

## XRP Ledger server version 3.1.2

[Version 3.1.2](https://github.com/XRPLF/rippled/releases/tag/3.1.2) was released on Mar 12, 2026.

This release contains bug fixes only and no API changes.

## XRP Ledger server version 3.1.1

[Version 3.1.1](https://github.com/XRPLF/rippled/releases/tag/3.1.1) was released on Feb 23, 2026.

This release contains bug fixes only and no API changes.

## XRP Ledger server version 3.1.0

[Version 3.1.0](https://github.com/XRPLF/rippled/releases/tag/3.1.0) was released on Jan 27, 2026.

### Additions in 3.1.0

- `vault_info`: New RPC method to retrieve information about a specific vault (part of XLS-66 Lending Protocol). ([#6156](https://github.com/XRPLF/rippled/pull/6156))

## XRP Ledger server version 3.0.0

[Version 3.0.0](https://github.com/XRPLF/rippled/releases/tag/3.0.0) was released on Dec 9, 2025.

### Additions in 3.0.0

- `ledger_entry`: Supports all ledger entry types with dedicated parsers. ([#5237](https://github.com/XRPLF/rippled/pull/5237))
- `ledger_entry`: New error codes `entryNotFound` and `unexpectedLedgerType` for more specific error handling. ([#5237](https://github.com/XRPLF/rippled/pull/5237))
- `ledger_entry`: Improved error messages with more context (e.g., specifying which field is invalid or missing). ([#5237](https://github.com/XRPLF/rippled/pull/5237))
- `ledger_entry`: Assorted bug fixes in RPC processing. ([#5237](https://github.com/XRPLF/rippled/pull/5237))
- `simulate`: Supports additional metadata in the response. ([#5754](https://github.com/XRPLF/rippled/pull/5754))

## XRP Ledger server version 2.6.2

[Version 2.6.2](https://github.com/XRPLF/rippled/releases/tag/2.6.2) was released on Nov 19, 2025.

This release contains bug fixes only and no API changes.

## XRP Ledger server version 2.6.1

[Version 2.6.1](https://github.com/XRPLF/rippled/releases/tag/2.6.1) was released on Sep 30, 2025.

This release contains bug fixes only and no API changes.

## XRP Ledger server version 2.6.0

[Version 2.6.0](https://github.com/XRPLF/rippled/releases/tag/2.6.0) was released on Aug 27, 2025.

### Additions in 2.6.0

- `account_info`: Added `allowTrustLineLocking` flag in response. ([#5525](https://github.com/XRPLF/rippled/pull/5525))
- `ledger`: Removed the type filter from the RPC command. ([#4934](https://github.com/XRPLF/rippled/pull/4934))
- `subscribe` (`validations` stream): `network_id` is now included. ([#5579](https://github.com/XRPLF/rippled/pull/5579))
- `subscribe` (`transactions` stream): `nftoken_id`, `nftoken_ids`, and `offer_id` are now included in transaction metadata. ([#5230](https://github.com/XRPLF/rippled/pull/5230))

## XRP Ledger server version 2.5.1

[Version 2.5.1](https://github.com/XRPLF/rippled/releases/tag/2.5.1) was released on Sep 17, 2025.

This release contains bug fixes only and no API changes.

## XRP Ledger server version 2.5.0

[Version 2.5.0](https://github.com/XRPLF/rippled/releases/tag/2.5.0) was released on Jun 24, 2025.

### Additions and bugfixes in 2.5.0

- `tx`: Added `ctid` field to the response and improved error handling. ([#4738](https://github.com/XRPLF/rippled/pull/4738))
- `ledger_entry`: Improved error messages in `permissioned_domain`. ([#5344](https://github.com/XRPLF/rippled/pull/5344))
- `simulate`: Improved multi-sign usage. ([#5479](https://github.com/XRPLF/rippled/pull/5479))
- `channel_authorize`: If `signing_support` is not enabled in the config, the RPC is disabled. ([#5385](https://github.com/XRPLF/rippled/pull/5385))
- `subscribe` (admin): Removed webhook queue limit to prevent dropping notifications; reduced HTTP timeout from 10 minutes to 30 seconds. ([#5163](https://github.com/XRPLF/rippled/pull/5163))
- `ledger_data` (gRPC): Fixed crashing issue with some invalid markers. ([#5137](https://github.com/XRPLF/rippled/pull/5137))
- `account_lines`: Fixed error with `no_ripple` and `no_ripple_peer` sometimes showing up incorrectly. ([#5345](https://github.com/XRPLF/rippled/pull/5345))
- `account_tx`: Fixed issue with incorrect CTIDs. ([#5408](https://github.com/XRPLF/rippled/pull/5408))

## XRP Ledger server version 2.4.0

[Version 2.4.0](https://github.com/XRPLF/rippled/releases/tag/2.4.0) was released on March 4, 2025.

### Additions and bugfixes in 2.4.0

- `simulate`: A new RPC that executes a [dry run of a transaction submission](https://github.com/XRPLF/XRPL-Standards/tree/master/XLS-0069d-simulate#2-rpc-simulate). ([#5069](https://github.com/XRPLF/rippled/pull/5069))
- Signing methods (`sign`, `sign_for`, `submit`): Autofill fees better, properly handle transactions without a base fee, and autofill the `NetworkID` field. ([#5069](https://github.com/XRPLF/rippled/pull/5069))
- `ledger_entry`: `state` is added as an alias for `ripple_state`. ([#5199](https://github.com/XRPLF/rippled/pull/5199))
- `ledger`, `ledger_data`, `account_objects`: Support filtering ledger entry types by their canonical names (case-insensitive). ([#5271](https://github.com/XRPLF/rippled/pull/5271))
- `validators`: Added new field `validator_list_threshold` in response. ([#5112](https://github.com/XRPLF/rippled/pull/5112))
- `server_info`: Added git commit hash info on admin connection. ([#5225](https://github.com/XRPLF/rippled/pull/5225))
- `server_definitions`: Changed larger `UInt` serialized types to `Hash`. ([#5231](https://github.com/XRPLF/rippled/pull/5231))

## XRP Ledger server version 2.3.1

[Version 2.3.1](https://github.com/XRPLF/rippled/releases/tag/2.3.1) was released on Jan 29, 2025.

This release contains bug fixes only and no API changes.

## XRP Ledger server version 2.3.0

[Version 2.3.0](https://github.com/XRPLF/rippled/releases/tag/2.3.0) was released on Nov 25, 2024.

### Breaking changes in 2.3.0

- `book_changes`: If the requested ledger version is not available on this node, a `ledgerNotFound` error is returned and the node does not attempt to acquire the ledger from the p2p network (as with other non-admin RPCs). Admins can still attempt to retrieve old ledgers with the `ledger_request` RPC.

### Additions and bugfixes in 2.3.0

- `book_changes`: Returns a `validated` field in its response. ([#5096](https://github.com/XRPLF/rippled/pull/5096))
- `book_changes`: Accepts shortcut strings (`current`, `closed`, `validated`) for the `ledger_index` parameter. ([#5096](https://github.com/XRPLF/rippled/pull/5096))
- `server_definitions`: Include `index` in response. ([#5190](https://github.com/XRPLF/rippled/pull/5190))
- `account_nfts`: Fix issue where unassociated marker would return incorrect results. ([#5045](https://github.com/XRPLF/rippled/pull/5045))
- `account_objects`: Fix issue where invalid marker would not return an error. ([#5046](https://github.com/XRPLF/rippled/pull/5046))
- `account_objects`: Disallow filtering by ledger entry types that an account cannot hold. ([#5056](https://github.com/XRPLF/rippled/pull/5056))
- `tx`: Allow lowercase CTID. ([#5049](https://github.com/XRPLF/rippled/pull/5049))
- `feature`: Better error handling for invalid values of `feature`. ([#5063](https://github.com/XRPLF/rippled/pull/5063))

## XRP Ledger server version 2.2.0

[Version 2.2.0](https://github.com/XRPLF/rippled/releases/tag/2.2.0) was released on Jun 5, 2024. The following additions are non-breaking (because they are purely additive):

- `feature`: Add a non-admin mode for users. (It was previously only available to admin connections.) The method returns an updated list of amendments, including their names and other information. ([#4781](https://github.com/XRPLF/rippled/pull/4781))

## XRP Ledger server version 2.0.1

[Version 2.0.1](https://github.com/XRPLF/rippled/releases/tag/2.0.1) was released on Jan 29, 2024. The following additions are non-breaking:

- `path_find`: Fixes unbounded memory growth. ([#4822](https://github.com/XRPLF/rippled/pull/4822))

## XRP Ledger server version 2.0.0

[Version 2.0.0](https://github.com/XRPLF/rippled/releases/tag/2.0.0) was released on Jan 9, 2024. The following additions are non-breaking (because they are purely additive):

- `server_definitions`: A new RPC that generates a `definitions.json`-like output that can be used in XRPL libraries.
- In `Payment` transactions, `DeliverMax` has been added. This is a replacement for the `Amount` field, which should not be used. Typically, the `delivered_amount` (in transaction metadata) should be used. To ease the transition, `DeliverMax` is present regardless of API version, since adding a field is non-breaking.
- API version 2 has been moved from beta to supported, meaning that it is generally available (regardless of the `beta_rpc_api` setting). The full list of changes is in [API-VERSION-2.md](API-VERSION-2.md).

## XRP Ledger server version 1.12.0

[Version 1.12.0](https://github.com/XRPLF/rippled/releases/tag/1.12.0) was released on Sep 6, 2023. The following additions are non-breaking (because they are purely additive):

- `server_info`: Added `ports`, an array which advertises the RPC and WebSocket ports. This information is also included in the `/crawl` endpoint (which calls `server_info` internally). `grpc` and `peer` ports are also included. ([#4427](https://github.com/XRPLF/rippled/pull/4427))
  - `ports` contains objects, each containing a `port` for the listening port (a number string), and a `protocol` array listing the supported protocols on that port.
  - This allows crawlers to build a more detailed topology without needing to port-scan nodes.
  - (For peers and other non-admin clients, the info about admin ports is excluded.)
- Clawback: The following additions are gated by the Clawback amendment (`featureClawback`). ([#4553](https://github.com/XRPLF/rippled/pull/4553))
  - Adds an [AccountRoot flag](https://xrpl.org/accountroot.html#accountroot-flags) called `lsfAllowTrustLineClawback`. ([#4617](https://github.com/XRPLF/rippled/pull/4617))
    - Adds the corresponding `asfAllowTrustLineClawback` [AccountSet Flag](https://xrpl.org/accountset.html#accountset-flags) as well.
    - Clawback is disabled by default, so if an issuer desires the ability to claw back funds, they must use an `AccountSet` transaction to set the AllowTrustLineClawback flag. They must do this before creating any trust lines, offers, escrows, payment channels, or checks.
  - Adds the [Clawback transaction type](https://github.com/XRPLF/XRPL-Standards/blob/master/XLS-39d-clawback/README.md#331-clawback-transaction), containing these fields:
    - `Account`: The issuer of the asset being clawed back. Must also be the sender of the transaction.
    - `Amount`: The amount being clawed back, with the `Amount.issuer` being the token holder's address.
- Adds [AMM](https://github.com/XRPLF/XRPL-Standards/discussions/78) ([#4294](https://github.com/XRPLF/rippled/pull/4294), [#4626](https://github.com/XRPLF/rippled/pull/4626)) feature:
  - Adds `amm_info` API to retrieve AMM information for a given tokens pair.
  - Adds `AMMCreate` transaction type to create `AMM` instance.
  - Adds `AMMDeposit` transaction type to deposit funds into `AMM` instance.
  - Adds `AMMWithdraw` transaction type to withdraw funds from `AMM` instance.
  - Adds `AMMVote` transaction type to vote for the trading fee of `AMM` instance.
  - Adds `AMMBid` transaction type to bid for the Auction Slot of `AMM` instance.
  - Adds `AMMDelete` transaction type to delete `AMM` instance.
  - Adds `sfAMMID` to `AccountRoot` to indicate that the account is `AMM`'s account. `AMMID` is used to fetch `ltAMM`.
  - Adds `lsfAMMNode` `TrustLine` flag to indicate that one side of the `TrustLine` is `AMM` account.
  - Adds `tfLPToken`, `tfSingleAsset`, `tfTwoAsset`, `tfOneAssetLPToken`, `tfLimitLPToken`, `tfTwoAssetIfEmpty`,
    `tfWithdrawAll`, `tfOneAssetWithdrawAll` which allow a trader to specify different fields combination
    for `AMMDeposit` and `AMMWithdraw` transactions.
  - Adds new transaction result codes:
    - tecUNFUNDED_AMM: insufficient balance to fund AMM. The account does not have funds for liquidity provision.
    - tecAMM_BALANCE: AMM has invalid balance. Calculated balances greater than the current pool balances.
    - tecAMM_FAILED: AMM transaction failed. Fails due to a processing failure.
    - tecAMM_INVALID_TOKENS: AMM invalid LP tokens. Invalid input values, format, or calculated values.
    - tecAMM_EMPTY: AMM is in empty state. Transaction requires AMM in non-empty state (LP tokens > 0).
    - tecAMM_NOT_EMPTY: AMM is not in empty state. Transaction requires AMM in empty state (LP tokens == 0).
    - tecAMM_ACCOUNT: AMM account. Clawback of AMM account.
    - tecINCOMPLETE: Some work was completed, but more submissions required to finish. AMMDelete partially deletes the trustlines.

## XRP Ledger server version 1.11.0

[Version 1.11.0](https://github.com/XRPLF/rippled/releases/tag/1.11.0) was released on Jun 20, 2023.

### Breaking changes in 1.11

- Added the ability to mark amendments as obsolete. For the `feature` admin API, there is a new possible value for the `vetoed` field. ([#4291](https://github.com/XRPLF/rippled/pull/4291))
  - The value of `vetoed` can now be `true`, `false`, or `"Obsolete"`.
- Removed the acceptance of seeds or public keys in place of account addresses. ([#4404](https://github.com/XRPLF/rippled/pull/4404))
  - This simplifies the API and encourages better security practices (i.e. seeds should never be sent over the network).
- For the `ledger_data` method, when all entries are filtered out, the `state` field of the response is now an empty list (in other words, an empty array, `[]`). (Previously, it would return `null`.) While this is technically a breaking change, the new behavior is consistent with the documentation, so this is considered only a bug fix. ([#4398](https://github.com/XRPLF/rippled/pull/4398))
- If and when the `fixNFTokenRemint` amendment activates, there will be a new AccountRoot field, `FirstNFTSequence`. This field is set to the current account sequence when the account issues their first NFT. If an account has not issued any NFTs, then the field is not set. ([#4406](https://github.com/XRPLF/rippled/pull/4406))
  - There is a new account deletion restriction: an account can only be deleted if `FirstNFTSequence` + `MintedNFTokens` + `256` is less than the current ledger sequence.
  - This is potentially a breaking change if clients have logic for determining whether an account can be deleted.
- NetworkID
  - For sidechains and networks with a network ID greater than 1024, there is a new [transaction common field](https://xrpl.org/transaction-common-fields.html), `NetworkID`. ([#4370](https://github.com/XRPLF/rippled/pull/4370))
    - This field helps to prevent replay attacks and is now required for chains whose network ID is 1025 or higher.
    - The field must be omitted for Mainnet, so there is no change for Mainnet users.
  - There are three new local error codes:
    - `telNETWORK_ID_MAKES_TX_NON_CANONICAL`: a `NetworkID` is present but the chain's network ID is less than 1025. Remove the field from the transaction, and try again.
    - `telREQUIRES_NETWORK_ID`: a `NetworkID` is required, but is not present. Add the field to the transaction, and try again.
    - `telWRONG_NETWORK`: a `NetworkID` is specified, but it is for a different network. Submit the transaction to a different server which is connected to the correct network.

### Additions and bug fixes in 1.11

- Added `nftoken_id`, `nftoken_ids` and `offer_id` meta fields into NFT `tx` and `account_tx` responses. ([#4447](https://github.com/XRPLF/rippled/pull/4447))
- Added an `account_flags` object to the `account_info` method response. ([#4459](https://github.com/XRPLF/rippled/pull/4459))
- Added `NFTokenPages` to the `account_objects` RPC. ([#4352](https://github.com/XRPLF/rippled/pull/4352))
- Fixed: `marker` returned from the `account_lines` command would not work on subsequent commands. ([#4361](https://github.com/XRPLF/rippled/pull/4361))

## XRP Ledger server version 1.10.0

[Version 1.10.0](https://github.com/XRPLF/rippled/releases/tag/1.10.0)
was released on Mar 14, 2023.

### Breaking changes in 1.10

- If the `XRPFees` feature is enabled, the `fee_ref` field will be
  removed from the [ledger subscription stream](https://xrpl.org/subscribe.html#ledger-stream), because it will no longer
  have any meaning.

# Unit tests for API changes

The following information is useful to developers contributing to this project:

The purpose of unit tests is to catch bugs and prevent regressions. In general, it often makes sense to create a test function when there is a breaking change to the API. For APIs that have changed in a new API version, the tests should be modified so that both the prior version and the new version are properly tested.

To take one example: for `account_info` version 1, WebSocket and JSON-RPC behavior should be tested. The latest API version, i.e. API version 2, should be tested over WebSocket, JSON-RPC, and command line.
