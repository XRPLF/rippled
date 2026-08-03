#pragma once

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/server/NetworkOPs.h>

#include <boost/asio.hpp>

#include <cstddef>
#include <memory>

namespace xrpl {

class LedgerMaster;
class ValidatorKeys;

std::unique_ptr<NetworkOPs>
makeNetworkOPs(
    ServiceRegistry& registry,
    NetworkOPs::clock_type& clock,
    bool standalone,
    std::size_t minPeerCount,
    bool startValid,
    JobQueue& jobQueue,
    LedgerMaster& ledgerMaster,
    ValidatorKeys const& validatorKeys,
    boost::asio::io_context& ioSvc,
    beast::Journal journal,
    beast::insight::Collector::ptr const& collector);

}  // namespace xrpl
