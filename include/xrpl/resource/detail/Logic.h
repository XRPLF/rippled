#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/insight/Insight.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/resource/Gossip.h>
#include <xrpl/resource/detail/Import.h>

#include <mutex>

namespace xrpl::Resource {

class Logic
{
private:
    using clock_type = Stopwatch;
    using Imports = hash_map<std::string, Import>;
    using Table = hash_map<Key, Entry, Key::Hasher, Key::KeyEqual>;
    using EntryIntrusiveList = beast::List<Entry>;

    struct Stats
    {
        Stats(beast::insight::Collector::ptr const& collector)
        {
            warn = collector->makeMeter("warn");
            drop = collector->makeMeter("drop");
        }

        beast::insight::Meter warn;
        beast::insight::Meter drop;
    };

    Stats stats_;
    Stopwatch& clock_;
    beast::Journal journal_;

    std::recursive_mutex lock_;

    // Table of all entries
    Table table_;

    // Because the following are intrusive lists, a given Entry may be in
    // at most list at a given instant.  The Entry must be removed from
    // one list before placing it in another.

    // List of all active inbound entries
    EntryIntrusiveList inbound_;

    // List of all active outbound entries
    EntryIntrusiveList outbound_;

    // List of all active admin entries
    EntryIntrusiveList admin_;

    // List of all inactive entries
    EntryIntrusiveList inactive_;

    // All imported gossip data
    Imports importTable_;

    //--------------------------------------------------------------------------
public:
    Logic(
        beast::insight::Collector::ptr const& collector,
        clock_type& clock,
        beast::Journal journal)
        : stats_(collector), clock_(clock), journal_(journal)
    {
    }

    ~Logic()
    {
        // These have to be cleared before the Logic is destroyed
        // since their destructors call back into the class.
        // Order matters here as well, the import table has to be
        // destroyed before the consumer table.
        //
        importTable_.clear();
        table_.clear();
    }

    Consumer
    newInboundEndpoint(beast::IP::Endpoint const& address)
    {
        Entry* entry(nullptr);

        {
            std::scoped_lock const _(lock_);
            auto [resultIt, resultInserted] = table_.emplace(
                std::piecewise_construct,
                std::make_tuple(Kind::Inbound, address.atPort(0)),  // Key
                std::make_tuple(clock_.now()));                     // Entry

            entry = &resultIt->second;
            entry->key = &resultIt->first;
            ++entry->refcount;
            if (entry->refcount == 1)
            {
                if (!resultInserted)
                {
                    inactive_.erase(inactive_.iteratorTo(*entry));
                }
                inbound_.pushBack(*entry);
            }
        }

        JLOG(journal_.debug()) << "New inbound endpoint " << *entry;

        return Consumer(*this, *entry);
    }

    Consumer
    newOutboundEndpoint(beast::IP::Endpoint const& address)
    {
        Entry* entry(nullptr);

        {
            std::scoped_lock const _(lock_);
            auto [resultIt, resultInserted] = table_.emplace(
                std::piecewise_construct,
                std::make_tuple(Kind::Outbound, address),  // Key
                std::make_tuple(clock_.now()));            // Entry

            entry = &resultIt->second;
            entry->key = &resultIt->first;
            ++entry->refcount;
            if (entry->refcount == 1)
            {
                if (!resultInserted)
                    inactive_.erase(inactive_.iteratorTo(*entry));
                outbound_.pushBack(*entry);
            }
        }

        JLOG(journal_.debug()) << "New outbound endpoint " << *entry;

        return Consumer(*this, *entry);
    }

    /**
     * Create endpoint that should not have resource limits applied. Other
     * restrictions, such as permission to perform certain RPC calls, may be
     * enabled.
     */
    Consumer
    newUnlimitedEndpoint(beast::IP::Endpoint const& address)
    {
        Entry* entry(nullptr);

        {
            std::scoped_lock const _(lock_);
            auto [resultIt, resultInserted] = table_.emplace(
                std::piecewise_construct,
                std::make_tuple(Kind::Unlimited, address.atPort(1)),  // Key
                std::make_tuple(clock_.now()));                       // Entry

            entry = &resultIt->second;
            entry->key = &resultIt->first;
            ++entry->refcount;
            if (entry->refcount == 1)
            {
                if (!resultInserted)
                    inactive_.erase(inactive_.iteratorTo(*entry));
                admin_.pushBack(*entry);
            }
        }

        JLOG(journal_.debug()) << "New unlimited endpoint " << *entry;

        return Consumer(*this, *entry);
    }

    json::Value
    getJson()
    {
        return getJson(kWarningThreshold);
    }

