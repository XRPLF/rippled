#pragma once

#include <xrpl/core/JobQueue.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/server/InfoSub.h>

#include <boost/asio/io_context.hpp>

#include <memory>
#include <string>

namespace xrpl {

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

// VFALCO Why is the ioContext needed?
std::shared_ptr<RPCSub>
makeRPCSub(
    InfoSub::Source& source,
    boost::asio::io_context& ioContext,
    JobQueue& jobQueue,
    std::string const& strUrl,
    std::string const& strUsername,
    std::string const& strPassword,
    ServiceRegistry& registry);

}  // namespace xrpl
