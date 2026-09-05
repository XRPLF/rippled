#pragma once

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Group.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>

#include <memory>
#include <string>

namespace xrpl {

/**
 * Provides the beast::insight::Collector service.
 */
class CollectorManager
{
public:
    virtual ~CollectorManager() = default;

    virtual beast::insight::Collector::ptr const&
    collector() = 0;

    virtual beast::insight::Group::ptr const&
    group(std::string const& name) = 0;
};

/**
 * Construct the collector manager.
 *
 * @param params       The [insight] config section.
 * @param serviceName  service.name resource attribute for OTel metrics
 * (empty -> the collector defaults it to "xrpld").
 * @param networkType  xrpl.network.type resource attribute for OTel
 * metrics (e.g. "mainnet"), derived from [network_id].
 * @param journal      Journal for logging.
 */
std::unique_ptr<CollectorManager>
makeCollectorManager(
    Section const& params,
    std::string const& serviceName,
    std::string const& networkType,
    beast::Journal journal);

}  // namespace xrpl
