#pragma once

#include <xrpl/beast/insight/Insight.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/server/NetworkOPs.h>

#include <boost/asio.hpp>

#include <memory>

namespace xrpl {

class LedgerMaster;
class ValidatorKeys;

std::unique_ptr<NetworkOPs>
make_NetworkOPs(
    ServiceRegistry& registry,
    NetworkOPs::clock_type& clock,
    bool standalone,
    std::size_t minPeerCount,
    bool startValid,
    JobQueue& jobQueue,
    LedgerMaster& ledgerMaster,
    ValidatorKeys const& validatorKeys,
    boost::asio::io_context& ioCtx,
    beast::Journal journal,
    beast::insight::Collector::ptr const& collector);

}  // namespace xrpl
