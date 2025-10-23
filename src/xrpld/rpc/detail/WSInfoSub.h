#ifndef XRPL_RPC_WSINFOSUB_H
#define XRPL_RPC_WSINFOSUB_H

#include <xrpld/rpc/InfoSub.h>
#include <xrpld/rpc/Role.h>

#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/server/WSSession.h>

#include <memory>
#include <string>

namespace ripple {

class WSInfoSub : public InfoSub
{
    std::weak_ptr<WSSession> ws_;
    std::string user_;
    std::string fwdfor_;

public:
    WSInfoSub(Source& source, std::shared_ptr<WSSession> const& ws)
        : InfoSub(source), ws_(ws)
    {
        auto const& h = ws->request();
        if (ipAllowed(
                beast::IPAddressConversion::from_asio(ws->remote_endpoint())
                    .address(),
                ws->port().secure_gateway_nets_v4,
                ws->port().secure_gateway_nets_v6))
        {
            auto it = h.find("X-User");
            if (it != h.end())
                user_ = it->value();
            fwdfor_ = std::string(forwardedFor(h));
        }
    }

    std::string_view
    user() const
    {
        return user_;
    }

    std::string_view
    forwarded_for() const
    {
        return fwdfor_;
    }

    void
    send(Json::Value const& jv, bool) override
    {
        auto sp = ws_.lock();
        if (!sp)
            return;
        boost::beast::multi_buffer sb;
        Json::stream(jv, [&](void const* data, std::size_t n) {
            sb.commit(boost::asio::buffer_copy(
                sb.prepare(n), boost::asio::buffer(data, n)));
        });
        auto m = std::make_shared<StreambufWSMsg<decltype(sb)>>(std::move(sb));
        sp->send(m);
    }
};

}  // namespace ripple

#endif
