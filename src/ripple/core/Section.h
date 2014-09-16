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

#ifndef RIPPLE_CORE_SECTION_H_INCLUDED
#define RIPPLE_CORE_SECTION_H_INCLUDED

#include <beast/utility/ci_char_traits.h>
#include <boost/lexical_cast.hpp>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ripple {

/** Holds a collection of configuration values.
    A configuration file contains zero or more sections.
*/
class Section
{
private:
    std::vector <std::string> lines_;
    std::map <std::string, std::string, beast::ci_less> map_;

public:
    /** Create an empty section. */
    Section() = default;

    /** Append a set of lines to this section.
        Parsable key/value pairs are also added to the map.
    */
    void
    append (std::vector <std::string> const& lines);

    /** Returns `true` if a key with the given name exists. */
    bool
    exists (std::string const& name) const;

    /** Retrieve a key/value pair.
        @return A pair with bool `true` if the string was found.
    */
    std::pair <std::string, bool>
    find (std::string const& name) const;
};

/** Set a value from a configuration Section
    If the named value is not found, the variable is unchanged.
    @return `true` if value was set.
*/
template <class T>
bool
set (T& target, std::string const& name, Section const& section)
{
    auto const result = section.find (name);
    if (! result.second)
        return false;
    try
    {
        target = boost::lexical_cast <T> (result.first);
        return true;
    }
    catch(...)
    {
    }
    return false;
}

/** Set a value from a configuration Section
    If the named value is not found, the variable is assigned the default.
    @return `true` if named value was found in the Section.
*/
template <class T>
bool
set (T& target, T const& defaultValue,
    std::string const& name, Section const& section)
{
    auto const result = section.find (name);
    if (! result.second)
        return false;
    try
    {
        // VFALCO TODO Use try_lexical_convert (boost 1.56.0)
        target = boost::lexical_cast <T> (result.first);
        return true;
    }
    catch(...)
    {
        target = defaultValue;
    }
    return false;
}

/** Retrieve a key/value pair from a section.
    @return The value string converted to T if it exists
            and can be parsed, or else defaultValue.
*/
// NOTE This routine might be more clumsy than the previous two
template <class T>
T
get (Section const& section,
    std::string const& name, T const& defaultValue = T{})
{
    auto const result = section.find (name);
    if (! result.second)
        return defaultValue;
    try
    {
        return boost::lexical_cast <T> (result.first);
    }
    catch(...)
    {
    }
    return defaultValue;
}

} // ripple

#endif

