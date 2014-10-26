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

#ifndef RIPPLE_SITEFILES_SITEFILES_H_INCLUDED
#define RIPPLE_SITEFILES_SITEFILES_H_INCLUDED

#include <beast/threads/Stoppable.h>
#include <beast/utility/PropertyStream.h>
#include <map>
#include <string>
#include <vector>

namespace ripple {
namespace SiteFiles {

/** A Site File section.
    Each section has a name, an associative map of key/value pairs,
    and a vector of zero or more free-form data strings.
*/
// VFALCO NOTE This could use ripple::Section instead, which
//             seems to offer the same functionality
//
class Section
{
public:
    typedef std::map <std::string, std::string> MapType;
    typedef std::vector <std::string> DataType;

    Section(int = 0); // dummy argument for emplace()

    // Observers
    std::string const& get (std::string const& key) const;
    std::string const& operator[] (std::string const& key) const;
    DataType const& data() const;

    // Modifiers
    void set (std::string const& key, std::string const& value);
    std::string& operator[] (std::string const& key);
    void push_back (std::string const& data);

private:
    MapType m_map;
    DataType m_data;
};

//------------------------------------------------------------------------------

class SiteFile
{
public:
    SiteFile (int = 0); // dummy argument for emplace

    typedef std::map <std::string, Section> SectionsType;

    /** Retrieve a section by name. */
    /** @{ */
    Section const& get (std::string const& name) const;
    Section const& operator[] (std::string const& key) const;
    /** @} */

    /** Retrieve or create a section with the specified name. */
    Section& insert (std::string const& name);

private:
    SectionsType m_sections;
};

//------------------------------------------------------------------------------

/** SiteFiles listeners receive notifications on new files and sections.
    Calls are made on an implementation-defined, unspecified thread.
    Subclasses implementations should not perform blocking i/o or take
    a long time.
*/
class Listener
{
public:
    /** Called every time a new site file is retrieved.
        Notifications for Site files retrieved before a listener was added will
        be sent at the time the listener is added.
    */
    virtual void onSiteFileFetch (
        std::string const& name, SiteFile const& siteFile) = 0;
};

//------------------------------------------------------------------------------

/** Fetches and maintains a collection of ripple.txt files from domains. */
class Manager
    : public beast::Stoppable
    , public beast::PropertyStream::Source
{
protected:
    explicit Manager (Stoppable& parent);

public:
    /** Create a new Manager. */
    static Manager* New (beast::Stoppable& parent, beast::Journal journal);

    /** Destroy the object.
        Any pending fetch operations are aborted.
    */
    virtual ~Manager() = default;

    /** Adds a listener. */
    virtual void addListener (Listener& listener) = 0;

    /** Remove a listener. */
    virtual void removeListener (Listener& listener) = 0;

    /** Add a URL leading to a ripple.txt file.
        This call does not block. The URL will be fetched asynchronously.
        Parsing errors are reported to the journal.
    */
    virtual void addURL (std::string const& urlstr) = 0;
};

}
}

#endif
