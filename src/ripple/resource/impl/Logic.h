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

#include <ripple/common/UnorderedContainers.h>

#include <beast/chrono/abstract_clock.h>

namespace ripple {
namespace Resource {

class Logic
{
public:
    typedef beast::abstract_clock <std::chrono::seconds> clock_type;
    typedef ripple::unordered_map <std::string, Import> Imports;
    typedef ripple::unordered_map <Key, Entry, Key::hasher, Key::key_equal> Table;

    struct State
    {
        // Table of all entries
        Table table;

        // List of all active inbound entries
        beast::List <Entry> inbound;

        // List of all active outbound entries
        beast::List <Entry> outbound;

        // List of all active admin entries
        beast::List <Entry> admin;

        // List of all inactve entries
        beast::List <Entry> inactive;

        // All imported gossip data
        Imports import_table;
    };

    typedef beast::SharedData <State> SharedState;

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

    SharedState m_state;
    Stats m_stats;
    beast::abstract_clock <std::chrono::seconds>& m_clock;
    beast::Journal m_journal;

    //--------------------------------------------------------------------------

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
        SharedState::UnlockedAccess state (m_state);
        state->import_table.clear();
        state->table.clear();
    }

    Consumer newInboundEndpoint (beast::IP::Endpoint const& address)
    {
        if (isWhitelisted (address))
            return newAdminEndpoint (to_string (address));

        Key key;
        key.kind = kindInbound;
        key.address = address.at_port (0);

        Entry* entry (nullptr);

        {
            SharedState::Access state (m_state);
            std::pair <Table::iterator, bool> result (
                state->table.emplace (key, 0));
            entry = &result.first->second;
            entry->key = &result.first->first;
            ++entry->refcount;
            if (entry->refcount == 1)
            {
                if (! result.second)
                    state->inactive.erase (
                        state->inactive.iterator_to (*entry));
                state->inbound.push_back (*entry);
            }
        }

        m_journal.debug <<
            "New inbound endpoint " << *entry;

        return Consumer (*this, *entry);
    }

    Consumer newOutboundEndpoint (beast::IP::Endpoint const& address)
    {
        if (isWhitelisted (address))
            return newAdminEndpoint (to_string (address));

        Key key;
        key.kind = kindOutbound;
        key.address = address;

        Entry* entry (nullptr);

        {
            SharedState::Access state (m_state);
            std::pair <Table::iterator, bool> result (
                state->table.emplace (key, 0));
            entry = &result.first->second;
            entry->key = &result.first->first;
            ++entry->refcount;
            if (entry->refcount == 1)
            {
                if (! result.second)
                    state->inactive.erase (
                        state->inactive.iterator_to (*entry));
                state->outbound.push_back (*entry);
            }
        }

        m_journal.debug <<
            "New outbound endpoint " << *entry;

        return Consumer (*this, *entry);
    }

    Consumer newAdminEndpoint (std::string const& name)
    {
        Key key;
        key.kind = kindAdmin;
        key.name = name;

        Entry* entry (nullptr);

        {
            SharedState::Access state (m_state);
            std::pair <Table::iterator, bool> result (
                state->table.emplace (key, 0));
            entry = &result.first->second;
            entry->key = &result.first->first;
            ++entry->refcount;
            if (entry->refcount == 1)
            {
                if (! result.second)
                    state->inactive.erase (
                        state->inactive.iterator_to (*entry));
                state->admin.push_back (*entry);
            }
        }

        m_journal.debug <<
            "New admin endpoint " << *entry;

        return Consumer (*this, *entry);
    }

    Entry& elevateToAdminEndpoint (Entry& prior, std::string const& name)
    {
        Key key;
        key.kind = kindAdmin;
        key.name = name;

        m_journal.info <<
            "Elevate " << prior << " to " << name;

        Entry* entry (nullptr);

        {
            SharedState::Access state (m_state);
            std::pair <Table::iterator, bool> result (
                state->table.emplace (key, 0));
            entry = &result.first->second;
            entry->key = &result.first->first;
            ++entry->refcount;
            if (entry->refcount == 1)
            {
                if (! result.second)
                    state->inactive.erase (
                        state->inactive.iterator_to (*entry));
                state->admin.push_back (*entry);
            }

            release (prior, state);
        }

        return *entry;
    }

    Json::Value getJson ()
    {
        return getJson (warningThreshold);
    }

