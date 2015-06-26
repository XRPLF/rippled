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

#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/basics/TestSuite.h>

namespace ripple {
namespace test {

class CanonicalTXSet_test : public TestSuite
{
    void test_txset_key_ordering()
    {
        // Verify that CanonicalTXSet::Key sorts as expected.
        using Key = CanonicalTXSet::Key;

        uint256 const acctRef {2000};
        uint256 const acctLo  {1999};
        uint256 const acctMid {2000};
        uint256 const acctHi  {2001};

        std::uint32_t const seq0         {0};
        std::uint32_t const seqRef {7000000};
        std::uint32_t const seqLo  {6999999};
        std::uint32_t const seqMid {7000000};
        std::uint32_t const seqHi  {7000001};

        AccountID const tktOwner0          {0};
        AccountID const tktOwnerRef {90000000};
        AccountID const tktOwnerLo  {89999999};
        AccountID const tktOwnerMid {90000000};
        AccountID const tktOwnerHi  {90000001};

        std::uint32_t const tktSeq0        {0};
        std::uint32_t const tktSeqRef {300000};
        std::uint32_t const tktSeqLo  {299999};
        std::uint32_t const tktSeqMid {300000};
        std::uint32_t const tktSeqHi  {300001};

        uint256 const txIdRef {50000};
        uint256 const txIdLo  {49999};
        uint256 const txIdMid {50000};
        uint256 const txIdHi  {50001};

        auto eqTest = [this] (Key const& lhs, Key const& rhs)
        {
            this->expect ((lhs <  rhs) == false);
            this->expect ((lhs <= rhs) == true);
            this->expect ((lhs >  rhs) == false);
            this->expect ((lhs >= rhs) == true);
        };

        auto ltTest = [this] (Key const& lhs, Key const& rhs)
        {
            this->expect ((lhs <  rhs) == true);
            this->expect ((lhs <= rhs) == true);
            this->expect ((lhs >  rhs) == false);
            this->expect ((lhs >= rhs) == false);
        };

        auto gtTest = [this] (Key const& lhs, Key const& rhs)
        {
            this->expect ((lhs <  rhs) == false);
            this->expect ((lhs <= rhs) == false);
            this->expect ((lhs >  rhs) == true);
            this->expect ((lhs >= rhs) == true);
        };

        // Test cases with no Tickets
        Key const keyRef {acctRef, seqRef, tktOwner0, tktSeq0, txIdRef};
        {
            Key const keySame {acctMid, seqMid, tktOwner0, tktSeq0, txIdMid};
            eqTest (keySame, keyRef);
        }
        {
            Key const keyLoAccount {acctLo, seqHi, tktOwner0, tktSeq0, txIdHi};
            ltTest (keyLoAccount, keyRef);
        }
        {
            Key const keyHiAccount {acctHi, seqLo, tktOwner0, tktSeq0, txIdLo};
            gtTest (keyHiAccount, keyRef);
        }
        {
            Key const keyLoSeq {acctRef, seqLo, tktOwner0, tktSeq0, txIdHi};
            ltTest (keyLoSeq, keyRef);
        }
        {
            Key const keyHiSeq {acctRef, seqHi, tktOwner0, tktSeq0, txIdLo};
            gtTest (keyHiSeq, keyRef);
        }
        {
            Key const keyLoTxId {acctRef, seqRef, tktOwner0, tktSeq0, txIdLo};
            ltTest (keyLoTxId, keyRef);
        }
        {
            Key const keyHiTxId {acctRef, seqRef, tktOwner0, tktSeq0, txIdHi};
            gtTest (keyHiTxId, keyRef);
        }

        Key const keyRefTicket {acctRef, seq0, tktOwnerRef, tktSeqRef, txIdRef};

        // Test cases with one Ticket.  A Key with a Ticket should always sort
        // greater than a Key without a Ticket.
        {
            ltTest (keyRef, keyRefTicket);
            gtTest (keyRefTicket, keyRef);
        }

        // Test cases with two Tickets
        {
            Key const keySame {acctMid, seq0, tktOwnerMid, tktSeqMid, txIdMid};
            eqTest (keySame, keyRefTicket);
        }
        {
            Key const keyLoOwner {acctMid, seq0, tktOwnerLo, tktSeqHi, txIdHi};
            ltTest (keyLoOwner, keyRefTicket);
        }
        {
            Key const keyHiOwner {acctMid, seq0, tktOwnerHi, tktSeqLo, txIdLo};
            gtTest (keyHiOwner, keyRefTicket);
        }
        {
            Key const keyLoSeq {acctMid, seq0, tktOwnerMid, tktSeqLo, txIdHi};
            ltTest (keyLoSeq, keyRefTicket);
        }
        {
            Key const keyHiSeq {acctMid, seq0, tktOwnerMid, tktSeqHi, txIdLo};
            gtTest (keyHiSeq, keyRefTicket);
        }
    }

public:
    void run()
    {
        test_txset_key_ordering();
    }
};

BEAST_DEFINE_TESTSUITE(CanonicalTXSet, app, ripple);

} // test
} // ripple
