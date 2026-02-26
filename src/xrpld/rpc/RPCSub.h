#pragma once

#include <xrpl/core/JobQueue.h>
#include <xrpl/server/InfoSub.h>

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

std::shared_ptr<RPCSub>
make_RPCSub(
    InfoSub::Source& source,
    JobQueue& jobQueue,
    std::string const& strUrl,
    std::string const& strUsername,
    std::string const& strPassword,
    Logs& logs);

}  // namespace xrpl
