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
#include <ripple/app/transactors/Transactor.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

class CancelOffer
    : public Transactor
{
public:
    CancelOffer (
        STTx const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : Transactor (
            txn,
            params,
            engine,
            deprecatedLogs().journal("CancelOffer"))
    {

    }

    TER preCheck () override
    {
        std::uint32_t const uTxFlags (mTxn.getFlags ());

        if (uTxFlags & tfUniversalMask)
        {
            m_journal.trace << "Malformed transaction: " <<
                "Invalid flags set.";
            return temINVALID_FLAG;
        }

        std::uint32_t const uOfferSequence = mTxn.getFieldU32 (sfOfferSequence);

        if (!uOfferSequence)
        {
            m_journal.trace << "Malformed transaction: " <<
                "No sequence specified.";
            return temBAD_SEQUENCE;
        }

        return Transactor::preCheck ();
    }

    TER doApply () override
    {
        std::uint32_t const uOfferSequence = mTxn.getFieldU32 (sfOfferSequence);

        if (mTxnAccount->getFieldU32 (sfSequence) - 1 <= uOfferSequence)
        {
            m_journal.trace << "Malformed transaction: " <<
                "Sequence " << uOfferSequence << " is invalid.";
            return temBAD_SEQUENCE;
        }

        uint256 const offerIndex (getOfferIndex (mTxnAccountID, uOfferSequence));

        SLE::pointer sleOffer (mEngine->view().entryCache (ltOFFER,
            offerIndex));

        if (sleOffer)
        {
            m_journal.debug << "Trying to cancel offer #" << uOfferSequence;
            return mEngine->view ().offerDelete (sleOffer);
        }

        m_journal.debug << "Offer #" << uOfferSequence << " can't be found.";
        return tesSUCCESS;
    }
};

TER
transact_CancelOffer (
    STTx const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    return CancelOffer (txn, params, engine).apply ();
}

}
