#pragma once

#include <xrpld/app/misc/detail/WorkBase.h>

#include <memory>
#include <string>

namespace xrpl::detail {

// Work over TCP/IP
class WorkPlain : public WorkBase<WorkPlain>, public std::enable_shared_from_this<WorkPlain>
{
    friend class WorkBase<WorkPlain>;

public:
    WorkPlain(
        std::string const& host,
        std::string const& path,
        std::string const& port,
        boost::asio::io_context& ios,
        EndpointType const& lastEndpoint,
        bool lastStatus,
        CallbackType cb);
    ~WorkPlain() override = default;

private:
    void
    onConnect(ErrorCode const& ec);

    SocketType&
    stream()
    {
        return socket_;
    }
};

//------------------------------------------------------------------------------

inline WorkPlain::WorkPlain(
    std::string const& host,
    std::string const& path,
    std::string const& port,
    boost::asio::io_context& ios,
    EndpointType const& lastEndpoint,
    bool lastStatus,
    CallbackType cb)
    : WorkBase(host, path, port, ios, lastEndpoint, lastStatus, cb)
{
}

inline void
WorkPlain::onConnect(ErrorCode const& ec)
{
    if (ec)
    {
        fail(ec);
        return;
    }

    onStart();
}

}  // namespace xrpl::detail
