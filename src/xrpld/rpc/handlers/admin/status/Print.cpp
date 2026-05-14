/** @file
 *  Admin RPC handler that serializes the application's `beast::PropertyStream`
 *  tree to JSON.
 *
 *  `Application` is the root of a named, hierarchical tree of diagnostic
 *  sources — every major subsystem registers itself as a child. `doPrint`
 *  drives a `JsonPropertyStream` sink over that tree to produce a runtime
 *  introspection snapshot. New subsystems that register as
 *  `PropertyStream::Source` children are automatically included without any
 *  change to this handler.
 */

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/JsonPropertyStream.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

/** Serialize the application's full `beast::PropertyStream` tree to JSON.
 *
 *  Drives a `JsonPropertyStream` sink over the `Application` property-stream
 *  tree and returns the resulting JSON object as the RPC response. An optional
 *  dot-delimited path filter may be supplied via
 *  `params[0]` (e.g. `"ledgermaster"` or `"consensus.*"`) to target a
 *  specific named sub-source; if the path ends with `*` the subtree is dumped
 *  recursively. If the parameter is absent or malformed the entire tree is
 *  returned.
 *
 *  Serialization safety is provided by `beast::PropertyStream::Source::write()`,
 *  which acquires a `std::recursive_mutex` on each source node before
 *  iterating its children. This handler acquires no additional locks.
 *
 *  @param context  RPC dispatch context; `context.app` is the stream root.
 *      An optional path filter is read from
 *      `context.params[jss::params][0u]` as a string; all other shapes are
 *      silently ignored and the full tree is serialized instead.
 *  @return JSON object containing the serialized property-stream tree (or
 *      the targeted sub-tree when a valid path filter is provided).
 *  @note This handler always succeeds — a malformed or mistyped path filter
 *      produces a full-tree dump rather than an error response.
 */
json::Value
doPrint(RPC::JsonContext& context)
{
    JsonPropertyStream stream;
    if (context.params.isObject() && context.params[jss::params].isArray() &&
        context.params[jss::params][0u].isString())
    {
        context.app.write(stream, context.params[jss::params][0u].asString());
    }
    else
    {
        context.app.write(stream);
    }

    return stream.top();
}

}  // namespace xrpl