    Json::Value getJson (int threshold)
    {
        clock_type::rep const now (m_clock.elapsed());

        Json::Value ret (Json::objectValue);
        SharedState::Access state (m_state);

        for (beast::List <Entry>::iterator iter (state->inbound.begin());
            iter != state->inbound.end(); ++iter)
        {
            int localBalance = iter->local_balance.value (now);
            if ((localBalance + iter->remote_balance) >= threshold)
            {
                Json::Value& entry = (ret[iter->to_string()] = Json::objectValue);
                entry["local"] = localBalance;
                entry["remote"] = iter->remote_balance;
                entry["type"] = "outbound";
            }

        }
        for (beast::List <Entry>::iterator iter (state->outbound.begin());
            iter != state->outbound.end(); ++iter)
        {
            int localBalance = iter->local_balance.value (now);
            if ((localBalance + iter->remote_balance) >= threshold)
            {
                Json::Value& entry = (ret[iter->to_string()] = Json::objectValue);
                entry["local"] = localBalance;
                entry["remote"] = iter->remote_balance;
                entry["type"] = "outbound";
            }

        }
        for (beast::List <Entry>::iterator iter (state->admin.begin());
            iter != state->admin.end(); ++iter)
        {
            int localBalance = iter->local_balance.value (now);
            if ((localBalance + iter->remote_balance) >= threshold)
            {
                Json::Value& entry = (ret[iter->to_string()] = Json::objectValue);
                entry["local"] = localBalance;
                entry["remote"] = iter->remote_balance;
                entry["type"] = "admin";
            }

        }

        return ret;
    }

    Gossip exportConsumers ()
    {
        clock_type::rep const now (m_clock.elapsed());

        Gossip gossip;
        SharedState::Access state (m_state);

        gossip.items.reserve (state->inbound.size());

        for (beast::List <Entry>::iterator iter (state->inbound.begin());
            iter != state->inbound.end(); ++iter)
        {
            Gossip::Item item;
            item.balance = iter->local_balance.value (now);
            if (item.balance >= minimumGossipBalance)
            {
                item.address = iter->key->address;
                gossip.items.push_back (item);
            }
        }

        return gossip;
    }

    //--------------------------------------------------------------------------

    void importConsumers (std::string const& origin, Gossip const& gossip)
    {
        clock_type::rep const now (m_clock.elapsed());

        {
            SharedState::Access state (m_state);
            std::pair <Imports::iterator, bool> result (
                state->import_table.emplace (origin, 0));

            if (result.second)
            {
                // This is a new import
                Import& next (result.first->second);
                next.whenExpires = now + gossipExpirationSeconds;
                next.items.reserve (gossip.items.size());
                for (std::vector <Gossip::Item>::const_iterator iter (gossip.items.begin());
                    iter != gossip.items.end(); ++iter)
                {
                    Import::Item item;
                    item.balance = iter->balance;
                    item.consumer = newInboundEndpoint (iter->address);
                    item.consumer.entry().remote_balance += item.balance;
                    next.items.push_back (item);
                }
            }
            else
            {
                // Previous import exists so add the new remote
                // balances and then deduct the old remote balances.

                Import next;
                next.whenExpires = now + gossipExpirationSeconds;
                next.items.reserve (gossip.items.size());
                for (std::vector <Gossip::Item>::const_iterator iter (gossip.items.begin());
                    iter != gossip.items.end(); ++iter)
                {
                    Import::Item item;
                    item.balance = iter->balance;
                    item.consumer = newInboundEndpoint (iter->address);
                    item.consumer.entry().remote_balance += item.balance;
                    next.items.push_back (item);
                }

                Import& prev (result.first->second);
                for (std::vector <Import::Item>::iterator iter (prev.items.begin());
                    iter != prev.items.end(); ++iter)
                {
                    iter->consumer.entry().remote_balance -= iter->balance;
                }

                std::swap (next, prev);
            }
        }
    }

    //--------------------------------------------------------------------------

    bool isWhitelisted (beast::IP::Endpoint const& address)
    {
        if (! is_public (address))
            return true;

        return false;
    }

