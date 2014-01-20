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

Job::Job (JobType& type, uint64 index)
    : m_type (type)
    , m_index (index)
{
}

Job::Job (Job const& other)
    : m_cancelCallback (other.m_cancelCallback)
    , m_type (other.m_type)
    , m_index (other.m_index)
    , m_work (other.m_work)
    , m_loadEvent (other.m_loadEvent)
    , m_name (other.m_name)
{
}

Job::Job (JobType& type,
          std::string const& name,
          uint64 index,
          std::function <void (Job&)> const& work,
          CancelCallback cancelCallback)
    : m_cancelCallback (cancelCallback)
    , m_type (type)
    , m_index (index)
    , m_work (work)
    , m_name (name)
{
    m_loadEvent = boost::make_shared <LoadEvent> (
        boost::ref (m_type.get ().load), name, false);
}

JobType& Job::getType () const
{
    return m_type;
}

CancelCallback Job::getCancelCallback () const
{
    bassert (! m_cancelCallback.empty());
    return m_cancelCallback;
}

bool Job::shouldCancel () const
{
    if (! m_cancelCallback.empty ())
        return m_cancelCallback ();
    return false;
}

void Job::work ()
{
    m_loadEvent->start ();
    m_loadEvent->reName (m_name);

    m_work (*this);
}

void Job::rename (std::string const& newName)
{
    m_name = newName;
}

bool Job::operator> (Job const& j) const
{
    if (m_type < j.m_type)
        return true;

    if (m_type > j.m_type)
        return false;

    return m_index > j.m_index;
}

bool Job::operator>= (Job const& j) const
{
    if (m_type < j.m_type)
        return true;

    if (m_type > j.m_type)
        return false;

    return m_index >= j.m_index;
}

bool Job::operator< (Job const& j) const
{
    if (m_type < j.m_type)
        return false;

    if (m_type > j.m_type)
        return true;

    return m_index < j.m_index;
}

bool Job::operator<= (Job const& j) const
{
    if (m_type < j.m_type)
        return false;

    if (m_type > j.m_type)
        return true;

    return m_index <= j.m_index;
}
