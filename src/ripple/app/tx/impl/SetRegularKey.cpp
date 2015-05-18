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
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

class SetRegularKey
    : public Transactor
{
    std::uint64_t calculateBaseFee () override
    {
        if ( mTxnAccount
                && (! (mTxnAccount->getFlags () & lsfPasswordSpent))
                && (mSigningPubKey.getAccountID () == mTxnAccountID))
        {
            // flag is armed and they signed with the right account
            return 0;
        }

        return Transactor::calculateBaseFee ();
    }

public:
    SetRegularKey (
        STTx const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : Transactor (
            txn,
            params,
            engine,
            deprecatedLogs().journal("SetRegularKey"))
    {

    }

    TER preCheck () override
    {
        std::uint32_t const uTxFlags = mTxn.getFlags ();

        if (uTxFlags & tfUniversalMask)
        {
            if (m_journal.trace) m_journal.trace <<
                "Malformed transaction: Invalid flags set.";

            return temINVALID_FLAG;
        }

        return Transactor::preCheck ();
    }

    TER doApply () override
    {
        if (mFeeDue == zero)
            mTxnAccount->setFlag (lsfPasswordSpent);

        if (mTxn.isFieldPresent (sfRegularKey))
        {
            mTxnAccount->setFieldAccount (sfRegularKey,
                mTxn.getFieldAccount160 (sfRegularKey));
        }
        else
        {
            if (mTxnAccount->isFlag (lsfDisableMaster))
                return tecMASTER_DISABLED;
            mTxnAccount->makeFieldAbsent (sfRegularKey);
        }

        return tesSUCCESS;
    }
};

TER
transact_SetRegularKey (
    STTx const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    return SetRegularKey(txn, params, engine).apply ();
}

}
