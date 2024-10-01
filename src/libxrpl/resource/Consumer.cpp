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

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/detail/Entry.h>
#include <xrpl/resource/detail/Logic.h>

namespace ripple {
namespace Resource {

Consumer::Consumer(Logic& logic, Entry& entry)
    : m_logic(&logic), m_entry(&entry)
{
}

Consumer::Consumer() : m_logic(nullptr), m_entry(nullptr)
{
}

Consumer::Consumer(Consumer const& other)
    : m_logic(other.m_logic), m_entry(nullptr)
{
    if (m_logic && other.m_entry)
    {
        m_entry = other.m_entry;
        m_logic->acquire(*m_entry);
    }
}

Consumer::~Consumer()
{
    if (m_logic && m_entry)
        m_logic->release(*m_entry);
}

Consumer&
Consumer::operator=(Consumer const& other)
{
    // remove old ref
    if (m_logic && m_entry)
        m_logic->release(*m_entry);

    m_logic = other.m_logic;
    m_entry = other.m_entry;

    // add new ref
    if (m_logic && m_entry)
        m_logic->acquire(*m_entry);

    return *this;
}

std::string
Consumer::to_string() const
{
    if (m_logic == nullptr)
        return "(none)";

    return m_entry->to_string();
}

bool
Consumer::isUnlimited() const
{
    if (m_entry)
        return m_entry->isUnlimited();

    return false;
}

Disposition
Consumer::disposition() const
{
    Disposition d = ok;
    if (m_logic && m_entry)
        d = m_logic->charge(*m_entry, Charge(0));

    return d;
}

Disposition
Consumer::charge(Charge const& what)
{
    Disposition d = ok;

    if (m_logic && m_entry && !m_entry->isUnlimited())
        d = m_logic->charge(*m_entry, what);

    return d;
}

bool
Consumer::warn()
{
    ASSERT(
        m_entry != nullptr,
        "ripple::Resource::Consumer::warn : non-null entry");
    return m_logic->warn(*m_entry);
}

bool
Consumer::disconnect(beast::Journal const& j)
{
    ASSERT(
        m_entry != nullptr,
        "ripple::Resource::Consumer::disconnect : non-null entry");
    bool const d = m_logic->disconnect(*m_entry);
    if (d)
    {
        JLOG(j.debug()) << "disconnecting " << m_entry->to_string();
    }
    return d;
}

int
Consumer::balance()
{
    ASSERT(
        m_entry != nullptr,
        "ripple::Resource::Consumer::balance : non-null entry");
    return m_logic->balance(*m_entry);
}

Entry&
Consumer::entry()
{
    ASSERT(
        m_entry != nullptr,
        "ripple::Resource::Consumer::entry : non-null entry");
    return *m_entry;
}

std::ostream&
operator<<(std::ostream& os, Consumer const& v)
{
    os << v.to_string();
    return os;
}

}  // namespace Resource
}  // namespace ripple