    // Called periodically to expire entries and groom the table.
    //
    void periodicActivity ()
    {
        SharedState::Access state (m_state);

        clock_type::rep const now (m_clock.elapsed());

        for (beast::List <Entry>::iterator iter (
            state->inactive.begin()); iter != state->inactive.end();)
        {
            if (iter->whenExpires <= now)
            {
                m_journal.debug << "Expired " << *iter;
                Table::iterator table_iter (
                    state->table.find (*iter->key));
                ++iter;
                erase (table_iter, state);
            }
            else
            {
                break;
            }
        }

        Imports::iterator iter (state->import_table.begin());
        while (iter != state->import_table.end())
        {
            Import& import (iter->second);
            if (iter->second.whenExpires <= now)
            {
                for (std::vector <Import::Item>::iterator item_iter (import.items.begin());
                    item_iter != import.items.end(); ++item_iter)
                {
                    item_iter->consumer.entry().remote_balance -= item_iter->balance;
                }

                iter = state->import_table.erase (iter);
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

    void acquire (Entry& entry, SharedState::Access& state)
    {
        ++entry.refcount;
    }

    void release (Entry& entry, SharedState::Access& state)
    {
        if (--entry.refcount == 0)
        {
            m_journal.debug <<
                "Inactive " << entry;

            switch (entry.key->kind)
            {
            case kindInbound:
                state->inbound.erase (
                    state->inbound.iterator_to (entry));
                break;
            case kindOutbound:
                state->outbound.erase (
                    state->outbound.iterator_to (entry));
                break;
            case kindAdmin:
                state->admin.erase (
                    state->admin.iterator_to (entry));
                break;
            default:
                bassertfalse;
                break;
            }
            state->inactive.push_back (entry);
            entry.whenExpires = m_clock.elapsed() + secondsUntilExpiration;
        }
    }

    void erase (Table::iterator iter, SharedState::Access& state)
    {
        Entry& entry (iter->second);
        bassert (entry.refcount == 0);
        state->inactive.erase (
            state->inactive.iterator_to (entry));
        state->table.erase (iter);
    }

    Disposition charge (Entry& entry, Charge const& fee, SharedState::Access& state)
    {
        clock_type::rep const now (m_clock.elapsed());
        int const balance (entry.add (fee.cost(), now));
        m_journal.trace <<
            "Charging " << entry << " for " << fee;
        return disposition (balance);
    }

    bool warn (Entry& entry, SharedState::Access& state)
    {
        bool notify (false);
        clock_type::rep const now (m_clock.elapsed());
        if (entry.balance (now) >= warningThreshold && now != entry.lastWarningTime)
        {
            charge (entry, feeWarning, state);
            notify = true;
            entry.lastWarningTime = now;
        }

        if (notify)
            m_journal.info <<
                "Load warning: " << entry;

        if (notify)
            ++m_stats.warn;

        return notify;
    }

    bool disconnect (Entry& entry, SharedState::Access& state)
    {
        bool drop (false);
        clock_type::rep const now (m_clock.elapsed());
        if (entry.balance (now) >= dropThreshold)
        {
            charge (entry, feeDrop, state);
            drop = true;
        }
        if (drop)
            ++m_stats.drop;
        return drop;
    }

    int balance (Entry& entry, SharedState::Access& state)
    {
        return entry.balance (m_clock.elapsed());
    }

    //--------------------------------------------------------------------------

    void acquire (Entry& entry)
    {
        SharedState::Access state (m_state);
        acquire (entry, state);
    }

    void release (Entry& entry)
    {
        SharedState::Access state (m_state);
        release (entry, state);
    }

    Disposition charge (Entry& entry, Charge const& fee)
    {
        SharedState::Access state (m_state);
        return charge (entry, fee, state);
    }

    bool warn (Entry& entry)
    {
        if (entry.admin())
            return false;

        SharedState::Access state (m_state);
        return warn (entry, state);
    }

    bool disconnect (Entry& entry)
    {
        if (entry.admin())
            return false;

        SharedState::Access state (m_state);
        return disconnect (entry, state);
    }

    int balance (Entry& entry)
    {
        SharedState::Access state (m_state);
        return balance (entry, state);
    }

    //--------------------------------------------------------------------------

    void writeList (
        clock_type::rep const now,
            beast::PropertyStream::Set& items,
                beast::List <Entry>& list)
    {
        for (beast::List <Entry>::iterator iter (list.begin());
            iter != list.end(); ++iter)
        {
            beast::PropertyStream::Map item (items);
            if (iter->refcount != 0)
                item ["count"] = iter->refcount;
            item ["name"] = iter->to_string();
            item ["balance"] = iter->balance(now);
            if (iter->remote_balance != 0)
                item ["remote_balance"] = iter->remote_balance;
        }
    }

    void onWrite (beast::PropertyStream::Map& map)
    {
        clock_type::rep const now (m_clock.elapsed());

        SharedState::Access state (m_state);

        {
            beast::PropertyStream::Set s ("inbound", map);
            writeList (now, s, state->inbound);
        }

        {
            beast::PropertyStream::Set s ("outbound", map);
            writeList (now, s, state->outbound);
        }

        {
            beast::PropertyStream::Set s ("admin", map);
            writeList (now, s, state->admin);
        }

        {
            beast::PropertyStream::Set s ("inactive", map);
            writeList (now, s, state->inactive);
        }
    }
};

}
}

#endif
