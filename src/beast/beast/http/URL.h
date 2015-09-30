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

#include <ios>
#include <utility>

namespace beast {

/** A URL.
    The accompanying robust parser is hardened against all forms of attack.
*/
class URL
{
public:
    /** Construct a URL from it's components. */
    URL (
        std::string schema_,
        std::string host_,
        std::uint16_t port_,
        std::string port_string_,
        std::string path_,
        std::string query_ = "",
        std::string fragment_ = "",
        std::string userinfo_ = "");

    /** Construct an empty URL. */
    explicit URL () = default;

    /** Copy construct a URL. */
    URL (URL const& other) = default;

    /** Copy assign a URL. */
    URL& operator= (URL const& other) = default;

    /** Move construct a URL. */
    URL (URL&& other) = default;

    /** Returns `true` if this is an empty URL. */
    bool
    empty () const;

    /** Returns the scheme of the URL.
        If no scheme was specified, the string will be empty.
    */
    std::string const&
    scheme () const;

    /** Returns the host of the URL.
        If no host was specified, the string will be empty.
    */
    std::string const&
    host () const;

    /** Returns the port number as an integer.
        If no port was specified, the value will be zero.
    */
    std::uint16_t
    port () const;

    /** Returns the port number as a string.
        If no port was specified, the string will be empty.
    */
    std::string const&
    port_string () const;

    /** Returns the path of the URL.
        If no path was specified, the string will be empty.
    */
    std::string const&
    path () const;

    /** Returns the query parameters portion of the URL.
        If no query parameters were present, the string will be empty.
    */
    std::string const&
    query () const;

    /** Returns the URL fragment, if any. */
    std::string const&
    fragment () const;

    /** Returns the user information, if any. */
    std::string const&
    userinfo () const;

private:
    std::string m_scheme;
    std::string m_host;
    std::uint16_t m_port = 0;
    std::string m_port_string;
    std::string m_path;
    std::string m_query;
    std::string m_fragment;
    std::string m_userinfo;
};

/** Attempt to parse a string into a URL */
std::pair<bool, URL>
parse_URL(std::string const&);

/** Retrieve the full URL as a single string. */
std::string
to_string(URL const& url);

/** Output stream conversion. */
std::ostream&
operator<< (std::ostream& os, URL const& url);

/** URL comparisons. */
/** @{ */
inline bool
operator== (URL const& lhs, URL const& rhs)
{
    return to_string (lhs) == to_string (rhs);
}

inline bool
operator!= (URL const& lhs, URL const& rhs)
{
    return to_string (lhs) != to_string (rhs);
}

inline bool
operator<  (URL const& lhs, URL const& rhs)
{
    return to_string (lhs) < to_string (rhs);
}

inline bool operator>  (URL const& lhs, URL const& rhs)
{
    return to_string (rhs) < to_string (lhs);
}

inline bool
operator<= (URL const& lhs, URL const& rhs)
{
    return ! (to_string (rhs) < to_string (lhs));
}

inline bool
operator>= (URL const& lhs, URL const& rhs)
{
    return ! (to_string (lhs) < to_string (rhs));
}
/** @} */

/** boost::hash support */
template <class Hasher>
inline
void
hash_append (Hasher& h, URL const& url)
{
    using beast::hash_append;
    hash_append (h, to_string (url));
}

}

//------------------------------------------------------------------------------

namespace std {

template <>
struct hash <beast::URL>
{
    std::size_t operator() (beast::URL const& url) const
    {
        return std::hash<std::string>{} (to_string (url));
    }
};

}

//------------------------------------------------------------------------------

#endif
