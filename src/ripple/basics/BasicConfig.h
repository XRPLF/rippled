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

#ifndef RIPPLE_BASICS_BASICCONFIG_H_INCLUDED
#define RIPPLE_BASICS_BASICCONFIG_H_INCLUDED

#include <ripple/basics/contract.h>
#include <beast/unit_test/detail/const_container.hpp>
#include <beast/core/detail/ci_char_traits.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>
#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace ripple {

using IniFileSections = std::map<std::string, std::vector<std::string>>;

//------------------------------------------------------------------------------

/** Holds a collection of configuration values.
    A configuration file contains zero or more sections.
*/
class Section
    : public beast::unit_test::detail::const_container <
        std::map <std::string, std::string, beast::detail::ci_less>>
{
private:
    std::string name_;
    std::vector <std::string> lines_;
    std::vector <std::string> values_;

public:
    /** Create an empty section. */
    explicit
    Section (std::string const& name = "");

    /** Returns the name of this section. */
    std::string const&
    name() const
    {
        return name_;
    }

    /** Returns all the lines in the section.
        This includes everything.
    */
    std::vector <std::string> const&
    lines() const
    {
        return lines_;
    }

    /** Returns all the values in the section.
        Values are non-empty lines which are not key/value pairs.
    */
    std::vector <std::string> const&
    values() const
    {
        return values_;
    }

    /**
     * Set the legacy value for this section.
     */
    void
    legacy (std::string value)
    {
        if (lines_.empty ())
            lines_.emplace_back (std::move (value));
        else
            lines_[0] = std::move (value);
    }

    /**
     * Get the legacy value for this section.
     *
     * @return The retrieved value. A section with an empty legacy value returns
               an empty string.
     */
    std::string
    legacy () const
    {
        if (lines_.size () > 1)
            Throw<std::runtime_error> (
                "A legacy value must have exactly one line. Section: " + name_);
        return lines_[0];
    }

    /** Set a key/value pair.
        The previous value is discarded.
    */
    void
    set (std::string const& key, std::string const& value);

    /** Append a set of lines to this section.
        Lines containing key/value pairs are added to the map,
        else they are added to the values list. Everything is
        added to the lines list.
    */
    void
    append (std::vector <std::string> const& lines);

    /** Append a line to this section. */
    void
    append (std::string const& line)
    {
        append (std::vector<std::string>{ line });
    }

    /** Returns `true` if a key with the given name exists. */
    bool
    exists (std::string const& name) const;

    /** Retrieve a key/value pair.
        @return A pair with bool `true` if the string was found.
    */
    std::pair <std::string, bool>
    find (std::string const& name) const;

    template <class T>
    boost::optional<T>
    get (std::string const& name) const
    {
        auto const iter = cont().find(name);
        if (iter == cont().end())
            return boost::none;
        return boost::lexical_cast<T>(iter->second);
    }

    friend
    std::ostream&
    operator<< (std::ostream&, Section const& section);
};

//------------------------------------------------------------------------------

/** Holds unparsed configuration information.
    The raw data sections are processed with intermediate parsers specific
    to each module instead of being all parsed in a central location.
*/
class BasicConfig
{
private:
    std::map <std::string, Section, beast::detail::ci_less> map_;

public:
    /** Returns `true` if a section with the given name exists. */
    bool
    exists (std::string const& name) const;

    /** Returns the section with the given name.
        If the section does not exist, an empty section is returned.
    */
    /** @{ */
    Section&
    section (std::string const& name);

    Section const&
    section (std::string const& name) const;

    Section const&
    operator[] (std::string const& name) const
    {
        return section(name);
    }

    Section&
    operator[] (std::string const& name)
    {
        return section(name);
    }
    /** @} */

    /** Overwrite a key/value pair with a command line argument
        If the section does not exist it is created.
        The previous value, if any, is overwritten.
    */
    void
    overwrite (std::string const& section, std::string const& key,
        std::string const& value);

    /** Remove all the key/value pairs from the section.
     */
    void
    deprecatedClearSection (std::string const& section);

    /**
     *  Set a value that is not a key/value pair.
     *
     *  The value is stored as the section's first value and may be retrieved
     *  through section::legacy.
     *
     *  @param section Name of the section to modify.
     *  @param value Contents of the legacy value.
     */
    void
    legacy(std::string const& section, std::string value);

    /**
     *  Get the legacy value of a section. A section with a
     *  single-line value may be retrieved as a legacy value.
     *
     *  @param sectionName Retrieve the contents of this section's
     *         legacy value.
     *  @return Contents of the legacy value.
     */
    std::string
    legacy(std::string const& sectionName) const;

    friend
    std::ostream&
    operator<< (std::ostream& ss, BasicConfig const& c);

protected:
    void
    build (IniFileSections const& ifs);
};

//------------------------------------------------------------------------------

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
    catch (boost::bad_lexical_cast&)
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
    catch (boost::bad_lexical_cast&)
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
    catch (boost::bad_lexical_cast&)
    {
    }
    return defaultValue;
}

inline
std::string
get (Section const& section,
    std::string const& name, const char* defaultValue)
{
    auto const result = section.find (name);
    if (! result.second)
        return defaultValue;
    try
    {
        return boost::lexical_cast <std::string> (result.first);
    }
    catch(std::exception const&)
    {
    }
    return defaultValue;
}

template <class T>
bool
get_if_exists (Section const& section,
    std::string const& name, T& v)
{
    auto const result = section.find (name);
    if (! result.second)
        return false;
    try
    {
        v = boost::lexical_cast <T> (result.first);
        return true;
    }
    catch (boost::bad_lexical_cast&)
    {
    }
    return false;
}

template <>
inline
bool
get_if_exists<bool> (Section const& section,
    std::string const& name, bool& v)
{
    int intVal = 0;
    if (get_if_exists (section, name, intVal))
    {
        v = bool (intVal);
        return true;
    }
    return false;
}

} // ripple

#endif
