#pragma once

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Group.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>

#include <memory>
#include <string>

namespace xrpl {

/** Provides the beast::insight::Collector service. */
class CollectorManager
{
public:
    virtual ~CollectorManager() = default;

    virtual beast::insight::Collector::ptr const&
    collector() = 0;

    virtual beast::insight::Group::ptr const&
    group(std::string const& name) = 0;
};

std::unique_ptr<CollectorManager>
makeCollectorManager(Section const& params, beast::Journal journal);

}  // namespace xrpl
