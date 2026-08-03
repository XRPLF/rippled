#include <xrpld/app/main/CollectorManager.h>

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Group.h>
#include <xrpl/beast/insight/Groups.h>
#include <xrpl/beast/insight/NullCollector.h>
#include <xrpl/beast/insight/StatsDCollector.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>

#include <memory>
#include <string>

namespace xrpl {

class CollectorManagerImp : public CollectorManager
{
public:
    // NOLINTBEGIN(readability-identifier-naming)
    beast::Journal journal_;
    beast::insight::Collector::ptr collector_;
    std::unique_ptr<beast::insight::Groups> groups_;
    // NOLINTEND(readability-identifier-naming)

    CollectorManagerImp(Section const& params, beast::Journal journal) : journal_(journal)
    {
        std::string const& server = get(params, Keys::kServer);

        if (server == "statsd")
        {
            beast::IP::Endpoint const address(
                beast::IP::Endpoint::fromString(get(params, Keys::kAddress)));
            std::string const& prefix(get(params, Keys::kPrefix));

            collector_ = beast::insight::StatsDCollector::make(address, prefix, journal);
        }
        else
        {
            collector_ = beast::insight::NullCollector::make();
        }

        groups_ = beast::insight::makeGroups(collector_);
    }

    ~CollectorManagerImp() override = default;

    beast::insight::Collector::ptr const&
    collector() override
    {
        return collector_;
    }

    beast::insight::Group::ptr const&
    group(std::string const& name) override
    {
        return groups_->get(name);
    }
};

//------------------------------------------------------------------------------

std::unique_ptr<CollectorManager>
makeCollectorManager(Section const& params, beast::Journal journal)
{
    return std::make_unique<CollectorManagerImp>(params, journal);
}

}  // namespace xrpl
