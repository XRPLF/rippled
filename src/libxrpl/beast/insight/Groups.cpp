#include <xrpl/beast/insight/Groups.h>

#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Counter.h>
#include <xrpl/beast/insight/Event.h>
#include <xrpl/beast/insight/Gauge.h>
#include <xrpl/beast/insight/Group.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/insight/HookImpl.h>
#include <xrpl/beast/insight/Meter.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace beast::insight {

namespace detail {

class GroupImp : public std::enable_shared_from_this<GroupImp>, public Group
{
public:
    std::string const name_;
    Collector::ptr collector_;

    GroupImp(std::string name, Collector::ptr collector)
        : name_(std::move(name)), collector_(std::move(collector))
    {
    }

    ~GroupImp() override = default;

    std::string const&
    name() const override
    {
        return name_;
    }

    std::string
    makeName(std::string const& name)
    {
        return name_ + "." + name;
    }

    Hook
    make_hook(HookImpl::HandlerType const& handler) override
    {
        return collector_->make_hook(handler);
    }

    Counter
    make_counter(std::string const& name) override
    {
        return collector_->make_counter(makeName(name));
    }

    Event
    make_event(std::string const& name) override
    {
        return collector_->make_event(makeName(name));
    }

    Gauge
    make_gauge(std::string const& name) override
    {
        return collector_->make_gauge(makeName(name));
    }

    Meter
    make_meter(std::string const& name) override
    {
        return collector_->make_meter(makeName(name));
    }

    GroupImp&
    operator=(GroupImp const&) = delete;
};

//------------------------------------------------------------------------------

class GroupsImp : public Groups
{
public:
    using Items = std::unordered_map<std::string, std::shared_ptr<Group>, uhash<>>;

    Collector::ptr collector;
    Items items;

    explicit GroupsImp(Collector::ptr collector) : collector(std::move(collector))
    {
    }

    ~GroupsImp() override = default;

    Group::ptr const&
    get(std::string const& name) override
    {
        std::pair<Items::iterator, bool> const result(items.emplace(name, Group::ptr()));
        Group::ptr& group(result.first->second);
        if (result.second)
            group = std::make_shared<GroupImp>(name, collector);
        return group;
    }
};

}  // namespace detail

//------------------------------------------------------------------------------

Groups::~Groups() = default;

std::unique_ptr<Groups>
makeGroups(Collector::ptr const& collector)
{
    return std::make_unique<detail::GroupsImp>(collector);
}

}  // namespace beast::insight
