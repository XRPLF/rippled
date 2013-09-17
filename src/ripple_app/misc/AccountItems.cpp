//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

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

            AccountItem::pointer item = mOfType->makeItem (accountID, sleCur);

            // VFALCO NOTE Under what conditions would makeItem() return nullptr?
            if (item)
            {
                mItems.push_back (item);
            }
        }

        uint64 uNodeNext    = ownerDir->getFieldU64 (sfIndexNext);

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

// vim:ts=4
