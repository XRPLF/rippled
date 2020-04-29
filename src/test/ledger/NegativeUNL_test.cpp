//-----------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2020 Ripple Labs Inc.

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

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/tx/apply.h>
#include <ripple/basics/Log.h>
#include <ripple/beast/unit_test.h>
#include <ripple/ledger/View.h>
#include <test/jtx.h>

namespace ripple {
namespace test {

bool
nUnlSizeTest(
    jtx::Env& env,
    std::shared_ptr<Ledger> l,
    size_t size,
    bool hasToAdd,
    bool hasToRemove)
{
    bool sameSize = l->nUnl().size() == size;
    if (!sameSize)
    {
        JLOG(env.journal.warn())
            << "nUnl size,"
            << " expect " << size << " actual " << l->nUnl().size();
    }

    bool sameToAdd = (l->nUnlToDisable() != boost::none) == hasToAdd;
    if (!sameToAdd)
    {
        JLOG(env.journal.warn()) << "nUnl has ToAdd,"
                                 << " expect " << hasToAdd << " actual "
                                 << (l->nUnlToDisable() != boost::none);
    }

    bool sameToRemove = (l->nUnlToReEnable() != boost::none) == hasToRemove;
    if (!sameToRemove)
    {
        JLOG(env.journal.warn()) << "nUnl has ToRemove,"
                                 << " expect " << hasToRemove << " actual "
                                 << (l->nUnlToReEnable() != boost::none);
    }

    return sameSize && sameToAdd && sameToRemove;
};

bool
applyAndTestResult(jtx::Env& env, OpenView& view, STTx const& tx, bool pass)
{
    auto res = apply(env.app(), view, tx, ApplyFlags::tapNONE, env.journal);
    if (pass)
        return res.first == tesSUCCESS;
    else
        return res.first == tefFAILURE;
};

bool
VerifyPubKeyAndSeq(
    std::shared_ptr<Ledger> l,
    hash_map<PublicKey, std::uint32_t> nUnlLedgerSeq)
{
    auto sle = l->read(keylet::negativeUNL());
    if (!sle)
        return false;
    if (!sle->isFieldPresent(sfNegativeUNL))
        return false;

    auto const& nUnlData = sle->getFieldArray(sfNegativeUNL);
    if (nUnlData.size() != nUnlLedgerSeq.size())
        return false;

    for (auto const& n : nUnlData)
    {
        if (!n.isFieldPresent(sfFirstLedgerSequence) ||
            !n.isFieldPresent(sfPublicKey))
            return false;

        auto seq = n.getFieldU32(sfFirstLedgerSequence);
        auto d = n.getFieldVL(sfPublicKey);
        auto s = makeSlice(d);
        if (!publicKeyType(s))
            return false;
        PublicKey pk(s);
        auto it = nUnlLedgerSeq.find(pk);
        if (it == nUnlLedgerSeq.end())
            return false;
        if (it->second != seq)
            return false;
        nUnlLedgerSeq.erase(it);
    }
    return nUnlLedgerSeq.size() == 0;
};

class NegativeUNL_test : public beast::unit_test::suite
{
    void
    testNegativeUNL()
    {
        testcase("Create UNLModify Tx and apply to ledgers");
        jtx::Env env(*this, jtx::supported_amendments());

        auto keyPair_1 = randomKeyPair(KeyType::ed25519);
        auto pk1 = keyPair_1.first;
        auto keyPair_2 = randomKeyPair(KeyType::ed25519);
        auto pk2 = keyPair_2.first;
        auto keyPair_3 = randomKeyPair(KeyType::ed25519);
        auto pk3 = keyPair_3.first;

        auto l = std::make_shared<Ledger>(
            create_genesis,
            env.app().config(),
            std::vector<uint256>{},
            env.app().family());

        bool adding;
        PublicKey txKey;
        auto fill = [&](auto& obj) {
            obj.setFieldU8(sfUNLModifyDisabling, adding ? 1 : 0);
            obj.setFieldU32(sfLedgerSequence, l->seq());
            obj.setFieldVL(sfUNLModifyValidator, txKey);
        };

        /*
         * test cases:
         * (0) insert amendment tests in later cases
         * -- with NegativeUNL amendment disabled,
         *    cannot apply UNLModify Tx
         *    cannot update ledger's NegativeUNL section
         * -- with NegativeUNL amendment enabled,
         *    can apply Tx and update ledger
         *
         * (1) the ledger after genesis
         * -- cannot apply Add Tx
         * -- cannot apply Remove Tx
         * -- nUNL empty
         * -- no ToAdd
         * -- no ToRemove
         *
         * (2) a flag ledger
         * -- apply an Add Tx
         * -- cannot apply the second Add Tx
         * -- cannot apply a Remove Tx
         * -- nUNL empty
         * -- has ToAdd with right nodeId
         * -- no ToRemove
         * ++ extra test: first Add Tx in ledger TxSet
         *
         * (3) ledgers before the next flag ledger
         * -- nUNL empty
         * -- has ToAdd with right nodeId
         * -- no ToRemove
         *
         * (4) next flag ledger
         * -- nUNL size == 1, with right nodeId
         * -- no ToAdd
         * -- no ToRemove
         * -- cannot apply an Add Tx with nodeId already in nUNL
         * -- apply an Add Tx with different nodeId
         * -- cannot apply a Remove Tx with the same NodeId as Add
         * -- cannot apply a Remove Tx with a NodeId not in nUNL
         * -- apply a Remove Tx with a nodeId already in nUNL
         * -- has ToAdd with right nodeId
         * -- has ToRemove with right nodeId
         * -- nUNL size still 1, right nodeId
         *
         * (5) ledgers before the next flag ledger
         * -- nUNL size == 1, right nodeId
         * -- has ToAdd with right nodeId
         * -- has ToRemove with right nodeId
         *
         * (6) next flag ledger
         * -- nUNL size == 1, different nodeId
         * -- no ToAdd
         * -- no ToRemove
         * -- apply an Add Tx with different nodeId
         * -- nUNL size still 1, right nodeId
         * -- has ToAdd with right nodeId
         * -- no ToRemove
         *
         * (7) ledgers before the next flag ledger
         * -- nUNL size still 1, right nodeId
         * -- has ToAdd with right nodeId
         * -- no ToRemove
         *
         * (8) next flag ledger
         * -- nUNL size == 2
         * -- apply a Remove Tx
         * -- cannot apply second Remove Tx, even with right nodeId
         * -- cannot apply an Add Tx with the same NodeId as Remove
         * -- nUNL size == 2
         * -- no ToAdd
         * -- has ToRemove with right nodeId
         *
         * (9) ledgers before the next flag ledger
         * -- nUNL size == 2
         * -- no ToAdd
         * -- has ToRemove with right nodeId
         *
         * (10) next flag ledger
         * -- nUNL size == 1
         * -- apply a Remove Tx
         * -- nUNL size == 1
         * -- no ToAdd
         * -- has ToRemove with right nodeId
         *
         * (11) ledgers before the next flag ledger
         * -- nUNL size == 1
         * -- no ToAdd
         * -- has ToRemove with right nodeId
         *
         * (12) next flag ledger
         * -- nUNL size == 0
         * -- no ToAdd
         * -- no ToRemove
         *
         * (13) ledgers before the next flag ledger
         * -- nUNL size == 0
         * -- no ToAdd
         * -- no ToRemove
         *
         * (14) next flag ledger
         * -- nUNL size == 0
         * -- no ToAdd
         * -- no ToRemove
         */

        hash_map<PublicKey, std::uint32_t> nUnlLedgerSeq;

        {
            //(1) the ledger after genesis, not a flag ledger
            l = std::make_shared<Ledger>(
                *l, env.app().timeKeeper().closeTime());
            adding = true;
            txKey = pk1;
            STTx txAdd(ttUNL_MODIFY, fill);
            adding = false;
            txKey = pk2;
            STTx txRemove_2(ttUNL_MODIFY, fill);

            OpenView accum(&*l);
            BEAST_EXPECT(applyAndTestResult(env, accum, txAdd, false));
            BEAST_EXPECT(applyAndTestResult(env, accum, txRemove_2, false));
            accum.apply(*l);
            BEAST_EXPECT(nUnlSizeTest(env, l, 0, false, false));
        }

        {
            //(2) a flag ledger
            // more ledgers
            for (auto i = 0; i < 256 - 2; ++i)
            {
                auto next = std::make_shared<Ledger>(
                    *l, env.app().timeKeeper().closeTime());
                l = next;
            }
            // flag ledger now
            adding = true;
            txKey = pk1;
            STTx txAdd(ttUNL_MODIFY, fill);
            txKey = pk2;
            STTx txAdd_2(ttUNL_MODIFY, fill);
            adding = false;
            txKey = pk3;
            STTx txRemove_3(ttUNL_MODIFY, fill);

            OpenView accum(&*l);
            BEAST_EXPECT(applyAndTestResult(env, accum, txAdd, true));
            BEAST_EXPECT(applyAndTestResult(env, accum, txAdd_2, false));
            BEAST_EXPECT(applyAndTestResult(env, accum, txRemove_3, false));
            accum.apply(*l);
            auto good_size = nUnlSizeTest(env, l, 0, true, false);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(l->nUnlToDisable() == pk1);
                //++ first Add Tx in ledger TxSet
                uint256 txID = txAdd.getTransactionID();
                BEAST_EXPECT(l->txExists(txID));
            }
        }

