#ifndef XRPL_APP_MISC_DETAIL_WORKPLAIN_H_INCLUDED
#define XRPL_APP_MISC_DETAIL_WORKPLAIN_H_INCLUDED

#include <xrpld/app/misc/detail/WorkBase.h>

namespace ripple {

namespace detail {

// Work over TCP/IP
class WorkPlain : public WorkBase<WorkPlain>,
                  public std::enable_shared_from_this<WorkPlain>
{
    friend class WorkBase<WorkPlain>;

public:
    WorkPlain(
        std::string const& host,
        std::string const& path,
        std::string const& port,
        boost::asio::io_context& ios,
        endpoint_type const& lastEndpoint,
        bool lastStatus,
        callback_type cb);
    ~WorkPlain() = default;

private:
    void
    onConnect(error_code const& ec);

    socket_type&
    stream()
    {
        return socket_;
    }
};

//------------------------------------------------------------------------------

WorkPlain::WorkPlain(
    std::string const& host,
    std::string const& path,
    std::string const& port,
    boost::asio::io_context& ios,
    endpoint_type const& lastEndpoint,
    bool lastStatus,
    callback_type cb)
    : WorkBase(host, path, port, ios, lastEndpoint, lastStatus, cb)
{
}

void
WorkPlain::onConnect(error_code const& ec)
{
    if (ec)
        return fail(ec);

    onStart();
}

}  // namespace detail

}  // namespace ripple

#endif
