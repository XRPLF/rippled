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

#ifndef BEAST_HTTP_URL_H_INCLUDED
#define BEAST_HTTP_URL_H_INCLUDED

#include "../strings/String.h"

namespace beast {

/** A URL.
    The accompanying robust parser is hardened against all forms of attack.
*/
class URL
{
public:
    /** Construct an empty URL. */
    explicit URL ();

    /** Construct a URL from it's components. */
    URL (
        String schema_,
        String host_,
        uint16 port_,
        String port_string_,
        String path_,
        String query_ = "",
        String fragment_ = "",
        String userinfo_ = "");

    /** Copy construct a URL. */
    URL (URL const& other);

    /** Copy assign a URL. */
    URL& operator= (URL const& other);

    /** Returns `true` if this is an empty URL. */
    bool empty () const;

    /** Returns the scheme of the URL.
        If no scheme was specified, the string will be empty.
    */
    String scheme () const;

    /** Returns the host of the URL.
        If no host was specified, the string will be empty.
    */
    String host () const;

    /** Returns the port number as an integer.
        If no port was specified, the value will be zero.
    */
    uint16 port () const;

    /** Returns the port number as a string.
        If no port was specified, the string will be empty.
    */
    String port_string () const;

    /** Returns the path of the URL.
        If no path was specified, the string will be empty.
    */
    String path () const;

    /** Returns the query parameters portion of the URL.
        If no query parameters were present, the string will be empty.
    */
    String query () const;

    /** Returns the URL fragment, if any. */
    String fragment () const;

    /** Returns the user information, if any. */
    String userinfo () const;

    /** Retrieve the full URL as a single string. */
    String full () const;

private:
    String m_scheme;
    String m_host;
    uint16 m_port;
    String m_port_string;
    String m_path;
    String m_query;
    String m_fragment;
    String m_userinfo;
};

/** URL comparisons. */
/** @{ */
inline bool operator== (URL const& lhs, URL const& rhs) { return    lhs.full() == rhs.full(); }
inline bool operator!= (URL const& lhs, URL const& rhs) { return ! (lhs.full() == rhs.full()); }
inline bool operator<  (URL const& lhs, URL const& rhs) { return    lhs.full() <  rhs.full(); }
inline bool operator>  (URL const& lhs, URL const& rhs) { return    rhs.full() <  lhs.full(); }
inline bool operator<= (URL const& lhs, URL const& rhs) { return ! (rhs.full() <  lhs.full()); }
inline bool operator>= (URL const& lhs, URL const& rhs) { return ! (lhs.full() <  rhs.full()); }
/** @} */

extern std::size_t hash_value (beast::URL const& url);

}

//------------------------------------------------------------------------------

namespace std {

template <typename T>
struct hash;

template <>
struct hash <beast::URL>
{
    std::size_t operator() (beast::URL const& v) const
        { return beast::hash_value (v); }
};

}

//------------------------------------------------------------------------------

#endif
