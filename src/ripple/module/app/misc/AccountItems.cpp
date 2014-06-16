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

namespace ripple {

AccountItems::AccountItems (uint160 const& accountID,
                            Ledger::ref ledger,
                            AccountItem::pointer ofType)
{
    mOfType = ofType;

    fillItems (accountID, ledger);
}

void AccountItems::fillItems (const uint160& accountID, Ledger::ref ledger)
{
    uint256 const rootIndex = Ledger::getOwnerDirIndex (accountID);
    uint256 currentIndex    = rootIndex;

    // VFALCO TODO Rewrite all infinite loops to have clear terminating
    //             conditions defined in one location.
    //
    while (1)
    {
        SLE::pointer ownerDir   = ledger->getDirNode (currentIndex);

        // VFALCO TODO Rewrite to not return from the middle of the function
        if (!ownerDir)
            return;

        BOOST_FOREACH (uint256 const & uNode, ownerDir->getFieldV256 (sfIndexes).peekValue ())
        {
            // VFALCO TODO rename getSLEi() to something legible.
            SLE::pointer sleCur = ledger->getSLEi (uNode);

            if (!sleCur)
            {
                // item in directory not in ledger
            }
            else
            {
                AccountItem::pointer item = mOfType->makeItem (accountID, sleCur);

                // VFALCO NOTE Under what conditions would makeItem() return nullptr?
                // DJS NOTE If the item wasn't one this particular AccountItems was interested in
                // (For example, if the owner is only interested in ripple lines and this is an offer)
                if (item)
                {
                    mItems.push_back (item);
                }
            }
        }

        std::uint64_t uNodeNext    = ownerDir->getFieldU64 (sfIndexNext);

        // VFALCO TODO Rewrite to not return from the middle of the function
        if (!uNodeNext)
            return;

        currentIndex = Ledger::getDirNodeIndex (rootIndex, uNodeNext);
    }
}

Json::Value AccountItems::getJson (int v)
{
    Json::Value ret (Json::arrayValue);

    BOOST_FOREACH (AccountItem::ref ai, mItems)
    {
        ret.append (ai->getJson (v));
    }

    return ret;
}

} // ripple

