//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_RESOURCE_LOGIC_H_INCLUDED
#define RIPPLE_RESOURCE_LOGIC_H_INCLUDED

#include <ripple/resource/Fees.h>
#include <ripple/resource/Gossip.h>
#include <ripple/resource/impl/Import.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/jss.h>
#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/beast/insight/Insight.h>
#include <ripple/beast/utility/PropertyStream.h>
#include <cassert>
#include <mutex>

namespace ripple {
namespace Resource {

class Logic
{
private:
    using clock_type = Stopwatch;
    using Imports = hash_map <std::string, Import>;
    using Table = hash_map <Key, Entry, Key::hasher, Key::key_equal>;
    using EntryIntrusiveList = beast::List <Entry>;

    struct Stats
    {
        Stats (beast::insight::Collector::ptr const& collector)
        {
            warn = collector->make_meter ("warn");
            drop = collector->make_meter ("drop");
        }

        beast::insight::Meter warn;
        beast::insight::Meter drop;
    };

    Stats m_stats;
    Stopwatch& m_clock;
    beast::Journal m_journal;

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

    // List of all inactve entries
    EntryIntrusiveList inactive_;

    // All imported gossip data
    Imports importTable_;

    //--------------------------------------------------------------------------
public:

    Logic (beast::insight::Collector::ptr const& collector,
        clock_type& clock, beast::Journal journal)
        : m_stats (collector)
        , m_clock (clock)
        , m_journal (journal)
    {
    }

    ~Logic ()
    {
        // These have to be cleared before the Logic is destroyed
        // since their destructors call back into the class.
        // Order matters here as well, the import table has to be
        // destroyed before the consumer table.
        //
        importTable_.clear();
        table_.clear();
    }

    Consumer newInboundEndpoint (beast::IP::Endpoint const& address)
    {
        Entry* entry (nullptr);

        {
            std::lock_guard _(lock_);
            auto [resultIt, resultInserted] =
                table_.emplace (std::piecewise_construct,
                    std::make_tuple (kindInbound, address.at_port (0)), // Key
                    std::make_tuple (m_clock.now()));                   // Entry

            entry = &resultIt->second;
            entry->key = &resultIt->first;
            ++entry->refcount;
            if (entry->refcount == 1)
            {
                if (! resultInserted)
                {
                    inactive_.erase (
                        inactive_.iterator_to (*entry));
                }
                inbound_.push_back (*entry);
            }
        }

        JLOG(m_journal.debug()) <<
            "New inbound endpoint " << *entry;

        return Consumer (*this, *entry);
    }

    Consumer newOutboundEndpoint (beast::IP::Endpoint const& address)
    {
        Entry* entry (nullptr);

        {
            std::lock_guard _(lock_);
            auto [resultIt, resultInserted] =
                table_.emplace (std::piecewise_construct,
                    std::make_tuple (kindOutbound, address),            // Key
                    std::make_tuple (m_clock.now()));                   // Entry

            entry = &resultIt->second;
            entry->key = &resultIt->first;
            ++entry->refcount;
            if (entry->refcount == 1)
            {
                if (! resultInserted)
                    inactive_.erase (
                        inactive_.iterator_to (*entry));
                outbound_.push_back (*entry);
            }
        }

        JLOG(m_journal.debug()) <<
            "New outbound endpoint " << *entry;

        return Consumer (*this, *entry);
    }

    /**
     * Create endpoint that should not have resource limits applied. Other
     * restrictions, such as permission to perform certain RPC calls, may be
     * enabled.
     */
    Consumer newUnlimitedEndpoint (beast::IP::Endpoint const& address)
    {
        Entry* entry (nullptr);

        {
            std::lock_guard _(lock_);
            auto [resultIt, resultInserted] =
                table_.emplace (std::piecewise_construct,
                    std::make_tuple (kindUnlimited, address.at_port(1)),// Key
                    std::make_tuple (m_clock.now()));                   // Entry

            entry = &resultIt->second;
            entry->key = &resultIt->first;
            ++entry->refcount;
            if (entry->refcount == 1)
            {
                if (! resultInserted)
                    inactive_.erase (
                        inactive_.iterator_to (*entry));
                admin_.push_back (*entry);
            }
        }

        JLOG(m_journal.debug()) <<
            "New unlimited endpoint " << *entry;

        return Consumer (*this, *entry);
    }

    Json::Value getJson ()
    {
        return getJson (warningThreshold);
    }

