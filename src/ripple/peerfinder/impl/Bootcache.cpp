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

#include <ripple/peerfinder/impl/Bootcache.h>
#include <ripple/peerfinder/impl/iosformat.h>
#include <ripple/peerfinder/impl/Tuning.h>
#include <ripple/basics/Log.h>

namespace ripple {
namespace PeerFinder {

Bootcache::Bootcache (
    Store& store,
    clock_type& clock,
    beast::Journal journal)
    : m_store (store)
    , m_clock (clock)
    , m_journal (journal)
    , m_whenUpdate (m_clock.now ())
    , m_needsUpdate (false)
{
}

Bootcache::~Bootcache ()
{
    update();
}

bool
Bootcache::empty() const
{
    return m_map.empty();
}

Bootcache::map_type::size_type
Bootcache::size() const
{
    return m_map.size();
}

Bootcache::const_iterator
Bootcache::begin() const
{
    return const_iterator (m_map.right.begin());
}

Bootcache::const_iterator
Bootcache::cbegin() const
{
    return const_iterator (m_map.right.begin());
}

Bootcache::const_iterator
Bootcache::end() const
{
    return const_iterator (m_map.right.end());
}

Bootcache::const_iterator
Bootcache::cend() const
{
    return const_iterator (m_map.right.end());
}

void
Bootcache::clear()
{
    m_map.clear();
    m_needsUpdate = true;
}

//--------------------------------------------------------------------------

void
Bootcache::load ()
{
    clear();
    auto const n (m_store.load (
        [this](beast::IP::Endpoint const& endpoint, int valence)
        {
            auto const result (this->m_map.insert (
                value_type (endpoint, valence)));
            if (! result.second)
            {
                JLOG(this->m_journal.error())
                    << beast::leftw (18) <<
                    "Bootcache discard " << endpoint;
            }
        }));

    if (n > 0)
    {
        JLOG(m_journal.info()) << beast::leftw (18) <<
            "Bootcache loaded " << n <<
                ((n > 1) ? " addresses" : " address");
        prune ();
    }
}

bool
Bootcache::insert (beast::IP::Endpoint const& endpoint)
{
    auto const result (m_map.insert (
        value_type (endpoint, 0)));
    if (result.second)
    {
        JLOG(m_journal.trace()) << beast::leftw (18) <<
            "Bootcache insert " << endpoint;
        prune ();
        flagForUpdate();
    }
    return result.second;
}

bool
Bootcache::insertStatic (beast::IP::Endpoint const& endpoint)
{
    auto result (m_map.insert (
        value_type (endpoint, staticValence)));

    if (! result.second && (result.first->right.valence() < staticValence))
    {
        // An existing entry has too low a valence, replace it
        m_map.erase (result.first);
        result = m_map.insert (
            value_type (endpoint, staticValence));
    }

    if (result.second)
    {
        JLOG(m_journal.trace()) << beast::leftw (18) <<
            "Bootcache insert " << endpoint;
        prune ();
        flagForUpdate();
    }
    return result.second;
}

void
Bootcache::on_success (beast::IP::Endpoint const& endpoint)
{
    auto result (m_map.insert (
        value_type (endpoint, 1)));
    if (result.second)
    {
        prune ();
    }
    else
    {
        Entry entry (result.first->right);
        if (entry.valence() < 0)
            entry.valence() = 0;
        ++entry.valence();
        m_map.erase (result.first);
        result = m_map.insert (
            value_type (endpoint, entry));
        assert (result.second);
    }
    Entry const& entry (result.first->right);
    JLOG(m_journal.info()) << beast::leftw (18) <<
        "Bootcache connect " << endpoint <<
        " with " << entry.valence() <<
        ((entry.valence() > 1) ? " successes" : " success");
    flagForUpdate();
}

void
Bootcache::on_failure (beast::IP::Endpoint const& endpoint)
{
    auto result (m_map.insert (
        value_type (endpoint, -1)));
    if (result.second)
    {
        prune();
    }
    else
    {
        Entry entry (result.first->right);
        if (entry.valence() > 0)
            entry.valence() = 0;
        --entry.valence();
        m_map.erase (result.first);
        result = m_map.insert (
            value_type (endpoint, entry));
        assert (result.second);
    }
    Entry const& entry (result.first->right);
    auto const n (std::abs (entry.valence()));
    JLOG(m_journal.debug()) << beast::leftw (18) <<
        "Bootcache failed " << endpoint <<
        " with " << n <<
        ((n > 1) ? " attempts" : " attempt");
    flagForUpdate();
}

void
Bootcache::periodicActivity ()
{
    checkUpdate();
}

//--------------------------------------------------------------------------

void
Bootcache::onWrite (beast::PropertyStream::Map& map)
{
    beast::PropertyStream::Set entries ("entries", map);
    for (auto iter = m_map.right.begin(); iter != m_map.right.end(); ++iter)
    {
        beast::PropertyStream::Map entry (entries);
        entry["endpoint"] = iter->get_left().to_string();
        entry["valence"] = std::int32_t (iter->get_right().valence());
    }
}

    // Checks the cache size and prunes if its over the limit.
void
Bootcache::prune ()
{
    if (size() <= Tuning::bootcacheSize)
        return;

    // Calculate the amount to remove
    auto count ((size() *
        Tuning::bootcachePrunePercent) / 100);
    decltype(count) pruned (0);

    // Work backwards because bimap doesn't handle
    // erasing using a reverse iterator very well.
    //
    for (auto iter (m_map.right.end());
        count-- > 0 && iter != m_map.right.begin(); ++pruned)
    {
        --iter;
        beast::IP::Endpoint const& endpoint (iter->get_left());
        Entry const& entry (iter->get_right());
        JLOG(m_journal.trace()) << beast::leftw (18) <<
            "Bootcache pruned" << endpoint <<
            " at valence " << entry.valence();
        iter = m_map.right.erase (iter);
    }

    JLOG(m_journal.debug()) << beast::leftw (18) <<
        "Bootcache pruned " << pruned << " entries total";
}

// Updates the Store with the current set of entries if needed.
void
Bootcache::update ()
{
    if (! m_needsUpdate)
        return;
    std::vector <Store::Entry> list;
    list.reserve (m_map.size());
    for (auto const& e : m_map)
    {
        Store::Entry se;
        se.endpoint = e.get_left();
        se.valence = e.get_right().valence();
        list.push_back (se);
    }
    m_store.save (list);
    // Reset the flag and cooldown timer
    m_needsUpdate = false;
    m_whenUpdate = m_clock.now() + Tuning::bootcacheCooldownTime;
}

// Checks the clock and calls update if we are off the cooldown.
void
Bootcache::checkUpdate ()
{
    if (m_needsUpdate && m_whenUpdate < m_clock.now())
        update ();
}

// Called when changes to an entry will affect the Store.
void
Bootcache::flagForUpdate ()
{
    m_needsUpdate = true;
    checkUpdate ();
}

}
}
