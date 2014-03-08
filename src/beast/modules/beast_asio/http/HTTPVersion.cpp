//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

namespace beast {

HTTPVersion::HTTPVersion ()
    : m_major (0)
    , m_minor (0)
{
}

HTTPVersion::HTTPVersion (unsigned short major_, unsigned short minor_)
    : m_major (major_)
    , m_minor (minor_)
{
}

HTTPVersion::HTTPVersion (HTTPVersion const& other)
    : m_major (other.m_major)
    , m_minor (other.m_minor)
{
}

HTTPVersion& HTTPVersion::operator= (HTTPVersion const& other)
{
    m_major = other.m_major;
    m_minor = other.m_minor;
    return *this;
}

String HTTPVersion::toString () const
{
    return String::fromNumber (vmajor ()) + "." +
           String::fromNumber (vminor ());
}

unsigned short HTTPVersion::vmajor () const
{
    return m_major;
}

unsigned short HTTPVersion::vminor () const
{
    return m_minor;
}

bool HTTPVersion::operator== (HTTPVersion const& rhs) const
{
    return (m_major == rhs.m_major) && (m_minor == rhs.m_minor);
}

bool HTTPVersion::operator!= (HTTPVersion const& rhs) const
{
    return (m_major != rhs.m_major) || (m_minor != rhs.m_minor);
}

bool HTTPVersion::operator>  (HTTPVersion const& rhs) const
{
    return (m_major > rhs.m_major) ||
           ((m_major == rhs.m_major) && (m_minor > rhs.m_minor));
}

bool HTTPVersion::operator>= (HTTPVersion const& rhs) const
{
    return (m_major > rhs.m_major) ||
           ((m_major == rhs.m_major) && (m_minor >= rhs.m_minor));
}

bool HTTPVersion::operator<  (HTTPVersion const& rhs) const
{
    return (m_major < rhs.m_major) ||
           ((m_major == rhs.m_major) && (m_minor < rhs.m_minor));
}

bool HTTPVersion::operator<= (HTTPVersion const& rhs) const
{
    return (m_major < rhs.m_major) ||
           ((m_major == rhs.m_major) && (m_minor <= rhs.m_minor));
}

}
