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
#include <ripple/app/main/Application.h>
#include <ripple/app/ledger/AcceptedLedgerTx.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/types.h>

namespace ripple {

AcceptedLedgerTx::AcceptedLedgerTx (Ledger::ref ledger, SerialIter& sit)
    : mLedger (ledger)
{
    // VFALCO This is making a needless copy
    auto const vl = sit.getVL();
    SerialIter txnIt (make_Slice(vl));

    mTxn =      std::make_shared<STTx> (std::ref (txnIt));
    mRawMeta =  sit.getVL ();
    mMeta =     std::make_shared<TxMeta> (mTxn->getTransactionID (),
        ledger->info().seq, mRawMeta);
    mAffected = mMeta->getAffectedAccounts ();
    mResult =   mMeta->getResultTER ();
    buildJson ();
}

AcceptedLedgerTx::AcceptedLedgerTx (Ledger::ref ledger,
    STTx::ref txn, TxMeta::ref met)
    : mLedger (ledger)
    , mTxn (txn)
    , mMeta (met)
    , mAffected (met->getAffectedAccounts ())
{
    mResult = mMeta->getResultTER ();
    buildJson ();
}

AcceptedLedgerTx::AcceptedLedgerTx (Ledger::ref ledger,
    STTx::ref txn, TER result)
    : mLedger (ledger)
    , mTxn (txn)
    , mResult (result)
    , mAffected (txn->getMentionedAccounts ())
{
    buildJson ();
}

std::string AcceptedLedgerTx::getEscMeta () const
{
    assert (!mRawMeta.empty ());
    return sqlEscape (mRawMeta);
}

void AcceptedLedgerTx::buildJson ()
{
    mJson = Json::objectValue;
    mJson[jss::transaction] = mTxn->getJson (0);

    if (mMeta)
    {
        mJson[jss::meta] = mMeta->getJson (0);
        mJson[jss::raw_meta] = strHex (mRawMeta);
    }

    mJson[jss::result] = transHuman (mResult);

    if (! mAffected.empty ())
    {
        Json::Value& affected = (mJson[jss::affected] = Json::arrayValue);
        for (auto const& account: mAffected)
            affected.append (getApp().accountIDCache().toBase58(account));
    }

    if (mTxn->getTxnType () == ttOFFER_CREATE)
    {
        auto const& account = mTxn->getAccountID(sfAccount);
        auto const amount = mTxn->getFieldAmount (sfTakerGets);

        // If the offer create is not self funded then add the owner balance
        if (account != amount.issue ().account)
        {
            auto const ownerFunds = accountFunds(*mLedger,
                account, amount, fhIGNORE_FREEZE, getConfig());
            mJson[jss::transaction][jss::owner_funds] = ownerFunds.getText ();
        }
    }
}

} // ripple
