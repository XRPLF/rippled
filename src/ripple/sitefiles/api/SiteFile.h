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

#ifndef RIPPLE_SITEFILES_SITEFILE_H_INCLUDED
#define RIPPLE_SITEFILES_SITEFILE_H_INCLUDED

#include <ripple/sitefiles/api/Section.h>
#include <ripple/common/UnorderedContainers.h>

#include <string>
#include <unordered_map>

namespace ripple {
namespace SiteFiles {

class SiteFile
{
public:
    SiteFile (int = 0); // dummy argument for emplace

    typedef ripple::unordered_map <std::string, Section> SectionsType;

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

}
}

#endif