    /** Returns a Json::objectValue. */
    Json::Value getJson (int threshold)
    {
        clock_type::time_point const now (m_clock.now());

        Json::Value ret (Json::objectValue);
        std::lock_guard _(lock_);

        for (auto& inboundEntry : inbound_)
        {
            int localBalance = inboundEntry.local_balance.value (now);
            if ((localBalance + inboundEntry.remote_balance) >= threshold)
            {
                Json::Value& entry = (ret[inboundEntry.to_string()] = Json::objectValue);
                entry[jss::local] = localBalance;
                entry[jss::remote] = inboundEntry.remote_balance;
                entry[jss::type] = "inbound";
            }

        }
        for (auto& outboundEntry : outbound_)
        {
            int localBalance = outboundEntry.local_balance.value (now);
            if ((localBalance + outboundEntry.remote_balance) >= threshold)
            {
                Json::Value& entry = (ret[outboundEntry.to_string()] = Json::objectValue);
                entry[jss::local] = localBalance;
                entry[jss::remote] = outboundEntry.remote_balance;
                entry[jss::type] = "outbound";
            }

        }
        for (auto& adminEntry : admin_)
        {
            int localBalance = adminEntry.local_balance.value (now);
            if ((localBalance + adminEntry.remote_balance) >= threshold)
            {
                Json::Value& entry = (ret[adminEntry.to_string()] = Json::objectValue);
                entry[jss::local] = localBalance;
                entry[jss::remote] = adminEntry.remote_balance;
                entry[jss::type] = "admin";
            }

        }

        return ret;
    }

    Gossip exportConsumers ()
    {
        clock_type::time_point const now (m_clock.now());

        Gossip gossip;
        std::lock_guard _(lock_);

        gossip.items.reserve (inbound_.size());

        for (auto& inboundEntry : inbound_)
        {
            Gossip::Item item;
            item.balance = inboundEntry.local_balance.value (now);
            if (item.balance >= minimumGossipBalance)
            {
                item.address = inboundEntry.key->address;
                gossip.items.push_back (item);
            }
        }

        return gossip;
    }

    //--------------------------------------------------------------------------

    void importConsumers (std::string const& origin, Gossip const& gossip)
    {
        auto const elapsed = m_clock.now();
        {
            std::lock_guard _(lock_);
            auto [resultIt, resultInserted] =
                importTable_.emplace (std::piecewise_construct,
                    std::make_tuple(origin),                  // Key
                    std::make_tuple(m_clock.now().time_since_epoch().count()));     // Import

            if (resultInserted)
            {
                // This is a new import
                Import& next (resultIt->second);
                next.whenExpires = elapsed + gossipExpirationSeconds;
                next.items.reserve (gossip.items.size());

                for (auto const& gossipItem : gossip.items)
                {
                    Import::Item item;
                    item.balance = gossipItem.balance;
                    item.consumer = newInboundEndpoint (gossipItem.address);
                    item.consumer.entry().remote_balance += item.balance;
                    next.items.push_back (item);
                }
            }
            else
            {
                // Previous import exists so add the new remote
                // balances and then deduct the old remote balances.

                Import next;
                next.whenExpires = elapsed + gossipExpirationSeconds;
                next.items.reserve (gossip.items.size());
                for (auto const& gossipItem : gossip.items)
                {
                    Import::Item item;
                    item.balance = gossipItem.balance;
                    item.consumer = newInboundEndpoint (gossipItem.address);
                    item.consumer.entry().remote_balance += item.balance;
                    next.items.push_back (item);
                }

                Import& prev (resultIt->second);
                for (auto& item : prev.items)
                {
                    item.consumer.entry().remote_balance -= item.balance;
                }

                std::swap (next, prev);
            }
        }
    }

    //--------------------------------------------------------------------------

    // Called periodically to expire entries and groom the table.
    //
    void periodicActivity ()
    {
        std::lock_guard _(lock_);

        auto const elapsed = m_clock.now();

        for (auto iter (inactive_.begin()); iter != inactive_.end();)
        {
            if (iter->whenExpires <= elapsed)
            {
                JLOG(m_journal.debug()) << "Expired " << *iter;
                auto table_iter =
                    table_.find (*iter->key);
                ++iter;
                erase (table_iter);
            }
            else
            {
                break;
            }
        }

        auto iter = importTable_.begin();
        while (iter != importTable_.end())
        {
            Import& import (iter->second);
            if (iter->second.whenExpires <= elapsed)
            {
                for (auto item_iter (import.items.begin());
                    item_iter != import.items.end(); ++item_iter)
                {
                    item_iter->consumer.entry().remote_balance -= item_iter->balance;
                }

                iter = importTable_.erase (iter);
            }
            else
                ++iter;
        }
    }

