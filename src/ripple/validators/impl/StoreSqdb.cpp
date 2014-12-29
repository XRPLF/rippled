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
#include <ripple/validators/impl/StoreSqdb.h>
#include <beast/module/core/text/LexicalCast.h>
#include <beast/utility/Debug.h>
#include <boost/regex.hpp>

namespace ripple {
namespace Validators {

StoreSqdb::StoreSqdb (beast::Journal journal)
    : m_journal (journal)
{
}

StoreSqdb::~StoreSqdb ()
{
}

beast::Error
StoreSqdb::open (beast::File const& file)
{
    beast::Error error (m_session.open (file.getFullPathName ()));

    m_journal.info <<
        "Opening " << file.getFullPathName();

    if (error)
        m_journal.error <<
            "Failed opening database: " << error.what();

    return error;
}

}
}
