#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Counter.h>
#include <xrpl/beast/insight/Event.h>
#include <xrpl/beast/insight/Gauge.h>
#include <xrpl/beast/insight/Group.h>
#include <xrpl/beast/insight/Groups.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/insight/HookImpl.h>
#include <xrpl/beast/insight/Meter.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace beast {
namespace insight {

namespace detail {

class GroupImp : public std::enable_shared_from_this<GroupImp>, public Group
{
public:
    std::string const m_name;
    Collector::ptr m_collector;

    GroupImp(std::string const& name_, Collector::ptr const& collector)
        : m_name(name_), m_collector(collector)
    {
    }

    ~GroupImp() = default;

    std::string const&
    name() const override
    {
        return m_name;
    }

    std::string
    make_name(std::string const& name)
    {
        return m_name + "." + name;
    }

    Hook
    make_hook(HookImpl::HandlerType const& handler) override
    {
        return m_collector->make_hook(handler);
    }

    Counter
    make_counter(std::string const& name) override
    {
        return m_collector->make_counter(make_name(name));
    }

    Event
    make_event(std::string const& name) override
    {
        return m_collector->make_event(make_name(name));
    }

    Gauge
    make_gauge(std::string const& name) override
    {
        return m_collector->make_gauge(make_name(name));
    }

    Meter
    make_meter(std::string const& name) override
    {
        return m_collector->make_meter(make_name(name));
    }

private:
    GroupImp&
    operator=(GroupImp const&);
};

//------------------------------------------------------------------------------

class GroupsImp : public Groups
{
public:
    using Items =
        std::unordered_map<std::string, std::shared_ptr<Group>, uhash<>>;

    Collector::ptr m_collector;
    Items m_items;

    explicit GroupsImp(Collector::ptr const& collector) : m_collector(collector)
    {
    }

    ~GroupsImp() = default;

    Group::ptr const&
    get(std::string const& name) override
    {
        std::pair<Items::iterator, bool> result(
            m_items.emplace(name, Group::ptr()));
        Group::ptr& group(result.first->second);
        if (result.second)
            group = std::make_shared<GroupImp>(name, m_collector);
        return group;
    }
};

}  // namespace detail

//------------------------------------------------------------------------------

Groups::~Groups() = default;

std::unique_ptr<Groups>
make_Groups(Collector::ptr const& collector)
{
    return std::make_unique<detail::GroupsImp>(collector);
}

}  // namespace insight
}  // namespace beast
