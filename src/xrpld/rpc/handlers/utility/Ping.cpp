#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

namespace RPC {
struct JsonContext;
}  // namespace RPC

/** Handle the `ping` RPC command, returning a role-conditional session
 *  introspection object.
 *
 *  The response is sparse by design — only the fields relevant to the
 *  caller's privilege level are emitted:
 *
 *  - `Role::ADMIN`: sets `"role": "admin"`.
 *  - `Role::IDENTIFIED`: sets `"role": "identified"`, `"username"` from
 *      `context.headers.user`, and `"ip"` from `context.headers.forwardedFor`
 *      when non-empty. This role is assigned when a `secure_gateway` proxy
 *      has authenticated a downstream user via the `X-User` HTTP header.
 *  - `Role::PROXY`: sets `"role": "proxied"` and `"ip"` when non-empty.
 *      The client is behind a gateway but has not been individually identified.
 *  - `Role::GUEST`, `Role::USER`, `Role::FORBID`: fall through to `default`,
 *      producing an empty object. These callers learn nothing about their
 *      classification from the response.
 *
 *  The `"ip"` field is written only when `forwardedFor` is non-empty, so
 *  direct connections never receive a spurious `"ip": ""` key.
 *
 *  For WebSocket sessions, `context.infoSub` is non-null and an additional
 *  `"unlimited": true` field is emitted when the consumer is exempt from
 *  resource throttling. HTTP callers always receive a null `infoSub` and
 *  therefore never see this field; the null check is the sole guard.
 *
 *  All JSON keys are referenced via `jss::` namespace constants rather than
 *  raw string literals, ensuring a single point of change across the codebase.
 *
 *  @param context  The JSON-RPC dispatch envelope, pre-populated with the
 *      resolved `role`, `headers` (`user`, `forwardedFor`), and `infoSub`
 *      by the HTTP/WebSocket dispatch layer before this handler is called.
 *  @return A `json::Value` object containing the applicable session fields,
 *      or an empty object for unprivileged / unclassified callers.
 *  @note `role` is resolved by `requestRole()` (in `Role.h`) before dispatch,
 *      so this handler contains no authorization logic of its own.
 *  @see Role.h, Context.h
 */
json::Value
doPing(RPC::JsonContext& context)
{
    json::Value ret(json::ValueType::Object);
    switch (context.role)
    {
        case Role::ADMIN:
            ret[jss::role] = "admin";
            break;
        case Role::IDENTIFIED:
            ret[jss::role] = "identified";
            ret[jss::username] = std::string{context.headers.user};
            if (!context.headers.forwardedFor.empty())
                ret[jss::ip] = std::string{context.headers.forwardedFor};
            break;
        case Role::PROXY:
            ret[jss::role] = "proxied";
            if (!context.headers.forwardedFor.empty())
                ret[jss::ip] = std::string{context.headers.forwardedFor};
            break;
        default:;
    }

    if (context.infoSub)
    {
        if (context.infoSub->getConsumer().isUnlimited())
            ret[jss::unlimited] = true;
    }

    return ret;
}

}  // namespace xrpl
