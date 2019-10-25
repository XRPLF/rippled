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
#include <boost/beast/core/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>
#include <algorithm>
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
        std::map <std::string, std::string, boost::beast::iless>>
{
private:
    std::string name_;
    std::vector <std::string> lines_;
    std::vector <std::string> values_;
    bool had_trailing_comments_ = false;

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
        if (lines_.empty ())
            return "";
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

    /// Returns a value if present, else another value.
    template<class T>
    T
    value_or(std::string const& name, T const& other) const
    {
        auto const v = get<T>(name);
        return v.is_initialized() ? *v : other;
    }

    // indicates if trailing comments were seen
    // during the appending of any lines/values
    bool had_trailing_comments() const { return had_trailing_comments_; }

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
    std::map <std::string, Section, boost::beast::iless> map_;

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

    // indicates if trailing comments were seen
    // in any loaded Sections
    bool had_trailing_comments() const {
        return std::any_of(map_.cbegin(), map_.cend(),
            [](auto s){ return s.second.had_trailing_comments(); });
    }

protected:
    void
    build (IniFileSections const& ifs);
};

//------------------------------------------------------------------------------

/** Set a value from a configuration Section
    If the named value is not found or doesn't parse as a T,
    the variable is unchanged.
    @return `true` if value was set.
*/
template <class T>
bool
set (T& target, std::string const& name, Section const& section)
{
    bool found_and_valid = false;
    try
    {
        auto const val = section.get<T> (name);
        if ((found_and_valid = val.is_initialized()))
            target = *val;
    }
    catch (boost::bad_lexical_cast&)
    {
    }
    return found_and_valid;
}

/** Set a value from a configuration Section
    If the named value is not found or doesn't cast to T,
    the variable is assigned the default.
    @return `true` if the named value was found and is valid.
*/
template <class T>
bool
set (T& target, T const& defaultValue,
    std::string const& name, Section const& section)
{
    bool found_and_valid = set<T>(target, name, section);
    if (!found_and_valid)
        target = defaultValue;
    return found_and_valid;
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
    try
    {
        return section.value_or<T> (name, defaultValue);
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
    bool found_and_valid = false;
    try
    {
        auto const val = section.get<std::string> (name);
        if ((found_and_valid = val.is_initialized()))
            return *val;
    }
    catch (boost::bad_lexical_cast&)
    {
    }
    return defaultValue;
}

template <class T>
bool
get_if_exists (Section const& section,
    std::string const& name, T& v)
{
    return set<T>(v, name, section);
}

template <>
inline
bool
get_if_exists<bool> (Section const& section,
    std::string const& name, bool& v)
{
    int intVal = 0;
    auto stat = get_if_exists(section, name, intVal);
    if (stat)
        v = bool (intVal);
    return stat;
}

} // ripple

#endif