    /** Returns a json::ValueType::Object. */
    json::Value
    getJson(int threshold)
    {
        clock_type::time_point const now(clock_.now());

        json::Value ret(json::ValueType::Object);
        std::scoped_lock const _(lock_);

        for (auto& inboundEntry : inbound_)
        {
            int const localBalance = inboundEntry.local_balance.value(now);
            if ((localBalance + inboundEntry.remote_balance) >= threshold)
            {
                json::Value& entry = (ret[inboundEntry.toString()] = json::ValueType::Object);
                entry[jss::local] = localBalance;
                entry[jss::remote] = inboundEntry.remote_balance;
                entry[jss::type] = "inbound";
            }
        }
        for (auto& outboundEntry : outbound_)
        {
            int const localBalance = outboundEntry.local_balance.value(now);
            if ((localBalance + outboundEntry.remote_balance) >= threshold)
            {
                json::Value& entry = (ret[outboundEntry.toString()] = json::ValueType::Object);
                entry[jss::local] = localBalance;
                entry[jss::remote] = outboundEntry.remote_balance;
                entry[jss::type] = "outbound";
            }
        }
        for (auto& adminEntry : admin_)
        {
            int const localBalance = adminEntry.local_balance.value(now);
            if ((localBalance + adminEntry.remote_balance) >= threshold)
            {
                json::Value& entry = (ret[adminEntry.toString()] = json::ValueType::Object);
                entry[jss::local] = localBalance;
                entry[jss::remote] = adminEntry.remote_balance;
                entry[jss::type] = "admin";
            }
        }

        return ret;
    }

    Gossip
    exportConsumers()
    {
        clock_type::time_point const now(clock_.now());

        Gossip gossip;
        std::scoped_lock const _(lock_);

        gossip.items.reserve(inbound_.size());

        for (auto& inboundEntry : inbound_)
        {
            Gossip::Item item;
            item.balance = inboundEntry.local_balance.value(now);
            if (item.balance >= kMinimumGossipBalance)
            {
                item.address = inboundEntry.key->address;
                gossip.items.push_back(item);
            }
        }

        return gossip;
    }

    //--------------------------------------------------------------------------

    void
    importConsumers(std::string const& origin, Gossip const& gossip)
    {
        auto const elapsed = clock_.now();
        {
            std::scoped_lock const _(lock_);
            auto [resultIt, resultInserted] = importTable_.emplace(
                std::piecewise_construct,
                std::make_tuple(origin),                                    // Key
                std::make_tuple(clock_.now().time_since_epoch().count()));  // Import

            if (resultInserted)
            {
                // This is a new import
                Import& next(resultIt->second);
                next.whenExpires = elapsed + kGossipExpirationSeconds;
                next.items.reserve(gossip.items.size());

                for (auto const& gossipItem : gossip.items)
                {
                    Import::Item item;
                    item.balance = gossipItem.balance;
                    item.consumer = newInboundEndpoint(gossipItem.address);
                    item.consumer.entry().remote_balance += item.balance;
                    next.items.push_back(item);
                }
            }
            else
            {
                // Previous import exists so add the new remote
                // balances and then deduct the old remote balances.

                Import next;
                next.whenExpires = elapsed + kGossipExpirationSeconds;
                next.items.reserve(gossip.items.size());
                for (auto const& gossipItem : gossip.items)
                {
                    Import::Item item;
                    item.balance = gossipItem.balance;
                    item.consumer = newInboundEndpoint(gossipItem.address);
                    item.consumer.entry().remote_balance += item.balance;
                    next.items.push_back(item);
                }

                Import& prev(resultIt->second);
                for (auto& item : prev.items)
                {
                    item.consumer.entry().remote_balance -= item.balance;
                }

                std::swap(next, prev);
            }
        }
    }

    //--------------------------------------------------------------------------

    // Called periodically to expire entries and groom the table.
    //
    void
    periodicActivity()
    {
        std::scoped_lock const _(lock_);

        auto const elapsed = clock_.now();

        for (auto iter(inactive_.begin()); iter != inactive_.end();)
        {
            if (iter->whenExpires <= elapsed)
            {
                JLOG(journal_.debug()) << "Expired " << *iter;
                auto tableIter = table_.find(*iter->key);
                ++iter;
                erase(tableIter);
            }
            else
            {
                break;
            }
        }

        auto iter = importTable_.begin();
        while (iter != importTable_.end())
        {
            Import& import(iter->second);
            if (iter->second.whenExpires <= elapsed)
            {
                for (auto itemIter(import.items.begin()); itemIter != import.items.end();
                     ++itemIter)
                {
                    itemIter->consumer.entry().remote_balance -= itemIter->balance;
                }

                iter = importTable_.erase(iter);
            }
            else
            {
                ++iter;
            }
        }
    }

    //--------------------------------------------------------------------------

    // Returns the disposition based on the balance and thresholds
    static Disposition
    disposition(int balance)
    {
        if (balance >= kDropThreshold)
            return Disposition::Drop;

        if (balance >= kWarningThreshold)
            return Disposition::Warn;

        return Disposition::Ok;
    }

    void
    erase(Table::iterator iter)
    {
        std::scoped_lock const _(lock_);
        Entry& entry(iter->second);
        XRPL_ASSERT(entry.refcount == 0, "xrpl::Resource::Logic::erase : entry not used");
        inactive_.erase(inactive_.iteratorTo(entry));
        table_.erase(iter);
    }

