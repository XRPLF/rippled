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

#ifndef RIPPLE_LEDGERHISTORY_H
#define RIPPLE_LEDGERHISTORY_H

// VFALCO TODO Rename to OldLedgers ?
class LedgerHistory : LeakChecked <LedgerHistory>
{
public:
    LedgerHistory ();

    void addLedger (Ledger::pointer ledger, bool validated);

    float getCacheHitRate ()
    {
        return m_ledgers_by_hash.getHitRate ();
    }

    Ledger::pointer getLedgerBySeq (LedgerIndex ledgerIndex);

    LedgerHash getLedgerHash (LedgerIndex ledgerIndex);

    Ledger::pointer getLedgerByHash (LedgerHash const& ledgerHash);

    void tune (int size, int age);

    void sweep ()
    {
        m_ledgers_by_hash.sweep ();
        m_consensus_validated.sweep ();
    }

    void builtLedger (Ledger::ref);
    void validatedLedger (Ledger::ref);

    bool fixIndex(LedgerIndex ledgerIndex, LedgerHash const& ledgerHash);

private:
    typedef TaggedCache <LedgerHash, Ledger> LedgersByHash;

    LedgersByHash m_ledgers_by_hash;

    //typedef std::pair <LedgerHash, LedgerHash>
    typedef TaggedCache <LedgerIndex,
        std::pair< LedgerHash, LedgerHash >> ConsensusValidated;
    ConsensusValidated m_consensus_validated;


    // Maps ledger indexes to the corresponding hash.
    std::map <LedgerIndex, LedgerHash> mLedgersByIndex; // validated ledgers
};

#endif
