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

class FeaturesImpl;

class FeeVoteImpl : public FeeVote
{
private:
    
    template <typename Integer>
    class VotableInteger
    {
    public:
        VotableInteger (Integer current, Integer target) 
            : mCurrent (current)
            , mTarget (target)
        {
            // Add our vote
            ++mVoteMap[mTarget];
        }

        bool
        mayVote () const
        {
            // If we love the current setting, we will not vote
            return mCurrent != mTarget;
        }

        void
        addVote (Integer vote)
        {
            ++mVoteMap[vote];
        }

        void
        noVote ()
        {
            addVote (mCurrent);
        }

        Integer
        getVotes ()
        {
            Integer ourVote = mCurrent;
            int weight = 0;

            typedef typename std::map<Integer, int>::value_type mapVType;
            for (auto const& e : mVoteMap)
            {
                // Take most voted value between current and target, inclusive
                if ((e.first <= std::max (mTarget, mCurrent)) &&
                        (e.first >= std::min (mTarget, mCurrent)) &&
                        (e.second > weight))
                {
                    ourVote = e.first;
                    weight = e.second;
                }
            }

            return ourVote;
        }

    private:
        Integer mCurrent;   // The current setting
        Integer mTarget;    // The setting we want
        std::map<Integer, int> mVoteMap;
    };

public:
    FeeVoteImpl (std::uint64_t targetBaseFee, std::uint32_t targetReserveBase,
             std::uint32_t targetReserveIncrement, beast::Journal journal)
        : mTargetBaseFee (targetBaseFee)
        , mTargetReserveBase (targetReserveBase)
        , mTargetReserveIncrement (targetReserveIncrement)
        , m_journal (journal)
    {
    }

    //--------------------------------------------------------------------------

    void
    doValidation (Ledger::ref lastClosedLedger, STObject& baseValidation) override
    {
        if (lastClosedLedger->getBaseFee () != mTargetBaseFee)
        {
            if (m_journal.info) m_journal.info <<
                "Voting for base fee of " << mTargetBaseFee;
            
            baseValidation.setFieldU64 (sfBaseFee, mTargetBaseFee);
        }

        if (lastClosedLedger->getReserve (0) != mTargetReserveBase)
        {
            if (m_journal.info) m_journal.info <<
                "Voting for base resrve of " << mTargetReserveBase;

            baseValidation.setFieldU32(sfReserveBase, mTargetReserveBase);
        }

        if (lastClosedLedger->getReserveInc () != mTargetReserveIncrement)
        {
            if (m_journal.info) m_journal.info <<
                "Voting for reserve increment of " << mTargetReserveIncrement;

            baseValidation.setFieldU32 (sfReserveIncrement, mTargetReserveIncrement);
        }
    }

    //--------------------------------------------------------------------------

    void
    doVoting (Ledger::ref lastClosedLedger, SHAMap::ref initialPosition) override
    {
        // LCL must be flag ledger
        assert ((lastClosedLedger->getLedgerSeq () % 256) == 0);

        VotableInteger<std::uint64_t> baseFeeVote (lastClosedLedger->getBaseFee (), mTargetBaseFee);
        VotableInteger<std::uint32_t> baseReserveVote (lastClosedLedger->getReserve (0), mTargetReserveBase);
        VotableInteger<std::uint32_t> incReserveVote (lastClosedLedger->getReserveInc (), mTargetReserveIncrement);

        // get validations for ledger before flag
        ValidationSet set = getApp().getValidations ().getValidations (lastClosedLedger->getParentHash ());
        for (auto const& e : set)
        {
            SerializedValidation const& val = *e.second;

            if (val.isTrusted ())
            {
                if (val.isFieldPresent (sfBaseFee))
                {
                    baseFeeVote.addVote (val.getFieldU64 (sfBaseFee));
                }
                else
                {
                    baseFeeVote.noVote ();
                }

                if (val.isFieldPresent (sfReserveBase))
                {
                    baseReserveVote.addVote (val.getFieldU32 (sfReserveBase));
                }
                else
                {
                    baseReserveVote.noVote ();
                }

                if (val.isFieldPresent (sfReserveIncrement))
                {
                    incReserveVote.addVote (val.getFieldU32 (sfReserveIncrement));
                }
                else
                {
                    incReserveVote.noVote ();
                }
            }
        }

        // choose our positions
        std::uint64_t baseFee = baseFeeVote.getVotes ();
        std::uint32_t baseReserve = baseReserveVote.getVotes ();
        std::uint32_t incReserve = incReserveVote.getVotes ();

        // add transactions to our position
        if ((baseFee != lastClosedLedger->getBaseFee ()) ||
                (baseReserve != lastClosedLedger->getReserve (0)) ||
                (incReserve != lastClosedLedger->getReserveInc ()))
        {
            if (m_journal.warning) m_journal.warning <<
                "We are voting for a fee change: " << baseFee <<
                "/" << baseReserve <<
                "/" << incReserve;

            SerializedTransaction trans (ttFEE);
            trans.setFieldAccount (sfAccount, uint160 ());
            trans.setFieldU64 (sfBaseFee, baseFee);
            trans.setFieldU32 (sfReferenceFeeUnits, 10);
            trans.setFieldU32 (sfReserveBase, baseReserve);
            trans.setFieldU32 (sfReserveIncrement, incReserve);

            uint256 txID = trans.getTransactionID ();

            if (m_journal.warning) m_journal.warning <<
                "Vote: " << txID;

            Serializer s;
            trans.add (s, true);

            SHAMapItem::pointer tItem = std::make_shared<SHAMapItem> (txID, s.peekData ());

            if (!initialPosition->addGiveItem (tItem, true, false))
            {
                if (m_journal.warning) m_journal.warning <<
                    "Ledger already had fee change";
            }
        }
    }

private:
    std::uint64_t mTargetBaseFee;
    std::uint32_t mTargetReserveBase;
    std::uint32_t mTargetReserveIncrement;
    beast::Journal m_journal;
};

//------------------------------------------------------------------------------

std::unique_ptr<FeeVote>
make_FeeVote (std::uint64_t targetBaseFee, std::uint32_t targetReserveBase,
    std::uint32_t targetReserveIncrement, beast::Journal journal)
{
    return std::make_unique<FeeVoteImpl> (targetBaseFee, targetReserveBase,
        targetReserveIncrement, journal);
}

} // ripple
