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

RippleState::pointer RippleState::makeItem (
    Account const& accountID, SerializedLedgerEntry::ref ledgerEntry)
{
    if (!ledgerEntry || ledgerEntry->getType () != ltRIPPLE_STATE)
        return pointer ();

    return pointer (new RippleState (ledgerEntry, accountID));
}

RippleState::RippleState (
        SerializedLedgerEntry::ref ledgerEntry,
        Account const& viewAccount)
    : mLedgerEntry (ledgerEntry)
    , mLowLimit (ledgerEntry->getFieldAmount (sfLowLimit))
    , mHighLimit (ledgerEntry->getFieldAmount (sfHighLimit))
    , mLowID (mLowLimit.getIssuer ())
    , mHighID (mHighLimit.getIssuer ())
    , mBalance (ledgerEntry->getFieldAmount (sfBalance))
{
    mFlags          = mLedgerEntry->getFieldU32 (sfFlags);

    mLowQualityIn   = mLedgerEntry->getFieldU32 (sfLowQualityIn);
    mLowQualityOut  = mLedgerEntry->getFieldU32 (sfLowQualityOut);

    mHighQualityIn  = mLedgerEntry->getFieldU32 (sfHighQualityIn);
    mHighQualityOut = mLedgerEntry->getFieldU32 (sfHighQualityOut);

    mViewLowest = (mLowID == viewAccount);

    if (!mViewLowest)
        mBalance.negate ();
}

Json::Value RippleState::getJson (int)
{
    Json::Value ret (Json::objectValue);
    ret["low_id"] = to_string (mLowID);
    ret["high_id"] = to_string (mHighID);
    return ret;
}

std::vector <RippleState::pointer>
getRippleStateItems (
    Account const& accountID,
    Ledger::ref ledger)
{
    std::vector <RippleState::pointer> items;

    ledger->visitAccountItems (accountID,
        [&items,&accountID](SLE::ref sleCur)
        {
             auto ret = RippleState::makeItem (accountID, sleCur);

             if (ret)
                items.push_back (ret);
        });

    return items;
}

} // ripple