        {
            //(3) ledgers before the next flag ledger
            for (auto i = 0; i < 256; ++i)
            {
                auto good_size = nUnlSizeTest(env, l, 0, true, false);
                BEAST_EXPECT(good_size);
                if (good_size)
                    BEAST_EXPECT(l->nUnlToDisable() == pk1);
                auto next = std::make_shared<Ledger>(
                    *l, env.app().timeKeeper().closeTime());
                l = next;
            }

            //(4) next flag ledger
            adding = true;
            txKey = pk1;
            STTx txAdd(ttUNL_MODIFY, fill);
            txKey = pk2;
            STTx txAdd_2(ttUNL_MODIFY, fill);
            adding = false;
            txKey = pk1;
            STTx txRemove(ttUNL_MODIFY, fill);
            txKey = pk2;
            STTx txRemove_2(ttUNL_MODIFY, fill);
            txKey = pk3;
            STTx txRemove_3(ttUNL_MODIFY, fill);
            auto good_size = nUnlSizeTest(env, l, 1, false, false);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(*(l->nUnl().begin()) == pk1);
                nUnlLedgerSeq.emplace(pk1, l->seq());
            }
            OpenView accum(&*l);
            BEAST_EXPECT(applyAndTestResult(env, accum, txAdd, false));
            BEAST_EXPECT(applyAndTestResult(env, accum, txAdd_2, true));
            BEAST_EXPECT(applyAndTestResult(env, accum, txRemove_2, false));
            BEAST_EXPECT(applyAndTestResult(env, accum, txRemove_3, false));
            BEAST_EXPECT(applyAndTestResult(env, accum, txRemove, true));
            accum.apply(*l);
            good_size = nUnlSizeTest(env, l, 1, true, true);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(l->nUnl().find(pk1) != l->nUnl().end());
                BEAST_EXPECT(l->nUnlToDisable() == pk2);
                BEAST_EXPECT(l->nUnlToReEnable() == pk1);

