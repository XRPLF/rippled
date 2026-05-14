/** @file
 *  WebSocket implementation of the InfoSub subscription handle.
 *
 *  Every WebSocket connection that passes through `ServerHandler` gets exactly
 *  one `WSInfoSub` attached to it as its `appDefined` payload. The handle
 *  bridges the XRPL notification system (ledger events, transactions, server
 *  status) to the underlying `WSSession` managed by the network layer.
 */
#pragma once

#include <xrpld/rpc/Role.h>

#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/server/InfoSub.h>
#include <xrpl/server/WSSession.h>

#include <memory>
#include <string>

namespace xrpl {

/** WebSocket-transport implementation of `InfoSub`.
 *
 *  Fulfills the `InfoSub` contract for clients connected over WebSocket.
 *  The session is held as a `std::weak_ptr` to avoid a reference cycle:
 *  the `WSSession` owns its `appDefined` (which is this object), so a strong
 *  back-reference would prevent either side from being destroyed.
 *
 *  During construction the remote address is checked against the port's
 *  `secure_gateway` networks. Only connections arriving through a trusted
 *  reverse-proxy address have their `X-User` and `X-Forwarded-For` headers
 *  honoured; all other connections leave `user_` and `fwdfor_` empty,
 *  preventing untrusted clients from spoofing identity or rate-limit buckets.
 *
 *  @see InfoSub
 *  @see ServerHandler::onUpgrade()
 */
class WSInfoSub : public InfoSub
{
    std::weak_ptr<WSSession> ws_;
    std::string user_;
    std::string fwdfor_;

public:
    /** Construct and register a WebSocket subscription handle.
     *
     *  Registers with @p source (typically `NetworkOPs`) via the `InfoSub`
     *  base constructor. Inspects the HTTP upgrade request headers to extract
     *  the trusted-proxy identity only when the remote IP falls within the
     *  port's configured `secure_gateway` networks.
     *
     *  @param source The subscription registry (usually `NetworkOPs`) that
     *      this handle registers with. The base-class destructor will
     *      unregister all subscriptions when the connection closes.
     *  @param ws The live WebSocket session. Stored as a `weak_ptr`; the
     *      session controls its own lifetime via the server I/O layer.
     */
    WSInfoSub(Source& source, std::shared_ptr<WSSession> const& ws) : InfoSub(source), ws_(ws)
    {
        auto const& h = ws->request();
        if (ipAllowed(
                beast::IPAddressConversion::fromAsio(ws->remoteEndpoint()).address(),
                ws->port().secure_gateway_nets_v4,
                ws->port().secure_gateway_nets_v6))
        {
            auto it = h.find("X-User");
            if (it != h.end())
                user_ = it->value();
            fwdfor_ = std::string(::xrpl::forwardedFor(h));
        }
    }

    /** Return the authenticated user name supplied by a trusted reverse proxy.
     *
     *  Non-empty only when the connection arrived through an address in the
     *  port's `secure_gateway` networks and the HTTP upgrade request contained
     *  an `X-User` header. Used by `ServerHandler::onUpgrade()` to assign the
     *  correct `Resource::Consumer` role and rate-limit bucket.
     *
     *  @return A view into the stored user string; empty if the connection is
     *      not from a trusted proxy or no `X-User` header was present.
     */
    [[nodiscard]] std::string_view
    user() const
    {
        return user_;
    }

    /** Return the originating client address from a trusted reverse proxy.
     *
     *  Non-empty only when the connection arrived through an address in the
     *  port's `secure_gateway` networks and a forwarding header was present.
     *  Parsed from `X-Forwarded-For` (or RFC 7239 `Forwarded`) by
     *  `xrpl::forwardedFor()`. Used alongside `user()` to establish the
     *  `Resource::Consumer` during WebSocket upgrade.
     *
     *  @return A view into the stored forwarded-for string; empty if the
     *      connection is not from a trusted proxy or no forwarding header
     *      was present.
     */
    [[nodiscard]] std::string_view
    forwardedFor() const
    {
        return fwdfor_;
    }

    /** Serialize @p jv and enqueue it for delivery over the WebSocket connection.
     *
     *  Serializes directly into a `boost::beast::multi_buffer` via
     *  `Json::stream()`, avoiding an intermediate `std::string` allocation.
     *  The buffer is wrapped in a `StreambufWSMsg` and forwarded to
     *  `WSSession::send()` for async delivery.
     *
     *  If the underlying `WSSession` has already been destroyed (connection
     *  closed or server shutdown), the send is silently dropped — events are
     *  fire-and-forget from the publisher's perspective.
     *
     *  @param jv The JSON value to serialize and send.
     *  @param  The broadcast flag from the base-class interface. Ignored here;
     *      the WebSocket transport always queues an individual async write
     *      regardless of whether the event is a broadcast or a targeted reply.
     */
    void
    send(json::Value const& jv, bool) override
    {
        auto sp = ws_.lock();
        if (!sp)
            return;
        boost::beast::multi_buffer sb;
        json::stream(jv, [&](void const* data, std::size_t n) {
            sb.commit(boost::asio::buffer_copy(sb.prepare(n), boost::asio::buffer(data, n)));
        });
        auto m = std::make_shared<StreambufWSMsg<decltype(sb)>>(std::move(sb));
        sp->send(m);
    }
};

}  // namespace xrpl
