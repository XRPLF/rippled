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

#include <ripple/basics/CountedObject.h>

namespace ripple {

CountedObjects& CountedObjects::getInstance ()
{
    static CountedObjects instance;

    return instance;
}

CountedObjects::CountedObjects ()
    : m_count (0)
    , m_head (nullptr)
{
}

CountedObjects::~CountedObjects() = default;

CountedObjects::List CountedObjects::getCounts (int minimumThreshold) const
{
    List counts;

    // When other operations are concurrent, the count
    // might be temporarily less than the actual count.
    int const count = m_count.load ();

    counts.reserve (count);

    CounterBase* counter = m_head.load ();

    while (counter != nullptr)
    {
        if (counter->getCount () >= minimumThreshold)
        {
            Entry entry;

            entry.first = counter->getName ();
            entry.second = counter->getCount ();

            counts.push_back (entry);
        }

        counter = counter->getNext ();
    }

    return counts;
}

//------------------------------------------------------------------------------

CountedObjects::CounterBase::CounterBase ()
    : m_count (0)
{
    // Insert ourselves at the front of the lock-free linked list

    CountedObjects& instance = CountedObjects::getInstance ();
    CounterBase* head;

    do
    {
        head = instance.m_head.load ();
        m_next = head;
    }
    while (instance.m_head.exchange (this) != head);

    ++instance.m_count;
}

CountedObjects::CounterBase::~CounterBase ()
{
    // VFALCO NOTE If the counters are destroyed before the singleton,
    //             undefined behavior will result if the singleton's member
    //             functions are called.
}

} // ripple