                // test sfFirstLedgerSequence
                BEAST_EXPECT(VerifyPubKeyAndSeq(l, nUnlLedgerSeq));
            }
        }

        {
            //(5) ledgers before the next flag ledger
            for (auto i = 0; i < 256; ++i)
            {
                auto good_size = nUnlSizeTest(env, l, 1, true, true);
                BEAST_EXPECT(good_size);
                if (good_size)
                {
                    BEAST_EXPECT(l->nUnl().find(pk1) != l->nUnl().end());
                    BEAST_EXPECT(l->nUnlToDisable() == pk2);
                    BEAST_EXPECT(l->nUnlToReEnable() == pk1);
                }
                auto next = std::make_shared<Ledger>(
                    *l, env.app().timeKeeper().closeTime());
                l = next;
            }

            //(6) next flag ledger
            adding = true;
            txKey = pk1;
            STTx txAdd(ttUNL_MODIFY, fill);
            auto good_size = nUnlSizeTest(env, l, 1, false, false);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(l->nUnl().find(pk2) != l->nUnl().end());
            }
            OpenView accum(&*l);
            BEAST_EXPECT(applyAndTestResult(env, accum, txAdd, true));
            accum.apply(*l);
            good_size = nUnlSizeTest(env, l, 1, true, false);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(l->nUnl().find(pk2) != l->nUnl().end());
                BEAST_EXPECT(l->nUnlToDisable() == pk1);
                nUnlLedgerSeq.emplace(pk2, l->seq());
                nUnlLedgerSeq.erase(pk1);
                BEAST_EXPECT(VerifyPubKeyAndSeq(l, nUnlLedgerSeq));
            }
        }

        {
            //(7) ledgers before the next flag ledger
            for (auto i = 0; i < 256; ++i)
            {
                auto good_size = nUnlSizeTest(env, l, 1, true, false);
                BEAST_EXPECT(good_size);
                if (good_size)
                {
                    BEAST_EXPECT(l->nUnl().find(pk2) != l->nUnl().end());
                    BEAST_EXPECT(l->nUnlToDisable() == pk1);
                }
                auto next = std::make_shared<Ledger>(
                    *l, env.app().timeKeeper().closeTime());
                l = next;
            }

            //(8) next flag ledger
            adding = true;
            txKey = pk1;
            STTx txAdd(ttUNL_MODIFY, fill);
            adding = false;
            txKey = pk1;
            STTx txRemove(ttUNL_MODIFY, fill);
            txKey = pk2;
            STTx txRemove_2(ttUNL_MODIFY, fill);

            auto good_size = nUnlSizeTest(env, l, 2, false, false);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(l->nUnl().find(pk1) != l->nUnl().end());
                BEAST_EXPECT(l->nUnl().find(pk2) != l->nUnl().end());
                nUnlLedgerSeq.emplace(pk1, l->seq());
                BEAST_EXPECT(VerifyPubKeyAndSeq(l, nUnlLedgerSeq));
            }
            OpenView accum(&*l);
            BEAST_EXPECT(applyAndTestResult(env, accum, txRemove, true));
            BEAST_EXPECT(applyAndTestResult(env, accum, txRemove_2, false));
            BEAST_EXPECT(applyAndTestResult(env, accum, txAdd, false));
            accum.apply(*l);
            good_size = nUnlSizeTest(env, l, 2, false, true);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(l->nUnl().find(pk1) != l->nUnl().end());
                BEAST_EXPECT(l->nUnl().find(pk2) != l->nUnl().end());
                BEAST_EXPECT(l->nUnlToReEnable() == pk1);
                BEAST_EXPECT(VerifyPubKeyAndSeq(l, nUnlLedgerSeq));
            }
        }

        {
            //(9) ledgers before the next flag ledger
            for (auto i = 0; i < 256; ++i)
            {
                auto good_size = nUnlSizeTest(env, l, 2, false, true);
                BEAST_EXPECT(good_size);
                if (good_size)
                {
                    BEAST_EXPECT(l->nUnl().find(pk1) != l->nUnl().end());
                    BEAST_EXPECT(l->nUnl().find(pk2) != l->nUnl().end());
                    BEAST_EXPECT(l->nUnlToReEnable() == pk1);
                }
                auto next = std::make_shared<Ledger>(
                    *l, env.app().timeKeeper().closeTime());
                l = next;
            }

            //(10) next flag ledger
            adding = false;
            txKey = pk2;
            STTx txRemove_2(ttUNL_MODIFY, fill);
            auto good_size = nUnlSizeTest(env, l, 1, false, false);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(l->nUnl().find(pk2) != l->nUnl().end());
                nUnlLedgerSeq.erase(pk1);
                BEAST_EXPECT(VerifyPubKeyAndSeq(l, nUnlLedgerSeq));
            }
            OpenView accum(&*l);
            BEAST_EXPECT(applyAndTestResult(env, accum, txRemove_2, true));
            accum.apply(*l);
            good_size = nUnlSizeTest(env, l, 1, false, true);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(l->nUnl().find(pk2) != l->nUnl().end());
                BEAST_EXPECT(l->nUnlToReEnable() == pk2);
                BEAST_EXPECT(VerifyPubKeyAndSeq(l, nUnlLedgerSeq));
            }
        }

        {
            //(11) ledgers before the next flag ledger
            for (auto i = 0; i < 256; ++i)
            {
                auto good_size = nUnlSizeTest(env, l, 1, false, true);
                BEAST_EXPECT(good_size);
                if (good_size)
                {
                    BEAST_EXPECT(l->nUnl().find(pk2) != l->nUnl().end());
                    BEAST_EXPECT(l->nUnlToReEnable() == pk2);
                }
                auto next = std::make_shared<Ledger>(
                    *l, env.app().timeKeeper().closeTime());
                l = next;
            }

            //(12) next flag ledger
            auto good_size = nUnlSizeTest(env, l, 0, false, false);
            BEAST_EXPECT(good_size);
        }

        {
            //(13) ledgers before the next flag ledger
            for (auto i = 0; i < 256; ++i)
            {
                auto good_size = nUnlSizeTest(env, l, 0, false, false);
                BEAST_EXPECT(good_size);
                auto next = std::make_shared<Ledger>(
                    *l, env.app().timeKeeper().closeTime());
                l = next;
            }

            //(14) next flag ledger
            auto good_size = nUnlSizeTest(env, l, 0, false, false);
            BEAST_EXPECT(good_size);
        }
    }

    void
    run() override
    {
        testNegativeUNL();
    }
};

BEAST_DEFINE_TESTSUITE(NegativeUNL, ledger, ripple);

}  // namespace test
}  // namespace ripple
