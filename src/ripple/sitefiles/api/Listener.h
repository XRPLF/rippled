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

#ifndef RIPPLE_SITEFILES_LISTENER_H_INCLUDED
#define RIPPLE_SITEFILES_LISTENER_H_INCLUDED

#include <ripple/sitefiles/api/SiteFile.h>

namespace ripple {
namespace SiteFiles {

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

}
}

#endif
