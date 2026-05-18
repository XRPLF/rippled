# `Print.cpp` — Admin RPC Handler for Application Property Stream Dump

## Role in the System

`Print.cpp` implements `doPrint`, an admin-only RPC command that exposes the entire `rippled` application's internal diagnostic state as a JSON object. It sits among a small cluster of status handlers (`ConsensusInfo`, `FetchInfo`, `GetCounts`, `ValidatorInfo`, etc.) in `src/xrpld/rpc/handlers/admin/status/` — but where those handlers target specific subsystems, `doPrint` exposes the full `beast::PropertyStream` tree rooted at the `Application` object itself.

## The PropertyStream Architecture

`Application` extends `beast::PropertyStream::Source`, making the application object the root of a named, hierarchical tree of diagnostic sources. Every major subsystem — consensus engine, ledger master, network operations, job queue, and so on — registers itself as a child source. This tree can be traversed and serialized on demand, providing a runtime introspection view of the node's internal state without coupling any component to a particular output format.

`beast::PropertyStream::Source` exposes two relevant `write()` overloads: one that serializes the full tree recursively, and one that accepts a dot-delimited path string to target a specific named sub-source (or, if the path ends with `*`, to recursively dump that subtree's children). `doPrint` uses whichever variant is appropriate based on the incoming RPC parameters.

`JsonPropertyStream` is the concrete `PropertyStream` sink used here. It implements the abstract `PropertyStream` interface by building up a `Json::Value` object tree, buffered in a stack of `Json::Value*` pointers. When `app.write()` drives the stream, it populates this structure; `stream.top()` then returns the accumulated root `Json::Value`, which becomes the RPC response.

## Parameter Handling and Validation

The optional path filter follows the nested `params` convention used throughout the RPC layer: the caller passes `{"params": ["some.path"]}`. The guard condition — `context.params.isObject() && context.params[jss::params].isArray() && context.params[jss::params][0u].isString()` — validates all three type levels before accessing the string. If any check fails, the function silently falls back to dumping the entire tree. There is no error return path; the handler always succeeds, making it robust to malformed or missing parameters.

This "silent fallback to full dump" is a deliberate design choice for a diagnostic tool: a caller who passes a bad filter still gets useful output rather than an error response. The tradeoff is that a typo in a path string is invisible to the caller.

## Concurrency Notes

Serialization safety is handled inside `beast::PropertyStream::Source::write()`, which acquires a `std::recursive_mutex` on the source before iterating its children. `doPrint` itself takes no locks; it relies entirely on the `PropertyStream` infrastructure to mediate access to subsystem state. This is consistent with the other admin status handlers in this directory, which similarly read state via thread-safe accessor methods rather than acquiring top-level locks.

## Relationship to Sibling Handlers

`doPrint` is the lowest-level, highest-breadth handler in the `admin/status` group. The other handlers in this directory (`ConsensusInfo`, `ValidatorInfo`, etc.) are purpose-built for specific subsystems and return structured, pre-shaped JSON. `doPrint` makes no assumptions about what the application contains — it delegates entirely to the property stream tree, so it automatically reflects new subsystems that register themselves as `PropertyStream::Source` children without requiring any changes to the handler itself.