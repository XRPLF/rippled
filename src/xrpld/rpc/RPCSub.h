#ifndef XRPL_NET_RPCSUB_H_INCLUDED
#define XRPL_NET_RPCSUB_H_INCLUDED

#include <xrpld/core/JobQueue.h>
#include <xrpld/rpc/InfoSub.h>

#include <boost/asio/io_context.hpp>

namespace ripple {

/** Subscription object for JSON RPC. */
class RPCSub : public InfoSub
{
public:
    virtual void
    setUsername(std::string const& strUsername) = 0;
    virtual void
    setPassword(std::string const& strPassword) = 0;

protected:
    explicit RPCSub(InfoSub::Source& source);
};

// VFALCO Why is the io_context needed?
std::shared_ptr<RPCSub>
make_RPCSub(
    InfoSub::Source& source,
    boost::asio::io_context& io_context,
    JobQueue& jobQueue,
    std::string const& strUrl,
    std::string const& strUsername,
    std::string const& strPassword,
    Logs& logs);

}  // namespace ripple

#endif
