//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <ripple/rpc/impl/GetAccountObjects.h>
#include <ripple/app/main/Application.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/JsonFields.h>

namespace ripple {
namespace RPC {

bool
getAccountObjects (ReadView const& ledger, AccountID const& account,
    LedgerEntryType const type, uint256 dirIndex, uint256 const& entryIndex,
    std::uint32_t const limit, Json::Value& jvResult)
{
    auto const rootDirIndex = getOwnerDirIndex (account);
    auto found = false;

    if (dirIndex.isZero ())
    {
        dirIndex = rootDirIndex;
        found = true;
    }

    auto dir = ledger.read({ltDIR_NODE, dirIndex});
    if (! dir)
        return false;

    std::uint32_t i = 0;
    auto& jvObjects = jvResult[jss::account_objects];
    for (;;)
    {
        auto const& entries = dir->getFieldV256 (sfIndexes);
        auto iter = entries.begin ();

        if (! found)
        {
            iter = std::find (iter, entries.end (), entryIndex);
            if (iter == entries.end ())
                return false;

            found = true;
        }

        for (; iter != entries.end (); ++iter)
        {
            auto const sleNode = ledger.read(keylet::child(*iter));
            if (type == ltINVALID || sleNode->getType () == type)
            {
                jvObjects.append (sleNode->getJson (0));

                if (++i == limit)
                {
                    if (++iter != entries.end ())
                    {
                        jvResult[jss::limit] = limit;
                        jvResult[jss::marker] = to_string (dirIndex) + ',' +
                            to_string (*iter);
                        return true;
                    }

                    break;
                }
            }
        }

        auto const nodeIndex = dir->getFieldU64 (sfIndexNext);
        if (nodeIndex == 0)
            return true;

        dirIndex = getDirNodeIndex (rootDirIndex, nodeIndex);
        dir = ledger.read({ltDIR_NODE, dirIndex});
        if (! dir)
            return true;

        if (i == limit)
        {
            auto const& e = dir->getFieldV256 (sfIndexes);
            if (! e.empty ())
            {
                jvResult[jss::limit] = limit;
                jvResult[jss::marker] = to_string (dirIndex) + ',' +
                    to_string (*e.begin ());
            }

            return true;
        }
    }
}

} // RPC
} // ripple
