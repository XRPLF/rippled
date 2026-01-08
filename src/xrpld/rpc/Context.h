#ifndef XRPL_RPC_CONTEXT_H_INCLUDED
#define XRPL_RPC_CONTEXT_H_INCLUDED

#include <xrpld/rpc/Role.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/server/InfoSub.h>

namespace xrpl {

class Application;
class NetworkOPs;
class LedgerMaster;

namespace RPC {

/** The context of information needed to call an RPC. */
struct Context
{
    beast::Journal const j;
    Application& app;
    Resource::Charge& loadType;
    NetworkOPs& netOps;
    LedgerMaster& ledgerMaster;
    Resource::Consumer& consumer;
    Role role;
    std::shared_ptr<JobQueue::Coro> coro{};
    InfoSub::pointer infoSub{};
    unsigned int apiVersion;
};

struct JsonContext : public Context
{
    /**
     * Data passed in from HTTP headers.
     */
    struct Headers
    {
        std::string_view user;
        std::string_view forwardedFor;
    };

    Json::Value params;

    Headers headers{};
};

template <class RequestType>
struct GRPCContext : public Context
{
    RequestType params;
};

}  // namespace RPC
}  // namespace xrpl

#endif