    void
    acquire(Entry& entry)
    {
        std::scoped_lock const _(lock_);
        ++entry.refcount;
    }

    void
    release(Entry& entry)
    {
        std::scoped_lock const _(lock_);
        if (--entry.refcount == 0)
        {
            JLOG(journal_.debug()) << "Inactive " << entry;

            switch (entry.key->kind)
            {
                case Kind::Inbound:
                    inbound_.erase(inbound_.iteratorTo(entry));
                    break;
                case Kind::Outbound:
                    outbound_.erase(outbound_.iteratorTo(entry));
                    break;
                case Kind::Unlimited:
                    admin_.erase(admin_.iteratorTo(entry));
                    break;
                default:
                    // LCOV_EXCL_START
                    UNREACHABLE(
                        "xrpl::Resource::Logic::release : invalid entry "
                        "kind");
                    break;
                    // LCOV_EXCL_STOP
            }
            inactive_.pushBack(entry);
            entry.whenExpires = clock_.now() + kSecondsUntilExpiration;
        }
    }

    Disposition
    charge(Entry& entry, Charge const& fee, std::string context = {})
    {
        static constexpr Charge::value_type kFeeLogAsWarn = 3000;
        static constexpr Charge::value_type kFeeLogAsInfo = 1000;
        static constexpr Charge::value_type kFeeLogAsDebug = 100;
        static_assert(
            kFeeLogAsWarn > kFeeLogAsInfo && kFeeLogAsInfo > kFeeLogAsDebug && kFeeLogAsDebug > 10);

        static auto kGetStream = [](Resource::Charge::value_type cost, beast::Journal& journal) {
            if (cost >= kFeeLogAsWarn)
                return journal.warn();
            if (cost >= kFeeLogAsInfo)
                return journal.info();
            if (cost >= kFeeLogAsDebug)
                return journal.debug();
            return journal.trace();
        };

        if (!context.empty())
            context = " (" + context + ")";

        std::scoped_lock const _(lock_);
        clock_type::time_point const now(clock_.now());
        int const balance(entry.add(fee.cost(), now));
        JLOG(kGetStream(fee.cost(), journal_)) << "Charging " << entry << " for " << fee << context;
        return disposition(balance);
    }

    bool
    warn(Entry& entry)
    {
        if (entry.isUnlimited())
            return false;

        std::scoped_lock const _(lock_);
        bool notify(false);
        auto const elapsed = clock_.now();
        if (entry.balance(clock_.now()) >= kWarningThreshold && elapsed != entry.lastWarningTime)
        {
            charge(entry, kFeeWarning);
            notify = true;
            entry.lastWarningTime = elapsed;
        }
        if (notify)
        {
            JLOG(journal_.info()) << "Load warning: " << entry;
            ++stats_.warn;
        }
        return notify;
    }

    bool
    disconnect(Entry& entry)
    {
        if (entry.isUnlimited())
            return false;

        std::scoped_lock const _(lock_);
        bool drop(false);
        clock_type::time_point const now(clock_.now());
        int const balance(entry.balance(now));
        if (balance >= kDropThreshold)
        {
            JLOG(journal_.warn()) << "Consumer entry " << entry << " dropped with balance "
                                  << balance << " at or above drop threshold " << kDropThreshold;

            // Adding feeDrop at this point keeps the dropped connection
            // from re-connecting for at least a little while after it is
            // dropped.
            charge(entry, kFeeDrop);
            ++stats_.drop;
            drop = true;
        }
        return drop;
    }

    int
    balance(Entry& entry)
    {
        std::scoped_lock const _(lock_);
        return entry.balance(clock_.now());
    }

    //--------------------------------------------------------------------------

    static void
    writeList(
        clock_type::time_point const now,
        beast::PropertyStream::Set& items,
        EntryIntrusiveList& list)
    {
        for (auto& entry : list)
        {
            beast::PropertyStream::Map item(items);
            if (entry.refcount != 0)
                item["count"] = entry.refcount;
            item["name"] = entry.toString();
            item["balance"] = entry.balance(now);
            if (entry.remote_balance != 0)
                item["remote_balance"] = entry.remote_balance;
        }
    }

    void
    onWrite(beast::PropertyStream::Map& map)
    {
        clock_type::time_point const now(clock_.now());

        std::scoped_lock const _(lock_);

        {
            beast::PropertyStream::Set s("inbound", map);
            writeList(now, s, inbound_);
        }

        {
            beast::PropertyStream::Set s("outbound", map);
            writeList(now, s, outbound_);
        }

        {
            beast::PropertyStream::Set s("admin", map);
            writeList(now, s, admin_);
        }

        {
            beast::PropertyStream::Set s("inactive", map);
            writeList(now, s, inactive_);
        }
    }
};

}  // namespace xrpl::Resource
