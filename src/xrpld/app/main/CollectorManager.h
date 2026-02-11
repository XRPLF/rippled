#pragma once

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/beast/insight/Insight.h>

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
make_CollectorManager(Section const& params, beast::Journal journal);

}  // namespace xrpl
