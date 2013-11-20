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

class Features;

//------------------------------------------------------------------------------

class FeeVote : public IFeeVote
{
private:
    // VFALCO TODO rename template parameter (wtf, looks like a macro)
    template <typename INT>
    class VotableInteger
    {
    public:
        VotableInteger (INT current, INT target) : mCurrent (current), mTarget (target)
        {
            ++mVoteMap[mTarget];                // Add our vote
        }

        bool mayVote ()
        {
            return mCurrent != mTarget;         // If we love the current setting, we will not vote
        }

        void addVote (INT vote)
        {
            ++mVoteMap[vote];
        }

        void noVote ()
        {
            addVote (mCurrent);
        }

        INT getVotes ()
        {
            INT ourVote = mCurrent;
            int weight = 0;

            typedef typename std::map<INT, int>::value_type mapVType;
            BOOST_FOREACH (const mapVType & value, mVoteMap)
            {
                // Take most voted value between current and target, inclusive
                if ((value.first <= std::max (mTarget, mCurrent)) &&
                        (value.first >= std::min (mTarget, mCurrent)) &&
                        (value.second > weight))
                {
                    ourVote = value.first;
                    weight = value.second;
                }
            }

            return ourVote;
        }

    private:
        INT                     mCurrent;       // The current setting
        INT                     mTarget;        // The setting we want
        std::map<INT, int>      mVoteMap;
    };
public:
    FeeVote (uint64 targetBaseFee, uint32 targetReserveBase, uint32 targetReserveIncrement)
        : mTargetBaseFee (targetBaseFee)
        , mTargetReserveBase (targetReserveBase)
        , mTargetReserveIncrement (targetReserveIncrement)
    {
    }

    //--------------------------------------------------------------------------

    void doValidation (Ledger::ref lastClosedLedger, STObject& baseValidation)
    {
        if (lastClosedLedger->getBaseFee () != mTargetBaseFee)
        {
            WriteLog (lsINFO, Features) << "Voting for base fee of " << mTargetBaseFee;
            baseValidation.setFieldU64 (sfBaseFee, mTargetBaseFee);
        }

        if (lastClosedLedger->getReserve (0) != mTargetReserveBase)
        {
            WriteLog (lsINFO, Features) << "Voting for base resrve of " << mTargetReserveBase;
            baseValidation.setFieldU32 (sfReserveBase, mTargetReserveBase);
        }

        if (lastClosedLedger->getReserveInc () != mTargetReserveIncrement)
        {
            WriteLog (lsINFO, Features) << "Voting for reserve increment of " << mTargetReserveIncrement;
            baseValidation.setFieldU32 (sfReserveIncrement, mTargetReserveIncrement);
        }
    }

    //--------------------------------------------------------------------------

    void doVoting (Ledger::ref lastClosedLedger, SHAMap::ref initialPosition)
    {
        // LCL must be flag ledger
        assert ((lastClosedLedger->getLedgerSeq () % 256) == 0);

        VotableInteger<uint64> baseFeeVote (lastClosedLedger->getBaseFee (), mTargetBaseFee);
        VotableInteger<uint32> baseReserveVote (lastClosedLedger->getReserve (0), mTargetReserveBase);
        VotableInteger<uint32> incReserveVote (lastClosedLedger->getReserveInc (), mTargetReserveIncrement);

        // get validations for ledger before flag
        ValidationSet set = getApp().getValidations ().getValidations (lastClosedLedger->getParentHash ());
        BOOST_FOREACH (ValidationSet::value_type const & value, set)
        {
            SerializedValidation const& val = *value.second;

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
        uint64 baseFee = baseFeeVote.getVotes ();
        uint32 baseReserve = baseReserveVote.getVotes ();
        uint32 incReserve = incReserveVote.getVotes ();

        // add transactions to our position
        if ((baseFee != lastClosedLedger->getBaseFee ()) ||
                (baseReserve != lastClosedLedger->getReserve (0)) ||
                (incReserve != lastClosedLedger->getReserveInc ()))
        {
            WriteLog (lsWARNING, Features) << "We are voting for a fee change: " << baseFee << "/" << baseReserve << "/" << incReserve;

            SerializedTransaction trans (ttFEE);
            trans.setFieldAccount (sfAccount, uint160 ());
            trans.setFieldU64 (sfBaseFee, baseFee);
            trans.setFieldU32 (sfReferenceFeeUnits, 10);
            trans.setFieldU32 (sfReserveBase, baseReserve);
            trans.setFieldU32 (sfReserveIncrement, incReserve);

            uint256 txID = trans.getTransactionID ();

            WriteLog (lsWARNING, Features) << "Vote: " << txID;

            Serializer s;
            trans.add (s, true);

            SHAMapItem::pointer tItem = boost::make_shared<SHAMapItem> (txID, s.peekData ());

            if (!initialPosition->addGiveItem (tItem, true, false))
            {
                WriteLog (lsWARNING, Features) << "Ledger already had fee change";
            }
        }
    }

private:
    uint64 mTargetBaseFee;
    uint32 mTargetReserveBase;
    uint32 mTargetReserveIncrement;
};

//------------------------------------------------------------------------------

IFeeVote* IFeeVote::New (uint64 targetBaseFee,
                         uint32 targetReserveBase,
                         uint32 targetReserveIncrement)
{
    return new FeeVote (targetBaseFee, targetReserveBase, targetReserveIncrement);
}

// vim:ts=4
