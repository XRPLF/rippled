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

/** Concrete `Group` implementation that transparently prefixes all metric names.
 *
 *  Wraps an underlying `Collector` and prepends `name + "."` to every named
 *  metric (counter, event, gauge, meter) before forwarding to the collector.
 *  This gives each subsystem an isolated dot-separated namespace — e.g., a
 *  group named `"ledger"` turns `"validations"` into `"ledger.validations"` —
 *  without requiring callers to manage the prefix manually.
 *
 *  Because `GroupImp` inherits `Group`, which inherits `Collector`, a
 *  `Group::ptr` is substitutable for any `Collector::ptr`. Metric consumers
 *  are unaware of the grouping indirection.
 *
 *  @note Not thread-safe. Groups are expected to be created on a single thread
 *      during node initialisation, not during concurrent hot-path operation.
 */
class GroupImp : public std::enable_shared_from_this<GroupImp>, public Group
{
    std::string const name_;
    Collector::ptr collector_;

public:
    /** Construct a group with the given namespace prefix and backing collector.
     *
     *  @param name      The dot-separated prefix applied to all metric names
     *      created through this group.
     *  @param collector The underlying collector that receives the prefixed
     *      metric-creation calls.
     */
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

    /** Forward the hook to the underlying collector without applying a prefix.
     *
     *  Hooks are polling callbacks, not named time-series, so there is no name
     *  to prefix. The handler is passed through verbatim.
     *
     *  @param handler The polling callback invoked at each collection interval.
     *  @return A `Hook` handle whose lifetime controls callback registration.
     */
    Hook
    makeHook(HookImpl::HandlerType const& handler) override
    {
        return collector_->makeHook(handler);
    }

    /** Create a counter whose name is automatically prefixed with this group's name.
     *
     *  @param name The unqualified metric name; the underlying collector receives
     *      `groupName + "." + name`.
     *  @return A `Counter` handle tied to the prefixed metric.
     */
    Counter
    makeCounter(std::string const& name) override
    {
        return collector_->makeCounter(makeName(name));
    }

    /** Create an event whose name is automatically prefixed with this group's name.
     *
     *  @param name The unqualified metric name; the underlying collector receives
     *      `groupName + "." + name`.
     *  @return An `Event` handle tied to the prefixed metric.
     */
    Event
    makeEvent(std::string const& name) override
    {
        return collector_->makeEvent(makeName(name));
    }

    /** Create a gauge whose name is automatically prefixed with this group's name.
     *
     *  @param name The unqualified metric name; the underlying collector receives
     *      `groupName + "." + name`.
     *  @return A `Gauge` handle tied to the prefixed metric.
     */
    Gauge
    makeGauge(std::string const& name) override
    {
        return collector_->makeGauge(makeName(name));
    }

    /** Create a meter whose name is automatically prefixed with this group's name.
     *
     *  @param name The unqualified metric name; the underlying collector receives
     *      `groupName + "." + name`.
     *  @return A `Meter` handle tied to the prefixed metric.
     */
    Meter
    makeMeter(std::string const& name) override
    {
        return collector_->makeMeter(makeName(name));
    }

    GroupImp&
    operator=(GroupImp const&) = delete;
};

//------------------------------------------------------------------------------

/** Concrete `Groups` implementation: a lazy-creating registry of named `GroupImp` instances.
 *
 *  Maintains an `unordered_map` from group name to `Group::ptr`. The first call
 *  to `get()` for a given name constructs a `GroupImp`; subsequent calls return
 *  the cached instance. Both this registry and each individual `GroupImp` hold
 *  an independent `shared_ptr` to the underlying collector, so a caller that
 *  retains a `Group::ptr` after the `GroupsImp` is destroyed continues to
 *  function correctly.
 *
 *  @note Not thread-safe. Concurrent calls to `get()` with new names would race
 *      on the internal map.
 */
class GroupsImp : public Groups
{
public:
    /** Map type from group name to cached `Group::ptr`, using beast's universal hash. */
    using Items = std::unordered_map<std::string, std::shared_ptr<Group>, Uhash<>>;

    /** The underlying collector shared with every `GroupImp` this registry creates. */
    Collector::ptr collector;

    /** Cache of previously created groups, keyed by name. */
    Items items;

    explicit GroupsImp(Collector::ptr collector) : collector(std::move(collector))
    {
    }

    ~GroupsImp() override = default;

    /** Return the group registered under `name`, creating it if necessary.
     *
     *  Uses `emplace()` to insert a null placeholder only when the key is new,
     *  then immediately replaces it with a fully constructed `GroupImp`. Returns
     *  a `const&` into the map, which remains stable across further insertions
     *  (but not across a rehash triggered by a concurrent insertion).
     *
     *  @param name The dot-separated namespace prefix for the group.
     *  @return A stable `const` reference to the cached `Group::ptr` for `name`.
     */
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
