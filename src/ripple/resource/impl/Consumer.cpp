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

namespace ripple {
namespace Resource {

Consumer::Consumer (Logic& logic, Entry& entry)
    : m_logic (&logic)
    , m_entry (&entry)
{
}

Consumer::Consumer ()
    : m_logic (nullptr)
    , m_entry (nullptr)
{
}

Consumer::Consumer (Consumer const& other)
    : m_logic (other.m_logic)
    , m_entry (nullptr)
{
    if (m_logic != nullptr)
    {
        if (other.m_entry != nullptr)
        {
            m_entry = other.m_entry;
            m_logic->acquire (*m_entry);
        }
    }
}

Consumer::~Consumer()
{
    if (m_logic != nullptr)
    {
        if (m_entry != nullptr)
            m_logic->release (*m_entry);
    }
}

Consumer& Consumer::operator= (Consumer const& other)
{
    // remove old ref
    if (m_logic != nullptr)
    {
        if (m_entry != nullptr)
            m_logic->release (*m_entry);
    }

    m_logic = other.m_logic;
    m_entry = other.m_entry;
   
    // add new ref
    if (m_logic != nullptr)
    {
        if (m_entry != nullptr)
            m_logic->acquire (*m_entry);
    }

    return *this;
}

std::string Consumer::label ()
{
    if (m_logic == nullptr)
        return "(none)";

    return m_entry->label();
}

bool Consumer::admin () const
{
    return m_entry->admin();
}

void Consumer::elevate (std::string const& name)
{
    m_entry = &m_logic->elevateToAdminEndpoint (*m_entry, name);
}

Disposition Consumer::disposition() const
{
    return ok;
}

Disposition Consumer::charge (Charge const& what)
{
    return m_logic->charge (*m_entry, what);
}

bool Consumer::warn ()
{
    return m_logic->warn (*m_entry);
}

bool Consumer::disconnect ()
{
    return m_logic->disconnect (*m_entry);
}

int Consumer::balance()
{
    return m_logic->balance (*m_entry);
}

Entry& Consumer::entry()
{
    return *m_entry;
}

}
}
