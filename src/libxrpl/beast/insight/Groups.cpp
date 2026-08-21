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
#include <xrpl/beast/insight/Unit.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace beast::insight {

namespace detail {

class GroupImp : public std::enable_shared_from_this<GroupImp>, public Group
{
    std::string const name_;
    Collector::ptr collector_;

public:
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
    makeHook(HookImpl::HandlerType const& handler) override
    {
        return collector_->makeHook(handler);
    }

    Counter
    makeCounter(std::string const& name) override
    {
        return collector_->makeCounter(makeName(name));
    }

    using Collector::makeEvent;

    Event
    makeEvent(std::string const& name) override
    {
        return collector_->makeEvent(makeName(name));
    }

    // Forwards the unit as well as the prefixed name. Without this override
    // the base-class default would delegate to the single-argument overload
    // above and silently drop the unit, which is how a byte-valued Event ends
    // up declared as milliseconds -- call sites reach a collector through a
    // Group, so this is the hop that actually matters.
    Event
    makeEvent(std::string const& name, Unit unit) override
    {
        return collector_->makeEvent(makeName(name), unit);
    }

    Gauge
    makeGauge(std::string const& name) override
    {
        return collector_->makeGauge(makeName(name));
    }

    Meter
    makeMeter(std::string const& name) override
    {
        return collector_->makeMeter(makeName(name));
    }

    GroupImp&
    operator=(GroupImp const&) = delete;
};

//------------------------------------------------------------------------------

class GroupsImp : public Groups
{
public:
    using Items = std::unordered_map<std::string, std::shared_ptr<Group>, Uhash<>>;

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
