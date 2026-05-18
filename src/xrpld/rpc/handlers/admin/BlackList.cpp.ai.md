# `BlackList.cpp` — Admin RPC Handler for Resource Consumption Inspection

`BlackList.cpp` implements `doBlackList`, the handler behind the `"blacklist"` admin RPC command. Its purpose is to surface the node's internal resource-consumption accounting to operators in real time: specifically, it reports which connected endpoints (inbound or outbound peers and clients) have accumulated a load balance at or above a given threshold, making them candidates for rate-limiting or disconnection.

## Role in the System

The handler is registered in `src/xrpld/rpc/detail/Handler.cpp` with `Role::ADMIN`, which means it is only callable by a locally authenticated administrator — not by ordinary network clients. This access control is enforced by the RPC dispatch layer before `doBlackList` is ever invoked; the handler itself does not need to recheck the caller's role.

The file is one of two handlers in the `admin/` subdirectory alongside `UnlList.cpp`. Both are thin shims: they pull an application subsystem handle from the `RPC::JsonContext` and delegate serialization entirely to that subsystem. `doBlackList` calls through to `Resource::Manager`, while `doUnlList` does the same for the validator list.

## What the Handler Does

```cpp
Json::Value
doBlackList(RPC::JsonContext& context)
{
    auto& rm = context.app.getResourceManager();
    if (context.params.isMember(jss::threshold))
        return rm.getJson(context.params[jss::threshold].asInt());

    return rm.getJson();
}
```

The function fetches a reference to the global `Resource::Manager` singleton through `Application::getResourceManager()`. If the caller supplies a `threshold` field in the request JSON, it is extracted as an integer and forwarded to `Manager::getJson(int threshold)`. Otherwise, the zero-argument `getJson()` is called, which internally calls `getJson(warningThreshold)` where `warningThreshold = 5000` (the balance at which the resource system begins issuing warnings).

`Manager::getJson(int threshold)` iterates over all tracked inbound entries and emits a JSON object for each whose combined `local_balance + remote_balance` meets or exceeds the threshold. Each emitted entry reports the local balance, remote balance (learned via gossip from other nodes), and connection type. The threshold parameter is therefore a filter: at the default of 5000 you see every endpoint already in warning territory; a caller passing `0` would see every tracked endpoint regardless of load.

## Design Notes

The name "blacklist" reflects the operational interpretation: endpoints at or above `warningThreshold` are the ones the resource system considers misbehaving and may warn or drop. The handler exposes exactly that view. Adjusting the threshold downward lets operators inspect endpoints approaching but not yet at warning level; adjusting it upward narrows the view to endpoints near or past the disconnection threshold (25000).

The handler performs no explicit type validation beyond `Json::Value::asInt()`. If `threshold` is present but not convertible to an integer, `asInt()` throws a `Json::LogicError`. There is no range check: a negative threshold is silently accepted and would cause `getJson` to return every endpoint (since all balances are non-negative). This is not exploitable — the command is admin-only — but it is a gap worth noting for defensive callers.

The decision to delegate serialization entirely to `Resource::Manager::getJson` rather than building the JSON response in the handler itself keeps the RPC layer free of resource-accounting knowledge. The `Manager` interface defines two overloads of `getJson` precisely to support this pattern: callers that do not care about the threshold get the sensible default, and callers that want control pass their own value.