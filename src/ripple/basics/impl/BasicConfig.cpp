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

#include <BeastConfig.h>
#include <ripple/basics/BasicConfig.h>
#include <boost/regex.hpp>
#include <algorithm>

namespace ripple {

Section::Section (std::string const& name)
    : name_(name)
{
}

void
Section::set (std::string const& key, std::string const& value)
{
    auto const result = cont().emplace (key, value);
    if (! result.second)
        result.first->second = value;
}

void
Section::append (std::vector <std::string> const& lines)
{
    // <key> '=' <value>
    static boost::regex const re1 (
        "^"                         // start of line
        "(?:\\s*)"                  // whitespace (optonal)
        "([a-zA-Z][_a-zA-Z0-9]*)"   // <key>
        "(?:\\s*)"                  // whitespace (optional)
        "(?:=)"                     // '='
        "(?:\\s*)"                  // whitespace (optional)
        "(.*\\S+)"                  // <value>
        "(?:\\s*)"                  // whitespace (optional)
        , boost::regex_constants::optimize
    );

    lines_.reserve (lines_.size() + lines.size());
    for (auto const& line : lines)
    {
        boost::smatch match;
        lines_.push_back (line);
        if (boost::regex_match (line, match, re1))
            set (match[1], match[2]);
        else
            values_.push_back (line);
    }
}

bool
Section::exists (std::string const& name) const
{
    return cont().find (name) != cont().end();
}

std::pair <std::string, bool>
Section::find (std::string const& name) const
{
    auto const iter = cont().find (name);
    if (iter == cont().end())
        return {{}, false};
    return {iter->second, true};
}

std::ostream&
operator<< (std::ostream& os, Section const& section)
{
    for (auto const& kv : section.cont())
        os << kv.first << "=" << kv.second << "\n";
    return os;
}

//------------------------------------------------------------------------------

bool
BasicConfig::exists (std::string const& name) const
{
    return map_.find(name) != map_.end();
}

Section&
BasicConfig::section (std::string const& name)
{
    return map_[name];
}

Section const&
BasicConfig::section (std::string const& name) const
{
    static Section none("");
    auto const iter = map_.find (name);
    if (iter == map_.end())
        return none;
    return iter->second;
}

void
BasicConfig::overwrite (std::string const& section, std::string const& key,
    std::string const& value)
{
    auto const result = map_.emplace (std::piecewise_construct,
        std::make_tuple(section), std::make_tuple(section));
    result.first->second.set (key, value);
}

void
BasicConfig::deprecatedClearSection (std::string const& section)
{
    auto i = map_.find(section);
    if (i != map_.end())
        i->second = Section(section);
}

void
BasicConfig::legacy(std::string const& section, std::string value)
{
    map_[section].legacy(std::move(value));
}

std::string
BasicConfig::legacy(std::string const& sectionName) const
{
    return section (sectionName).legacy ();
}

void
BasicConfig::build (IniFileSections const& ifs)
{
    for (auto const& entry : ifs)
    {
        auto const result = map_.emplace (std::piecewise_construct,
            std::make_tuple(entry.first), std::make_tuple(entry.first));
        result.first->second.append (entry.second);
    }
}

std::ostream&
operator<< (std::ostream& ss, BasicConfig const& c)
{
    for (auto const& s : c.map_)
        ss << "[" << s.first << "]\n" << s.second;
    return ss;
}

} // ripple
