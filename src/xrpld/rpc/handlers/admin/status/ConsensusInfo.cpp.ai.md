# `ConsensusInfo.cpp` — RPC Handler for `consensus_info`

This file implements `doConsensusInfo`, the RPC handler that surfaces the ledger node's current consensus engine state to administrators. It exists as one of several diagnostic handlers in the `admin/status/` directory, alongside peers like `doFetchInfo` and `doValidatorInfo`, all of which follow the same pattern: receive an `RPC::JsonContext`, call one method on the network-operations subsystem, and return the result wrapped in a standard JSON envelope.

## Role in the System

The `consensus_info` command is an admin-only diagnostic tool. In `src/xrpld/rpc/detail/Handler.cpp`, it is registered as:

```cpp
{"consensus_info", byRef(&doConsensusInfo), Role::ADMIN, NO_CONDITION}
```

The `Role::ADMIN` restriction means the command is only reachable by connections authenticated as administrator clients — it is not exposed to ordinary user-facing API consumers. `NO_CONDITION` means no ledger state prerequisite is checked before dispatch; the handler runs regardless of whether the node is synchronized or has a current or closed ledger in hand.

In `RPCCall.cpp`, the command is mapped to `parseAsIs` with both minimum and maximum argument counts set to zero. Any command-line invocation that passes extra arguments is immediately rejected with a `badSyntax` error before the handler is ever reached.

## The Handler

`doConsensusInfo` is a pure pass-through:

```cpp
Json::Value
doConsensusInfo(RPC::JsonContext& context)
{
    Json::Value ret(Json::objectValue);
    ret[jss::info] = context.netOps.getConsensusInfo();
    return ret;
}
```

The `RPC::JsonContext` carries a reference to the `NetworkOPs` interface, which abstracts the node's network and consensus operations. `getConsensusInfo()` is implemented in `NetworkOPsImp` as a single-line delegation:

```cpp
Json::Value
NetworkOPsImp::getConsensusInfo()
{
    return mConsensus.getJson(true);
}
```

`mConsensus` is the live `Consensus<Adaptor>` instance. The `true` argument selects the verbose response path in `Consensus::getJson(bool full)`, which is explicitly documented in `Consensus.h` as being "called by the `consensus_info` RPC." That method assembles a JSON object containing fields like `proposing`, `proposers`, `synched`, `ledger_seq`, `close_granularity`, and (in full mode) the current consensus phase, dispute sets, and timing information.

## Design Rationale

The handler itself performs no transformation, no validation, and no error handling. This is intentional and consistent throughout the `admin/status/` module. All input validation is done either by the RPC framework before dispatch (argument count enforcement via `parseAsIs`) or by the role-check gate (`Role::ADMIN`). Because `consensus_info` accepts no user parameters, there is nothing to validate at the handler level — the only meaningful action is to snapshot the subsystem and return it.

Keeping handlers this thin has a practical benefit: when `Consensus::getJson()` changes — say, a new consensus phase is added — the RPC layer requires no changes. The boundary between "how consensus state is serialized" and "how it reaches the RPC caller" stays clean.

## Testing

`src/test/server/ServerStatus_test.cpp` exercises `getConsensusInfo()` directly on the `NetworkOPs` object to assert `validating` status under various amendment-blocked and UNL-blocked scenarios. The `consensus_info` command-line form is tested in `RPCCall_test.cpp`, which confirms that the minimal invocation produces correct JSON and that extra arguments trigger a `badSyntax` error. There is no dedicated unit test for `doConsensusInfo` itself — given its role as a one-line adapter, such a test would only validate the JSON key name `jss::info`, which the framework tests cover transitively.