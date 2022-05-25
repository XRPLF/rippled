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

#ifndef RIPPLE_RESOURCE_CONSUMER_H_INCLUDED
#define RIPPLE_RESOURCE_CONSUMER_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/resource/Charge.h>
#include <ripple/resource/Disposition.h>

namespace ripple {
namespace Resource {

struct Entry;
class Logic;

/** An endpoint that consumes resources. */
class Consumer
{
private:
    friend class Logic;
    Consumer(Logic& logic, Entry& entry);

public:
    Consumer();
    ~Consumer();
    Consumer(Consumer const& other);
    Consumer&
    operator=(Consumer const& other);

    /** Return a human readable string uniquely identifying this consumer. */
    std::string
    to_string() const;

    /** Returns `true` if this is a privileged endpoint. */
    bool
    isUnlimited() const;

    /** Raise the Consumer's privilege level to a Named endpoint.
        The reference to the original endpoint descriptor is released.
    */
    void
    elevate(std::string const& name);

    /** Returns the current disposition of this consumer.
        This should be checked upon creation to determine if the consumer
        should be disconnected immediately.
    */
    Disposition
    disposition() const;

    /** Apply a load charge to the consumer. */
    Disposition
    charge(Charge const& fee);

    /** Returns `true` if the consumer should be warned.
        This consumes the warning.
    */
    bool
    warn();

    /** Returns `true` if the consumer should be disconnected. */
    bool
    disconnect(beast::Journal const& j);

    /** Returns the credit balance representing consumption. */
    int
    balance();

    // Private: Retrieve the entry associated with the consumer
    Entry&
    entry();

private:
    Logic* m_logic;
    Entry* m_entry;
};

std::ostream&
operator<<(std::ostream& os, Consumer const& v);

}  // namespace Resource
}  // namespace ripple

#endif