    //--------------------------------------------------------------------------

    // Returns the disposition based on the balance and thresholds
    static Disposition disposition (int balance)
    {
        if (balance >= dropThreshold)
            return Disposition::drop;

        if (balance >= warningThreshold)
            return Disposition::warn;

        return Disposition::ok;
    }

    void erase (Table::iterator iter)
    {
        std::lock_guard _(lock_);
        Entry& entry (iter->second);
        assert (entry.refcount == 0);
        inactive_.erase (
            inactive_.iterator_to (entry));
        table_.erase (iter);
    }

    void acquire (Entry& entry)
    {
        std::lock_guard _(lock_);
        ++entry.refcount;
    }

    void release (Entry& entry)
    {
        std::lock_guard _(lock_);
        if (--entry.refcount == 0)
        {
            JLOG(m_journal.debug()) <<
                "Inactive " << entry;

            switch (entry.key->kind)
            {
            case kindInbound:
                inbound_.erase (
                    inbound_.iterator_to (entry));
                break;
            case kindOutbound:
                outbound_.erase (
                    outbound_.iterator_to (entry));
                break;
            case kindUnlimited:
                admin_.erase (
                    admin_.iterator_to (entry));
                break;
            default:
                assert(false);
                break;
            }
            inactive_.push_back (entry);
            entry.whenExpires = m_clock.now() + secondsUntilExpiration;
        }
    }

    Disposition charge (Entry& entry, Charge const& fee)
    {
        std::lock_guard _(lock_);
        clock_type::time_point const now (m_clock.now());
        int const balance (entry.add (fee.cost(), now));
        JLOG(m_journal.trace()) <<
            "Charging " << entry << " for " << fee;
        return disposition (balance);
    }

    bool warn (Entry& entry)
    {
        if (entry.isUnlimited())
            return false;

        std::lock_guard _(lock_);
        bool notify (false);
        auto const elapsed = m_clock.now();
        if (entry.balance (m_clock.now()) >= warningThreshold &&
            elapsed != entry.lastWarningTime)
        {
            charge (entry, feeWarning);
            notify = true;
            entry.lastWarningTime = elapsed;
        }
        if (notify)
        {
            JLOG(m_journal.info()) << "Load warning: " << entry;
            ++m_stats.warn;
        }
        return notify;
    }

    bool disconnect (Entry& entry)
    {
        if (entry.isUnlimited())
            return false;

        std::lock_guard _(lock_);
        bool drop (false);
        clock_type::time_point const now (m_clock.now());
        int const balance (entry.balance (now));
        if (balance >= dropThreshold)
        {
            JLOG(m_journal.warn()) <<
                "Consumer entry " << entry <<
                " dropped with balance " << balance <<
                " at or above drop threshold " << dropThreshold;

            // Adding feeDrop at this point keeps the dropped connection
            // from re-connecting for at least a little while after it is
            // dropped.
            charge (entry, feeDrop);
            ++m_stats.drop;
            drop = true;
        }
        return drop;
    }

    int balance (Entry& entry)
    {
        std::lock_guard _(lock_);
        return entry.balance (m_clock.now());
    }

    //--------------------------------------------------------------------------

    void writeList (
        clock_type::time_point const now,
            beast::PropertyStream::Set& items,
                EntryIntrusiveList& list)
    {
        for (auto& entry : list)
        {
            beast::PropertyStream::Map item (items);
            if (entry.refcount != 0)
                item ["count"] = entry.refcount;
            item ["name"] = entry.to_string();
            item ["balance"] = entry.balance(now);
            if (entry.remote_balance != 0)
                item ["remote_balance"] = entry.remote_balance;
        }
    }

    void onWrite (beast::PropertyStream::Map& map)
    {
        clock_type::time_point const now (m_clock.now());

        std::lock_guard _(lock_);

        {
            beast::PropertyStream::Set s ("inbound", map);
            writeList (now, s, inbound_);
        }

        {
            beast::PropertyStream::Set s ("outbound", map);
            writeList (now, s, outbound_);
        }

        {
            beast::PropertyStream::Set s ("admin", map);
            writeList (now, s, admin_);
        }

        {
            beast::PropertyStream::Set s ("inactive", map);
            writeList (now, s, inactive_);
        }
    }
};

}
}

#endif
