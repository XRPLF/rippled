#include <xrpld/app/main/CollectorManager.h>

#include <memory>

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
        std::string const& server = get(params, "server");

        if (server == "statsd")
        {
            beast::IP::Endpoint const address(
                beast::IP::Endpoint::from_string(get(params, "address")));
            std::string const& prefix(get(params, "prefix"));

            collector_ = beast::insight::StatsDCollector::New(address, prefix, journal);
        }
        else
        {
            collector_ = beast::insight::NullCollector::New();
        }

        groups_ = beast::insight::make_Groups(collector_);
    }

    ~CollectorManagerImp() = default;

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
make_CollectorManager(Section const& params, beast::Journal journal)
{
    return std::make_unique<CollectorManagerImp>(params, journal);
}

}  // namespace xrpl
