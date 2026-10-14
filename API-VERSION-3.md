# API Version 3

API version 3 is currently a **beta API**. It requires `[beta_rpc_api]` set to `1` in the xrpld configuration to use; the section takes one value line, and a bare header leaves the flag off. To use this API, clients specify `"api_version" : 3` in each request.

For info about how [API versioning](https://xrpl.org/request-formatting.html#api-versioning) works, including examples, please view the [XLS-22d spec](https://github.com/XRPLF/XRPL-Standards/discussions/54). For details about the implementation of API versioning, view the [implementation PR](https://github.com/XRPLF/rippled/pull/3155). API versioning ensures existing integrations and users continue to receive existing behavior, while those that request a higher API version will experience new behavior.

## Breaking Changes

### Replies take the JSON-RPC 2.0 envelope

Every reply the server can attribute to API version 3 takes the envelope the [JSON-RPC 2.0 specification](https://www.jsonrpc.org/specification) defines, on both the JSON-RPC and WebSocket transports. The Deviations section below lists where the content departs from the specification, and the rejections that stay a plain text body. This replaces the three envelope shapes selected by the `ripplerpc` request field, none of which conformed to the specification that the field is named after.

A successful reply carries the payload under `result`:

```json
{
  "jsonrpc": "2.0",
  "id": 7,
  "result": { "ledger_hash": "F742...", "ledger_index": 2 }
}
```

A failure carries it under `error` instead:

```json
{
  "jsonrpc": "2.0",
  "id": 7,
  "error": {
    "code": -32000,
    "message": "Account malformed.",
    "data": { "error": "actMalformed", "error_code": 35 }
  }
}
```

Compared with earlier versions:

- **`jsonrpc` is always present** and names the protocol version.
- **`id` is echoed from the top level of the request**, where the specification puts it. This changes the JSON-RPC transport only. Earlier versions of that transport echoed `id` only when it was nested inside `params`, so a client that placed it correctly received no `id` and could not correlate the reply with its request. A WebSocket session has always echoed a top-level `id`, and is unaffected. A request that names no `id` receives `"id": null`, so that a reply is always distinguishable from a notification; the specification would send nothing, which the Deviations section below records. The specification allows a string, a number or null there, so a request naming anything else there, an object, an array or a boolean, is rejected as an invalid request, on both transports; that reply names `"id": null` too, the id being the thing that was wrong. A number is read as this server reads every JSON number: an integer from -2147483648 to 4294967295, or a fraction. An integer outside that range fails to parse, and the body is refused as unreadable before any envelope is chosen, so a client using a millisecond timestamp as an `id` should send it as a string; the Deviations section below records the bound. Earlier versions echo whatever they are given, whatever its shape, and continue to.
- **`status` is gone from the reply envelope**, at both levels. Whether the call succeeded is already carried by which of `result` or `error` is present. Earlier versions also let `path_find`'s `status` subcommand report one inside `result`; that one is dropped too. A subscription notification is not a reply and keeps whatever the event carries: the `transactions` stream reports `"closed"` or `"proposed"` in a `status` member of the event itself, so a version 3 subscriber still receives it under `params`.
- **`error.code` is `-32000`** for any request that reaches a command, the specification's reserved code for an implementation-defined server error. The XRPL error token and numeric code move into `error.data`, so the two code spaces stay separate and renumbering an XRPL error can never change a JSON-RPC code. Everything else the handler reported about the failure also travels in `data`. A request naming a method this server does not have reaches no command, and the specification gives that one condition a code of its own, so it reports `-32601` instead, with `unknownCmd` and its code still under `data`.
- **A notice stays at the top level.** Three member names are notices: `warning`, `warnings` and `deprecated`. The rule is positional, not a judgment about what each one says: any of the three that a handler puts at the **top level of its result** is moved beside the specification's members, and one nested deeper is left where the handler put it. So `ledger`'s `warnings` is moved, while `server_info`'s, nested under `info`, and `server_state`'s, nested under `state`, are not. Two of the three moved members do describe one outcome rather than the server: `wallet_propose`'s `warning` describes the wallet the call just produced, and `subscribe`'s describes an experimental feature. Both are moved, because they sit at that top level.
- **`error_exception` is gone; its text is `error.message`.** `submit` and `simulate` reported the specific detail of a failure in an `error_exception` member of their own, leaving a generic `error_message` beside it: a `tx_blob` that fails to parse reported both `"Transaction is invalid."` and `"Transaction length invalid"`. The specific text is now the `message`, which is where every other handler puts it, and the token and code in `data` still say what kind of failure it was. A client that read `error_exception` must read `error.message`.
- **The request is not echoed back.** Earlier versions echoed it on version 1 errors. A request rejected before it reaches a command is not echoed either, and the shape its reply takes is described below.
- **`ripplerpc` is ignored.** It no longer selects the envelope. The field is ignored rather than rejected, as any other field the server does not honor is.

The envelope above describes a request that reaches a command. A request the server rejects before then (one naming no method, or a method that is not a string, or one refused for permissions or load) is answered differently, since there is no handler result to shape. One case has no reply at all: a WebSocket sender over the resource drop threshold is disconnected, whatever its message carried, as it has been at every version, while the JSON-RPC transport answers that sender with 503 and `Server is overloaded`. A request whose `api_version`, where the server reads it, names a version it does not serve is rejected before then too, but is answered outside the envelope, as plain text on the JSON-RPC transport and in the legacy frame on WebSocket, unless it is an entry of a batch, as described below: the server cannot tell which envelope that client speaks. That place is inside `params` for a lone JSON-RPC request, a top-level value naming such a version being ignored there and the request served as version 1, as the request section below describes, and the top level of a WebSocket message.

Such a rejection is a specification error object, whether the request was sent alone or as an entry in a batch, correlated by the request's own `id`, or naming `"id": null` where the `id` itself was the unusable member:

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "error": { "code": -32602, "message": "params unparsable" }
}
```

The HTTP status is the one the rejection has always reported: 400 for a malformed request, 403 for a forbidden one and 503 for an overloaded server. Earlier versions receive that status with the message as a bare text body (`Null method`, `params unparsable`), and continue to. A request sent as an entry of a batch reports no status of its own, the body carrying several outcomes; the batch section below says where its outcome is stated instead.

The rejected request is **not** echoed back. Earlier versions echo a rejected batch entry with its credentials masked and continue to do so; the specification makes `error.data` optional, a client already knows what it sent, and copying a whole entry per rejection is what an overloaded server must not do.

The codes reported here are the specification's own: `-32600` for a request that names no usable `method`, or is not a JSON object at all, and `-32602` for invalid parameters. Earlier versions report `-32601`, method not found, for such an entry of a `"method": "batch"` request, where version 3 reports `-32600`; a lone request there is answered as plain text, with no code at all. `-32601` is what they have always sent for the entry and moving it would change `error.code` for their clients. Version 3 keeps `-32601` for the one condition the specification defines it for, a request naming a method this server does not have, which is answered from the reply path above rather than as a rejection: a request naming no usable method names nothing that could be found or not found. A request naming one method at its top level and another inside `params` names two, which is not that condition either, so it reports `-32600` from version 3 up on the JSON-RPC transport, the WebSocket transport having no `params` to disagree with, its `command` and `method` disagreement being described below; earlier versions of the JSON-RPC transport answer it as `unknownCmd` with `error_code` 32 at HTTP 200 in the `ripplerpc` `"1.0"` and `"2.0"` envelopes, and at 405 in the `"3.0"` envelope, and continue to. They also report `-32604` for an overloaded server, `-32605` for a forbidden request and `-32606` for an unsupported API version, which lie in the range the specification reserves for the protocol rather than the `-32000..-32099` range it leaves to implementations. Those are unchanged at every version, for the same reason.

Those three codes belong to a rejection the server makes before it dispatches a request. Load and permission can also be reported once a request has been read, and then they carry an XRPL token and so report `-32000` like any other: a request refused because the job queue is full reports `-32000` with `tooBusy` under `data` and HTTP 503, and an admin-only command called without the role reports `-32000` with `noPermission` and HTTP 401. The rule is not which subject the failure is about: **every error carrying an XRPL token reports `-32000`, with the one exception of `unknownCmd`, which reports `-32601`.**

On the JSON-RPC transport three classes of rejection are answered as plain text at every version, including this one, because the server cannot tell which envelope the client speaks:

- a body it could not parse, and one larger than the request size limit;
- a body that parses but carries no request: `{}` or `null`;
- a request whose `api_version`, read from inside `params` for a lone request, names a version the server does not serve, unless it is an entry of a batch.

The first two name no version anywhere. The third names one, but not one this server has, so it cannot know what that client expects. A lone request naming such a version at its top level only is not in the third: that value is ignored, as the request section below describes, and the request is served as version 1. A version 3 client must therefore tolerate a non-JSON body for these. A top-level value that is neither an object nor an array, such as `42` or `"text"`, does not parse at all and falls in the first class.

The WebSocket transport never answers plain text. A frame the server cannot read is answered with the `jsonInvalid` frame described below, and a frame naming an `api_version` the server does not serve is answered in the legacy envelope those versions have always used: `{"type": "response", "status": "error", "error": "invalid_API_version", "request": { ... }}`, with the request echoed and its credentials masked. That is the reply a version 3 WebSocket client receives from a server where `[beta_rpc_api]` is not enabled.

An entry of a top-level array is the exception to the third, because the array's own version is known: only a version 3 client sends an array at all. Such an entry is answered with a `-32606` error object in the array's envelope, described in the batch section below. An entry of a `"method": "batch"` request is answered the same way when the body is served at version 3, which it is when the body or any of its entries names it; a body served below version 3 keeps the shape earlier versions send.

One rejection is worth naming separately. On the WebSocket transport there are four ways for a request to name no one command to dispatch on: it names neither `command` nor `method`, one of those two is not a string, or the two disagree. From version 3 each is answered in the specification envelope, reporting `-32600` and a message naming which of the four it was. No such reply carries `data`: a rejection is not a failure a command reported, so it has no XRPL token to put there. Earlier versions answer all four with the single `missingCommand` token, and continue to.

The HTTP status still derives from the XRPL error code, now read from `error.data.error_code`. Four codes named no status in the error table and fell back to 200, which said the call succeeded on a reply that carries an error: `account_info` answered both a malformed account and a missing one with 200. They now name the status the table gives every comparable code: `actMalformed`, `alreadyMultisig` and `alreadySingleSig` report 400, and `actNotFound` reports 404.

Only version 3 reports those four. The legacy `ripplerpc: "3.0"` envelope derives the status from the same code and still answers 200 for them, since that is the status it has answered since it shipped and `account_info` on an account the ledger does not hold is a routine call rather than a failure. Every other code reports the status it always has, in both envelopes.

On the WebSocket transport the reply carries one extra member, `"type": "response"`. The specification has no place for it, but a WebSocket session also receives server-initiated messages on the same connection, and `type` is how a client tells the two apart. It is retained for that reason.

A WebSocket frame that does not parse as a JSON object, or exceeds the request size limit, is answered `{"type": "error", "error": "jsonInvalid", "size": <bytes>}` at every API version, including this one, unless the sender is already over the resource drop threshold, in which case the frame is charged and the connection is closed without a reply, as it is for a readable message from such a sender. There are no fields yet to shape into an envelope, and none to mask either, so the body is reported by its size rather than echoed: a frame with a stray comma would otherwise return whatever it carried, credentials included.

### Requests may take the specification's shape

Two request spellings are now accepted on the JSON-RPC transport, in addition to the ones XRPL clients already send: one the specification defines, `params` as an object, and one XRPL extension placed beside the members the specification defines, `api_version` at the top level. Both are additive, so every request that worked before still works; one shape is read differently, and it is named below. A WebSocket message is one flat object whose members are the parameters themselves: it has always carried `api_version` at its top level, and a `params` member there is not unpacked, so neither spelling applies to it.

- **`api_version` at the top level**, beside `method` and `id`, and not only inside `params`. For a lone JSON-RPC request a top-level value is honored only when it names version 3 or above: `"api_version": 2` there has been answered as version 1 for years, as has a version the server does not serve, and both continue to be. An entry of either batch form honors any version it names at its top level, including one the server does not serve, which is answered as an unsupported version: a `"method": "batch"` entry carries its parameters at its own top level, so that is where its version sits, and one that carries a by-name `params` object is read for `api_version` and credentials from that object instead; an array entry is read the same way so that the two forms agree, and an entry naming a served version other than the body's refuses the body.

  A value inside `params` decides where a request carries both, which **reverses the earlier precedence**: a request that named a version inside `params` and a different one at its top level was previously answered at the top-level one whenever the value inside `params` resolved to version 1. Only a `"method": "batch"` entry could carry both, so that is the only shape the reversal reaches, and it reaches it at every API version. `API-CHANGELOG.md` records it.

- **`params` as the object itself**, the specification's by-name form, rather than an array holding that object. The array form is unchanged: it holds exactly one element, which is the object or `null`, as the Deviations section below records.

#### One API version per connection is the shape to write a client to

The server reads `api_version` from each request and answers that request at that version, so a connection may carry requests at several versions and each reply is shaped for the request that asked for it. Nothing refuses that. But there is no reason for a client to rely on it: a client that names one version for the life of a connection reads one reply shape throughout, and has one thing to change when it migrates. Write a client that way.

One case is not advice but a rule the server enforces: **every subscription a connection holds must name the same `api_version`**. A `subscribe` naming a version different from the one the connection's subscriptions are served at is refused with `apiVersionConflict` and registers nothing, because one event can match several of a connection's subscriptions and only one message goes out, so a second version would leave no shape that message could take. The version is fixed by the first `subscribe` for the connection's life, whether or not that call went on to register anything: a `subscribe` refused for a stream name the server does not have has still fixed it. A client that wants another version opens another connection.

A webhook subscriber is keyed on its `url` alone, so the same rule reaches every caller that names that url. To subscribe a url at a different version, send `unsubscribe` naming that `url` **and its streams**: naming the url alone does not release it, since the subscriber is not evicted while any stream map still holds it. Be aware that doing so removes the subscription of whoever subscribed that url first, whether or not that was you - the server keeps no record of which caller named it. Where the url may be shared, naming a different url is the safer answer. The refusal message names the version the url's subscriptions are served at and no remedy, for that reason. Ordinary requests are untouched by this: a connection subscribed at version 3 still calls `account_info` at version 1 and reads the version 1 shape.

```json
{
  "jsonrpc": "2.0",
  "id": 7,
  "method": "ledger_closed",
  "api_version": 3,
  "params": { "ledger_index": "validated" }
}
```

### Batch requests

A top-level JSON array is accepted as a [batch](https://www.jsonrpc.org/specification#batch), and is answered with an array of replies, one per entry, each correlated by its own `id`:

```json
[
  {
    "jsonrpc": "2.0",
    "id": 1,
    "method": "ledger_closed",
    "params": [{ "api_version": 3 }]
  },
  {
    "jsonrpc": "2.0",
    "id": 2,
    "method": "account_info",
    "params": [{ "api_version": 3 }]
  }
]
```

In earlier versions a top-level array is rejected with HTTP 400, and that is unchanged; an array is treated as a batch only when an entry names an `api_version` of 3 or above that the server serves, inside its own `params` or at its top level, wherever that entry sits; an array holding at least one object in which no entry does is refused whole: as below version 3 where an entry names a served version below 3 or no entry names any, and for the version named where every version named is one the server does not serve. An array holding no object at all is answered per entry instead, as below. That entry also sets the version for the array: an entry naming none of its own inherits it, whether it comes before or after, so one batch is answered in one envelope even where an entry is rejected before its parameters can be read. An entry that names a served version of its own must name that one; a body whose entries name two different served versions is refused whole, as the table below shows. An entry naming a version the server does not serve is answered on its own, as below.

An entry's parameters are nested exactly as a lone request's are, so an entry reports the same error it would have reported on its own.

An entry that is not a JSON object is not a request. It is answered in place, among the replies, with `-32600` and `Request is not a JSON object`, and the entries that are requests are still dispatched. Earlier versions answer such an entry with `-32601` and `Method not found`, and echo it back under `request`; both are unchanged for them.

An entry naming an `api_version` the server does not serve is answered the same way: in place, with `-32606` and `invalid_API_version`, correlated by the entry's own `id`, or naming `"id": null` where the `id` itself was unusable, and echoing nothing. It takes the array's envelope rather than the one it asked for, since the version it asked for does not exist, and unlike a lone request in that position it is not answered as plain text: the array told the server which envelope this client reads. An entry naming a version the server does have must name the array's, or the whole array is refused.

A rejection of the whole array is answered with one error object, since only version 3 sends an array at all. The array names no `id`, its entries do, so `id` is null. The version is set by the first entry naming a served version of 3 or above. The version rows below are read in order: the first applies where every version named is one the server does not serve, the second where no entry names a served version of 3 or above, and the third where an entry names a served version other than the one set. An entry naming a version the server does not serve beside one naming a served version is answered on its own, wherever it sits, as above:

| Body                                                                                    | HTTP | `error.code` | `error.message`                         |
| --------------------------------------------------------------------------------------- | ---- | ------------ | --------------------------------------- |
| `[]`                                                                                    | 400  | `-32600`     | `Request is empty`                      |
| More than 100 entries                                                                   | 400  | `-32600`     | `Batch has too many entries`            |
| Every version named is one the server does not serve                                    | 400  | `-32606`     | `invalid_API_version`                   |
| Otherwise, no entry names a served version of 3 or above                                | 400  | `-32600`     | `Batch requires API version 3 or above` |
| An entry names a served version of 3 or above, and another names a different served one | 400  | `-32600`     | `Batch entries name different versions` |

A non-empty array of 100 entries or fewer holding nothing that could be a request, such as `[1,2,3]` from the specification's own example, is answered with one `-32600` object per entry, since no entry can name a version or an `id`. The table's first two rows are checked before any entry is read, so an empty or oversized array is answered once, whatever it holds.

A batch is limited to 100 entries, and the limit is applied before the entries are read, so an oversized array is answered once rather than once per entry. The body size limit alone is not a bound on the work a batch buys, since it dispatches one command per entry.

The `"method": "batch"` form, which nests its entries under `params`, is unrelated to the specification and continues to work on every API version. From version 3 up it takes the same 100-entry limit, read from the one version the body is served at, so no ordering of the entries evades it. Below version 3 it stays uncapped, which is what keeps every body clients send today acceptable.

Every entry is charged against the sender's resource allowance, whether it reaches a handler or is rejected first, except the one shed as overloaded, whose load the drop check has already accounted for. Once a connection is over the drop threshold the remaining entries are not answered, so a reply array can be shorter than the request array. The last element depends on where the batch stopped: an entry that is not a JSON object, or names an API version the server does not serve, is charged before its role is known and answered with its own error object, and the batch stops after it when that charge crossed the threshold; every other entry met over the threshold reports `-32604` and `Server is overloaded` in its own error object. Nothing follows in either case, so a client that correlates by `id` sees answers missing for the entries at the end, and should read any short reply array as an overloaded connection whatever its last element says. This applies to both batch forms.

A batch whose entries are answered reports HTTP 200, whatever those entries report, including the overloaded one above. An array holding nothing that could be a request is the exception: its entries are answered one by one, but the body is a rejection rather than a batch the server served, and it reports 400. One status cannot describe several outcomes, so an entry's own reply is where its outcome is stated: whether it carries `result` or `error`, and, for an error, the specification code in `error.code` for a rejection and the XRPL token and code under `error.data` for a handler failure, every one of which reports `-32000` in `error.code`. No entry has an HTTP status of its own. A rejection of the whole array is one answer rather than many and reports its own status, as the table above shows, and so does a request sent alone.

### Subscription messages are notifications

A message the server sends without being asked, such as a `subscribe` stream event, is shaped as a specification [notification](https://www.jsonrpc.org/specification#notification): a request-shaped object naming the protocol and the method, with the event's content under `params` and no `id`.

```json
{
  "jsonrpc": "2.0",
  "method": "ledgerClosed",
  "params": { "ledger_index": 3, "...": "..." }
}
```

The `type` member that named the event in earlier versions becomes `method`.

A `path_find` update is a notification too, though the server directs it at the one client that asked rather than publishing it to a stream. The specification allows one response per request, and `path_find create` already consumed it, so no later update can be one. The `id` that correlates the update with the request travels inside `params`, which is where the update has always carried it:

```json
{
  "jsonrpc": "2.0",
  "method": "path_find",
  "params": { "id": 7, "alternatives": ["..."], "full_reply": true }
}
```

An `account_history` subscription that fails reports the failure the same way, naming `account_history_tx_stream` as the method with the error under `params`. Earlier versions receive a bare error object with no `type`, which is what they have always received.

A subscriber named by a `url` is the exception, and receives the legacy `event` call at every version, including version 3. Such a subscriber is not sent the message directly: the server posts each message to the URL as the `params` of an `event` call, with a `seq` member counting the messages beside it. A notification sent that way would arrive nested inside a request and next to a member the specification does not define, so it would not be a notification. Which shape a subscriber receives therefore depends on how the message reaches it, not only on the version it asked for.

### Deviations from the specification

The reply follows the specification as closely as the XRPL API allows. These are the places it does not, all of them deliberate:

- **A request that names no `id` still receives a reply.** The specification calls such a request a notification and gives it no reply at all. XRPL clients routinely omit `id`, so a reply is always sent, with `"id": null`.
- **A batch of such requests is answered with an array**, for the same reason, where the specification would send no body.
- **An integer `id` must fit this server's JSON integer range**, -2147483648 to 4294967295. The specification allows any Number. One outside that range fails to parse, and the body is refused as unreadable before any envelope is chosen, so a client using a larger value, such as a millisecond timestamp, should send it as a string.
- **By-position `params` is an array of exactly one element, an object or `null`.** The specification allows an array of any length. This server's methods take one object, so an array of any other length, or one whose element is neither an object nor `null`, is refused with `-32602` and `params unparsable`. A `null` element is served as an empty parameter object, as it has been at every version.
- **`error.code` is `-32000`** for every failure a command reports, rather than a code per failure. The XRPL token and code carry that detail in `error.data`. A method the server does not have is the one exception, reporting `-32601`, because no command runs for it.
- **`-32700` (parse error) and `-32603` (internal error) are never reported.** A body that does not parse is answered as plain text on the JSON-RPC transport and as a `jsonInvalid` frame on WebSocket, and neither is an envelope, so there is no `error.code` to put a parse code in. An internal failure of a command is reported the way every other failure a command reports is, with `-32000` and the `internal` token under `error.data`, so that a client reads one code space for all of them.
- **`-32604`, `-32605` and `-32606`** lie in the band the specification reserves for the protocol. They are the codes earlier versions report for an overloaded server, a forbidden request and an unsupported API version, and moving one would change `error.code` for their clients.
- **A rejection the server cannot attribute to a version is plain text**, not an error object. The three classes are listed above.
- **The batch is accepted on the JSON-RPC transport only.** A WebSocket frame must be a single object, as it always has been.
- **`ripplerpc` is ignored** rather than rejected, as any other field the server does not honor is. When support for versions 1 and 2 ends, an ignored field stays ignored, where a rejection would silently turn into acceptance.
- **A WebSocket reply carries `"type": "response"`**, which the specification has no place for. A WebSocket session receives server-initiated messages on the same connection, and `type` is how a client tells the two apart.
- **A batch is served at one API version.** The specification says nothing about versions. An entry may name the version the body names, or name none and inherit it, and a body whose entries name two different versions the server serves is refused whole with `-32600` and `Batch entries name different versions`; one naming a version the server does not serve is answered on its own, as below. For a top-level array it is the one the first entry naming a served version of 3 or above does, wherever that entry sits. For a `"method": "batch"` body it is the one the first entry naming a served version does; such a body may also name it at its own top level, which then decides, and one naming a version the server does not serve there is refused. An entry names a version wherever dispatch would read one from it, inside its own `params` or at its top level. An entry naming a version the server does not serve is answered on its own, as it always has been, rather than taking the body down with it, except that a top-level array holding at least one object in which no entry names a served version of 3 or above is refused whole, with `-32606` where every version named is one the server does not serve and with `-32600` otherwise, as the table above shows, while one holding no object at all is answered per entry once it has passed the empty and size checks of the table's first two rows.
- **A reply array can be shorter than the request array.** The specification asks for one response per non-notification request; a batch stops at the entry the connection is too loaded to serve. `API-CHANGELOG.md` records this for the `"method": "batch"` form, which earlier versions serve; the array form is version 3 only, and this document is its record.
- **`warning`, `warnings` and `deprecated` sit beside `jsonrpc`, `result` and `id`** at the top level of the reply, where the specification defines no member of its own. The rule is positional: any of the three that a handler puts at the top level of its result is moved there, whatever it describes, and one nested deeper stays where the handler put it. A client therefore reads a top-level notice from one place whether the call succeeded or failed.
- **A subscriber named by a `url` receives the legacy `event` call**, not a notification, at every version. The transport that carries the message to the URL wraps it in a request of its own, so a notification could not survive the trip. See above.
- **A batch is limited to 100 entries.** The specification places no bound on an array's length. One command is dispatched per entry, so the request size limit bounds the bytes rather than the work, and an array of small entries buys far more of it than its size suggests. The same limit applies to the `"method": "batch"` form from version 3 up, for the reason given above.
- **A `subscribe` naming an `api_version` different from the one this connection's subscriptions are served at is refused.** The specification describes no such condition. Every subscription a connection holds is served at one version, for the reason given above, which is also why the remedy is a second connection rather than a second version. The refusal reports `apiVersionConflict`, registers nothing, and names the version already established. On the JSON-RPC transport it answers HTTP 400, as every version 3 error reporting a malformed request does. A WebSocket frame carries no status. `API-CHANGELOG.md` records it as breaking. This constrains `subscribe` alone: an ordinary request names its own `api_version` and is answered at it, whatever the connection has subscribed.
- **The request's own `jsonrpc` member is neither required nor read.** The specification asks a client to send `"jsonrpc": "2.0"`. A request is served whether it names that member, names some other value, or names none at all: `api_version` is what selects the shape of the reply, and refusing an otherwise well formed call for a missing member would reject requests every XRPL client sends today. The reply always names the member.

### Modifications to `amm_info`

The order of error checks has been changed to provide more specific error messages. ([#4924](https://github.com/XRPLF/rippled/pull/4924))

- **Before (API v2)**: When sending an invalid account or asset to `amm_info` while other parameters are not set as expected, the method returns a generic `rpcINVALID_PARAMS` error.
- **After (API v3)**: The same scenario returns a more specific error: `rpcISSUE_MALFORMED` for malformed assets or `rpcACT_MALFORMED` for malformed accounts.

### Modifications to `ledger_entry`

Added support for string shortcuts to look up fixed-location ledger entries using the `"index"` parameter. ([#5644](https://github.com/XRPLF/rippled/pull/5644))

In API version 3, the following string values can be used with the `"index"` parameter:

- `"index": "amendments"` - Returns the `Amendments` ledger entry
- `"index": "fee"` - Returns the `FeeSettings` ledger entry
- `"index": "nunl"` - Returns the `NegativeUNL` ledger entry
- `"index": "hashes"` - Returns the "short" `LedgerHashes` ledger entry (recent ledger hashes)

These shortcuts are only available in API version 3 and later. In API versions 1 and 2, these string values would result in an error.

#### `error_code` is the code that belongs to the reported token

`ledger_entry` validates its request fields through helpers that name the malformed field in the `error` token, such as `malformedOwner` or `malformedDirRoot`. Below API version 3 all of them report `error_code` 31 (`invalidParams`) regardless of the token, because changing the number would have broken clients matching on it. From version 3 each token reports the code it owns.

A client that matches on `error_code` 31 for these methods must match on the token instead, or on the code listed below. The `error` token itself is unchanged at every version, so a client that already matches on the token needs no change.

| Token                                      | `error_code` below v3 | `error_code` from v3 |
| ------------------------------------------ | --------------------- | -------------------- |
| `malformedRequest`                         | 31                    | 107                  |
| `malformedAccount`                         | 31                    | 110                  |
| `malformedAddress`                         | 31                    | 111                  |
| `malformedAuthorized`                      | 31                    | 112                  |
| `malformedAuthorizedCredentials`           | 31                    | 113                  |
| `malformedBridgeAccount`                   | 31                    | 114                  |
| `malformedBroker`                          | 31                    | 115                  |
| `malformedCurrency`                        | 31                    | 116                  |
| `malformedDirRoot`                         | 31                    | 117                  |
| `malformedDocumentID`                      | 31                    | 118                  |
| `malformedIssue`                           | 31                    | 119                  |
| `malformedIssuingChainDoor`                | 31                    | 120                  |
| `malformedLockingChainDoor`                | 31                    | 121                  |
| `malformedMPTIssuanceID`                   | 31                    | 122                  |
| `malformedMPTokenIssuance`                 | 31                    | 123                  |
| `malformedOwner`                           | 31                    | 124                  |
| `malformedSeq`                             | 31                    | 125                  |
| `malformedSponsee`                         | 31                    | 126                  |
| `malformedSponsor`                         | 31                    | 127                  |
| `malformedXChainOwnedClaimID`              | 31                    | 128                  |
| `malformedXChainOwnedCreateAccountClaimID` | 31                    | 129                  |

`transaction_entry` reports `malformedRequest` too, and the two handlers agree on it from version 3 on. It set that token with no `error_code` at all before this release, and now reports 107 at every API version, where `ledger_entry` reports 31 below version 3 as the table above shows.

The HTTP status is unchanged: every code in the table above carries 400, as `invalidParams` does. `entryNotFound`, `lgrNotFound` and `unexpectedLedgerType` are unaffected by all of this, having always carried their own code. `unknownOption` is a separate change and is not a version 3 one: `ledger_entry` reports that token at API version 1 only, it carried no `error_code` at all there, and it now carries code 109.
