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

#ifndef RIPPLE_SITEFILES_MANAGER_H_INCLUDED
#define RIPPLE_SITEFILES_MANAGER_H_INCLUDED

#include <ripple/sitefiles/api/Listener.h>

#include <beast/utility/PropertyStream.h>

namespace ripple {
namespace SiteFiles {

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
    virtual ~Manager () { }

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
