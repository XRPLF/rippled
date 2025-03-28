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

#include <xrpld/app/ledger/AcceptedLedgerTx.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

AcceptedLedgerTx::AcceptedLedgerTx(
    std::shared_ptr<ReadView const> const& ledger,
    std::shared_ptr<STTx const> const& txn,
    std::shared_ptr<STObject const> const& met)
    : mTxn(txn)
    , mMeta(txn->getTransactionID(), ledger->seq(), *met)
    , mAffected(mMeta.getAffectedAccounts())
{
    XRPL_ASSERT(
        !ledger->open(),
        "ripple::AcceptedLedgerTx::AcceptedLedgerTx : valid ledger state");

    Serializer s;
    met->add(s);
    mRawMeta = std::move(s.modData());

    mJson = Json::objectValue;
    mJson[jss::transaction] = mTxn->getJson(JsonOptions::none);

    mJson[jss::meta] = mMeta.getJson(JsonOptions::none);
    mJson[jss::raw_meta] = strHex(mRawMeta);

    mJson[jss::result] = transHuman(mMeta.getResultTER());

    if (!mAffected.empty())
    {
        Json::Value& affected = (mJson[jss::affected] = Json::arrayValue);
        for (auto const& account : mAffected)
            affected.append(toBase58(account));
    }

    if (mTxn->getTxnType() == ttOFFER_CREATE)
    {
        auto const& account = mTxn->getAccountID(sfAccount);
        auto const amount = mTxn->getFieldAmount(sfTakerGets);

        // If the offer create is not self funded then add the owner balance
        if (account != amount.issue().account)
        {
            auto const ownerFunds = accountFunds(
                *ledger,
                account,
                amount,
                fhIGNORE_FREEZE,
                beast::Journal{beast::Journal::getNullSink()});
            mJson[jss::transaction][jss::owner_funds] = ownerFunds.getText();
        }
    }
}

std::string
AcceptedLedgerTx::getEscMeta() const
{
    XRPL_ASSERT(
        !mRawMeta.empty(),
        "ripple::AcceptedLedgerTx::getEscMeta : metadata is set");
    return sqlBlobLiteral(mRawMeta);
}

}  // namespace ripple
