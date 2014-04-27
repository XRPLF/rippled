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

#ifndef RIPPLE_STRINGUTILITIES_H
#define RIPPLE_STRINGUTILITIES_H

namespace ripple {

// String utility functions.

// Ripple specific constant used for parsing qualities and other things
//
// NIKB TODO Why is this here instead of somewhere more sensible? What
// "other things" is this being used for?
#define QUALITY_ONE         1000000000  // 10e9

//------------------------------------------------------------------------------

extern std::string strprintf (const char* format, ...);

extern std::string urlEncode (const std::string& strSrc);

// NIKB TODO remove this function - it's only used for some logging in the UNL
//           code which can be trivially rewritten.
template<class Iterator>
std::string strJoin (Iterator first, Iterator last, std::string strSeperator)
{
    std::ostringstream  ossValues;

    for (Iterator start = first; first != last; first++)
    {
        ossValues << str (boost::format ("%s%s") % (start == first ? "" : strSeperator) % *first);
    }

    return ossValues.str ();
}

// NIKB TODO Remove the need for all these overloads. Move them out of here.
inline const std::string strHex (const std::string& strSrc)
{
    return strHex (strSrc.begin (), strSrc.size ());
}

inline std::string strHex (Blob const& vucData)
{
    return strHex (vucData.begin (), vucData.size ());
}

inline std::string strHex (const std::uint64_t uiHost)
{
    uint64_t    uBig    = htobe64 (uiHost);

    return strHex ((unsigned char*) &uBig, sizeof (uBig));
}

inline static std::string sqlEscape (const std::string& strSrc)
{
    static boost::format f ("X'%s'");
    return str (boost::format (f) % strHex (strSrc));
}

inline static std::string sqlEscape (Blob const& vecSrc)
{
    size_t size = vecSrc.size ();

    if (size == 0)
        return "X''";

    std::string j (size * 2 + 3, 0);

    unsigned char* oPtr = reinterpret_cast<unsigned char*> (&*j.begin ());
    const unsigned char* iPtr = &vecSrc[0];

    *oPtr++ = 'X';
    *oPtr++ = '\'';

    for (int i = size; i != 0; --i)
    {
        unsigned char c = *iPtr++;
        *oPtr++ = charHex (c >> 4);
        *oPtr++ = charHex (c & 15);
    }

    *oPtr++ = '\'';
    return j;
}

int strUnHex (std::string& strDst, const std::string& strSrc);

uint64_t uintFromHex (const std::string& strSrc);

std::pair<Blob, bool> strUnHex (const std::string& strSrc);

Blob strCopy (const std::string& strSrc);
std::string strCopy (Blob const& vucSrc);

bool parseIpPort (const std::string& strSource, std::string& strIP, int& iPort);
bool parseQuality (const std::string& strSource, std::uint32_t& uQuality);

inline std::string strGetEnv (const std::string& strKey)
{
    return getenv (strKey.c_str ()) ? getenv (strKey.c_str ()) : "";
}

bool parseUrl (const std::string& strUrl, std::string& strScheme,
               std::string& strDomain, int& iPort, std::string& strPath);

#define ADDRESS(p) strHex(uint64( ((char*) p) - ((char*) 0)))

/** Create a Parameters from a String.

    Parameter strings have the format:

    <key>=<value>['|'<key>=<value>]
*/
extern beast::StringPairArray
parseDelimitedKeyValueString (
    beast::String s, beast::beast_wchar delimiter='|');

/** toString() generalizes std::to_string to handle bools, chars, and strings.

    It's also possible to provide implementation of toString for a class
    which needs a string implementation.
 */

template <class T>
typename std::enable_if<std::is_arithmetic<T>::value,
                        std::string>::type
toString(T t)
{
    return std::to_string(t);
}

inline std::string toString(bool b)
{
    return b ? "true" : "false";
}

inline std::string toString(char c)
{
    return std::string(1, c);
}

inline std::string toString(std::string s)
{
    return s;
}

inline std::string toString(char const* s)
{
    return s;
}

namespace detail {

// ConcatArg is used to represent arguments to stringConcat.

struct ConcatArg {
    ConcatArg(std::string const& s) : data_(s.data()), size_(s.size())
    {
    }

    ConcatArg(char const* s) : data_(s), size_(strlen(s))
    {
    }

    template <typename T>
    ConcatArg(T t) : string_(toString(t)),
                     data_(string_.data()),
                     size_(string_.size())
    {
    }

    std::string string_;
    char const* data_;
    std::size_t size_;
};

} // namespace detail

/** Concatenate strings, numbers, bools and chars into one string in O(n) time.

    Usage:
      stringConcat({"hello ", 23, 'x', true});

    Returns:
      "hello 23xtrue"
 */
inline std::string stringConcat(std::vector<detail::ConcatArg> args)
{
    int capacity = 0;
    for (auto const& a: args)
        capacity += a.size_;

    std::string result;
    result.reserve(capacity);
    for (auto const& a: args)
        result.append(a.data_, a.data_ + a.size_);
    return result;
}

} // ripple

#endif
