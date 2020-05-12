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

#include <ripple/app/consensus/RCLValidations.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/misc/NegativeUNLVote.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/app/tx/apply.h>
#include <ripple/basics/Log.h>
#include <ripple/beast/unit_test.h>
#include <ripple/ledger/View.h>
#include <ripple/rpc/impl/GRPCHelpers.h>
#include <test/jtx.h>

namespace ripple {
namespace test {

/*
 * This file implements the following negative UNL related tests:
 * -- test filling and applying ttUNL_MODIFY Tx and ledger update
 * -- test ttUNL_MODIFY Tx failure without featureNegativeUNL amendment
 * -- test the NegativeUNLVote class. The test cases are split to multiple
 *    test classes to allow parallel execution.
 * -- test the negativeUNLFilter function
 *
 * Other negative UNL related tests such as ValidatorList and RPC related ones
 * are put in their existing unit test files.
 */

/**
 * Test the size of the negative UNL in a ledger,
 * also test if the ledger has ToDisalbe and/or ToReEnable
 *
 * @param l the ledger
 * @param size the expected negative UNL size
 * @param hasToDisable if expect ToDisable in ledger
 * @param hasToReEnable if expect ToDisable in ledger
 * @return true if meet all three expectation
 */
bool
negUnlSizeTest(
    std::shared_ptr<Ledger const> const& l,
    size_t size,
    bool hasToDisable,
    bool hasToReEnable);

/**
 * Try to apply a ttUNL_MODIFY Tx, and test the apply result
 *
 * @param env the test environment
 * @param view the OpenView of the ledger
 * @param tx the ttUNL_MODIFY Tx
 * @param pass if the Tx should be applied successfully
 * @return true if meet the expectation of apply result
 */
bool
applyAndTestResult(jtx::Env& env, OpenView& view, STTx const& tx, bool pass);

/**
 * Verify the content of negative UNL entries (public key and ledger sequence)
 * of a ledger
 *
 * @param l the ledger
 * @param nUnlLedgerSeq the expected PublicKeys and ledger Sequences
 * @note nUnlLedgerSeq is copied so that it can be modified.
 * @return true if meet the expectation
 */
bool
VerifyPubKeyAndSeq(
    std::shared_ptr<Ledger const> const& l,
    hash_map<PublicKey, std::uint32_t> nUnlLedgerSeq);

/**
 * Count the number of Tx in a TxSet
 *
 * @param txSet the TxSet
 * @return the number of Tx
 */
std::size_t
countTx(std::shared_ptr<SHAMap> const& txSet);

/**
 * Create fake public keys
 *
 * @param n the number of public keys
 * @return a vector of public keys created
 */
std::vector<PublicKey>
createPublicKeys(std::size_t n);

/**
 * Create ttUNL_MODIFY Tx
 *
 * @param disabling disabling or re-enabling a validator
 * @param seq current ledger seq
 * @param txKey the public key of the validator
 * @return the ttUNL_MODIFY Tx
 */
STTx
createTx(bool disabling, LedgerIndex seq, PublicKey const& txKey);

class NegativeUNL_test : public beast::unit_test::suite
{
    /**
     * Test filling and applying ttUNL_MODIFY Tx, as well as ledger update:
     *
     * We will build a long history of ledgers, and try to apply different
     * ttUNL_MODIFY Txes. We will check if the apply results meet expectations
     * and if the ledgers are updated correctly.
     */
    void
    testNegativeUNL()
    {
        /*
         * test cases:
         *
         * (1) the ledger after genesis
         * -- cannot apply Disable Tx
         * -- cannot apply ReEnable Tx
         * -- nUNL empty
         * -- no ToDisable
         * -- no ToReEnable
         *
         * (2) a flag ledger
         * -- apply an Disable Tx
         * -- cannot apply the second Disable Tx
         * -- cannot apply a ReEnable Tx
         * -- nUNL empty
         * -- has ToDisable with right nodeId
         * -- no ToReEnable
         * ++ extra test: first Disable Tx in ledger TxSet
         *
         * (3) ledgers before the next flag ledger
         * -- nUNL empty
         * -- has ToDisable with right nodeId
         * -- no ToReEnable
         *
         * (4) next flag ledger
         * -- nUNL size == 1, with right nodeId
         * -- no ToDisable
         * -- no ToReEnable
         * -- cannot apply an Disable Tx with nodeId already in nUNL
         * -- apply an Disable Tx with different nodeId
         * -- cannot apply a ReEnable Tx with the same NodeId as Add
         * -- cannot apply a ReEnable Tx with a NodeId not in nUNL
         * -- apply a ReEnable Tx with a nodeId already in nUNL
         * -- has ToDisable with right nodeId
         * -- has ToReEnable with right nodeId
         * -- nUNL size still 1, right nodeId
         *
         * (5) ledgers before the next flag ledger
         * -- nUNL size == 1, right nodeId
         * -- has ToDisable with right nodeId
         * -- has ToReEnable with right nodeId
         *
         * (6) next flag ledger
         * -- nUNL size == 1, different nodeId
         * -- no ToDisable
         * -- no ToReEnable
         * -- apply an Disable Tx with different nodeId
         * -- nUNL size still 1, right nodeId
         * -- has ToDisable with right nodeId
         * -- no ToReEnable
         *
         * (7) ledgers before the next flag ledger
         * -- nUNL size still 1, right nodeId
         * -- has ToDisable with right nodeId
         * -- no ToReEnable
         *
         * (8) next flag ledger
         * -- nUNL size == 2
         * -- apply a ReEnable Tx
         * -- cannot apply second ReEnable Tx, even with right nodeId
         * -- cannot apply an Disable Tx with the same NodeId as Remove
         * -- nUNL size == 2
         * -- no ToDisable
         * -- has ToReEnable with right nodeId
         *
         * (9) ledgers before the next flag ledger
         * -- nUNL size == 2
         * -- no ToDisable
         * -- has ToReEnable with right nodeId
         *
         * (10) next flag ledger
         * -- nUNL size == 1
         * -- apply a ReEnable Tx
         * -- nUNL size == 1
         * -- no ToDisable
         * -- has ToReEnable with right nodeId
         *
         * (11) ledgers before the next flag ledger
         * -- nUNL size == 1
         * -- no ToDisable
         * -- has ToReEnable with right nodeId
         *
         * (12) next flag ledger
         * -- nUNL size == 0
         * -- no ToDisable
         * -- no ToReEnable
         *
         * (13) ledgers before the next flag ledger
         * -- nUNL size == 0
         * -- no ToDisable
         * -- no ToReEnable
         *
         * (14) next flag ledger
         * -- nUNL size == 0
         * -- no ToDisable
         * -- no ToReEnable
         */

        testcase("Create UNLModify Tx and apply to ledgers");

        jtx::Env env(*this, jtx::supported_amendments());
        std::vector<PublicKey> publicKeys = createPublicKeys(3);
        // genesis ledger
        auto l = std::make_shared<Ledger>(
            create_genesis,
            env.app().config(),
            std::vector<uint256>{},
            env.app().family());
        BEAST_EXPECT(l->rules().enabled(featureNegativeUNL));

        // Record the public keys and ledger sequences of expected negative UNL
        // validators when we build the ledger history
        hash_map<PublicKey, std::uint32_t> nUnlLedgerSeq;

        {
            //(1) the ledger after genesis, not a flag ledger
            l = std::make_shared<Ledger>(
                *l, env.app().timeKeeper().closeTime());

            auto txDisable_0 = createTx(true, l->seq(), publicKeys[0]);
            auto txReEnable_1 = createTx(false, l->seq(), publicKeys[1]);

            OpenView accum(&*l);
            BEAST_EXPECT(applyAndTestResult(env, accum, txDisable_0, false));
            BEAST_EXPECT(applyAndTestResult(env, accum, txReEnable_1, false));
            accum.apply(*l);
            BEAST_EXPECT(negUnlSizeTest(l, 0, false, false));
        }

        {
            //(2) a flag ledger
            // generate more ledgers
            for (auto i = 0; i < 256 - 2; ++i)
            {
                l = std::make_shared<Ledger>(
                    *l, env.app().timeKeeper().closeTime());
            }

            auto txDisable_0 = createTx(true, l->seq(), publicKeys[0]);
            auto txDisable_1 = createTx(true, l->seq(), publicKeys[1]);
            auto txReEnable_2 = createTx(false, l->seq(), publicKeys[2]);

            // can apply 1 and only 1 ToDisable Tx,
            // cannot apply ToReEnable Tx, since negative UNL is empty
            OpenView accum(&*l);
            BEAST_EXPECT(applyAndTestResult(env, accum, txDisable_0, true));
            BEAST_EXPECT(applyAndTestResult(env, accum, txDisable_1, false));
            BEAST_EXPECT(applyAndTestResult(env, accum, txReEnable_2, false));
            accum.apply(*l);
            auto good_size = negUnlSizeTest(l, 0, true, false);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(l->negativeUnlToDisable() == publicKeys[0]);
                //++ first ToDisable Tx in ledger's TxSet
                uint256 txID = txDisable_0.getTransactionID();
                BEAST_EXPECT(l->txExists(txID));
            }
        }

        {
            //(3) ledgers before the next flag ledger
            for (auto i = 0; i < 256; ++i)
            {
                auto good_size = negUnlSizeTest(l, 0, true, false);
                BEAST_EXPECT(good_size);
                if (good_size)
                    BEAST_EXPECT(l->negativeUnlToDisable() == publicKeys[0]);
                l = std::make_shared<Ledger>(
                    *l, env.app().timeKeeper().closeTime());
            }

            //(4) next flag ledger
            // test if the ledger updated correctly
            auto good_size = negUnlSizeTest(l, 1, false, false);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(*(l->negativeUnl().begin()) == publicKeys[0]);
                nUnlLedgerSeq.emplace(publicKeys[0], l->seq());
            }

            auto txDisable_0 = createTx(true, l->seq(), publicKeys[0]);
            auto txDisable_1 = createTx(true, l->seq(), publicKeys[1]);
            auto txReEnable_0 = createTx(false, l->seq(), publicKeys[0]);
            auto txReEnable_1 = createTx(false, l->seq(), publicKeys[1]);
            auto txReEnable_2 = createTx(false, l->seq(), publicKeys[2]);

            OpenView accum(&*l);
            BEAST_EXPECT(applyAndTestResult(env, accum, txDisable_0, false));
            BEAST_EXPECT(applyAndTestResult(env, accum, txDisable_1, true));
            BEAST_EXPECT(applyAndTestResult(env, accum, txReEnable_1, false));
            BEAST_EXPECT(applyAndTestResult(env, accum, txReEnable_2, false));
            BEAST_EXPECT(applyAndTestResult(env, accum, txReEnable_0, true));
            accum.apply(*l);
            good_size = negUnlSizeTest(l, 1, true, true);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(l->negativeUnl().count(publicKeys[0]));
                BEAST_EXPECT(l->negativeUnlToDisable() == publicKeys[1]);
                BEAST_EXPECT(l->negativeUnlToReEnable() == publicKeys[0]);
                // test sfFirstLedgerSequence
                BEAST_EXPECT(VerifyPubKeyAndSeq(l, nUnlLedgerSeq));
            }
        }

        {
            //(5) ledgers before the next flag ledger
            for (auto i = 0; i < 256; ++i)
            {
                auto good_size = negUnlSizeTest(l, 1, true, true);
                BEAST_EXPECT(good_size);
                if (good_size)
                {
                    BEAST_EXPECT(l->negativeUnl().count(publicKeys[0]));
                    BEAST_EXPECT(l->negativeUnlToDisable() == publicKeys[1]);
                    BEAST_EXPECT(l->negativeUnlToReEnable() == publicKeys[0]);
                }
                l = std::make_shared<Ledger>(
                    *l, env.app().timeKeeper().closeTime());
            }

            //(6) next flag ledger
            // test if the ledger updated correctly
            auto good_size = negUnlSizeTest(l, 1, false, false);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(l->negativeUnl().count(publicKeys[1]));
            }

            auto txDisable_0 = createTx(true, l->seq(), publicKeys[0]);

            OpenView accum(&*l);
            BEAST_EXPECT(applyAndTestResult(env, accum, txDisable_0, true));
            accum.apply(*l);
            good_size = negUnlSizeTest(l, 1, true, false);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(l->negativeUnl().count(publicKeys[1]));
                BEAST_EXPECT(l->negativeUnlToDisable() == publicKeys[0]);
                nUnlLedgerSeq.emplace(publicKeys[1], l->seq());
                nUnlLedgerSeq.erase(publicKeys[0]);
                BEAST_EXPECT(VerifyPubKeyAndSeq(l, nUnlLedgerSeq));
            }
        }

        {
            //(7) ledgers before the next flag ledger
            for (auto i = 0; i < 256; ++i)
            {
                auto good_size = negUnlSizeTest(l, 1, true, false);
                BEAST_EXPECT(good_size);
                if (good_size)
                {
                    BEAST_EXPECT(l->negativeUnl().count(publicKeys[1]));
                    BEAST_EXPECT(l->negativeUnlToDisable() == publicKeys[0]);
                }
                l = std::make_shared<Ledger>(
                    *l, env.app().timeKeeper().closeTime());
            }

            //(8) next flag ledger
            // test if the ledger updated correctly
            auto good_size = negUnlSizeTest(l, 2, false, false);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(l->negativeUnl().count(publicKeys[0]));
                BEAST_EXPECT(l->negativeUnl().count(publicKeys[1]));
                nUnlLedgerSeq.emplace(publicKeys[0], l->seq());
                BEAST_EXPECT(VerifyPubKeyAndSeq(l, nUnlLedgerSeq));
            }

            auto txDisable_0 = createTx(true, l->seq(), publicKeys[0]);
            auto txReEnable_0 = createTx(false, l->seq(), publicKeys[0]);
            auto txReEnable_1 = createTx(false, l->seq(), publicKeys[1]);

            OpenView accum(&*l);
            BEAST_EXPECT(applyAndTestResult(env, accum, txReEnable_0, true));
            BEAST_EXPECT(applyAndTestResult(env, accum, txReEnable_1, false));
            BEAST_EXPECT(applyAndTestResult(env, accum, txDisable_0, false));
            accum.apply(*l);
            good_size = negUnlSizeTest(l, 2, false, true);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(l->negativeUnl().count(publicKeys[0]));
                BEAST_EXPECT(l->negativeUnl().count(publicKeys[1]));
                BEAST_EXPECT(l->negativeUnlToReEnable() == publicKeys[0]);
                BEAST_EXPECT(VerifyPubKeyAndSeq(l, nUnlLedgerSeq));
            }
        }

        {
            //(9) ledgers before the next flag ledger
            for (auto i = 0; i < 256; ++i)
            {
                auto good_size = negUnlSizeTest(l, 2, false, true);
                BEAST_EXPECT(good_size);
                if (good_size)
                {
                    BEAST_EXPECT(l->negativeUnl().count(publicKeys[0]));
                    BEAST_EXPECT(l->negativeUnl().count(publicKeys[1]));
                    BEAST_EXPECT(l->negativeUnlToReEnable() == publicKeys[0]);
                }
                l = std::make_shared<Ledger>(
                    *l, env.app().timeKeeper().closeTime());
            }

            //(10) next flag ledger
            // test if the ledger updated correctly
            auto good_size = negUnlSizeTest(l, 1, false, false);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(l->negativeUnl().count(publicKeys[1]));
                nUnlLedgerSeq.erase(publicKeys[0]);
                BEAST_EXPECT(VerifyPubKeyAndSeq(l, nUnlLedgerSeq));
            }

            auto txReEnable_1 = createTx(false, l->seq(), publicKeys[1]);

            OpenView accum(&*l);
            BEAST_EXPECT(applyAndTestResult(env, accum, txReEnable_1, true));
            accum.apply(*l);
            good_size = negUnlSizeTest(l, 1, false, true);
            BEAST_EXPECT(good_size);
            if (good_size)
            {
                BEAST_EXPECT(l->negativeUnl().count(publicKeys[1]));
                BEAST_EXPECT(l->negativeUnlToReEnable() == publicKeys[1]);
                BEAST_EXPECT(VerifyPubKeyAndSeq(l, nUnlLedgerSeq));
            }
        }

        {
            //(11) ledgers before the next flag ledger
            for (auto i = 0; i < 256; ++i)
            {
                auto good_size = negUnlSizeTest(l, 1, false, true);
                BEAST_EXPECT(good_size);
                if (good_size)
                {
                    BEAST_EXPECT(l->negativeUnl().count(publicKeys[1]));
                    BEAST_EXPECT(l->negativeUnlToReEnable() == publicKeys[1]);
                }
                l = std::make_shared<Ledger>(
                    *l, env.app().timeKeeper().closeTime());
            }

            //(12) next flag ledger
            BEAST_EXPECT(negUnlSizeTest(l, 0, false, false));
        }

        {
            //(13) ledgers before the next flag ledger
            for (auto i = 0; i < 256; ++i)
            {
                BEAST_EXPECT(negUnlSizeTest(l, 0, false, false));
                l = std::make_shared<Ledger>(
                    *l, env.app().timeKeeper().closeTime());
            }

            //(14) next flag ledger
            BEAST_EXPECT(negUnlSizeTest(l, 0, false, false));
        }
    }

    void
    run() override
    {
        testNegativeUNL();
    }
};

class NegativeUNLNoAmendment_test : public beast::unit_test::suite
{
    void
    testNegativeUNLNoAmendment()
    {
        testcase("No negative UNL amendment");

        jtx::Env env(*this, jtx::supported_amendments() - featureNegativeUNL);
        std::vector<PublicKey> publicKeys = createPublicKeys(1);
        // genesis ledger
        auto l = std::make_shared<Ledger>(
            create_genesis,
            env.app().config(),
            std::vector<uint256>{},
            env.app().family());
        BEAST_EXPECT(!l->rules().enabled(featureNegativeUNL));

        // generate more ledgers
        for (auto i = 0; i < 256 - 1; ++i)
        {
            l = std::make_shared<Ledger>(
                *l, env.app().timeKeeper().closeTime());
        }
        BEAST_EXPECT(l->seq() == 256);
        auto txDisable_0 = createTx(true, l->seq(), publicKeys[0]);
        OpenView accum(&*l);
        BEAST_EXPECT(applyAndTestResult(env, accum, txDisable_0, false));
        accum.apply(*l);
        BEAST_EXPECT(negUnlSizeTest(l, 0, false, false));
    }

    void
    run() override
    {
        testNegativeUNLNoAmendment();
    }
};

/**
 * Utility class for creating validators and ledger history
 */
struct NetworkHistory
{
    using LedgerHistory = std::vector<std::shared_ptr<Ledger>>;
    /**
     *
     * Only reasonable parameters can be honored,
     * e.g cannot hasToReEnable when nUNLSize == 0
     */
    struct Parameter
    {
        std::uint32_t numNodes;    // number of validators
        std::uint32_t negUNLSize;  // size of negative UNL in the last ledger
        bool hasToDisable;         // if has ToDisable in the last ledger
        bool hasToReEnable;        // if has ToReEnable in the last ledger
        /**
         * if not specified, the number of ledgers in the history is calculated
         * from negUNLSize, hasToDisable, and hasToReEnable
         */
        std::optional<int> numLedgers;
    };

    NetworkHistory(beast::unit_test::suite& suite, Parameter const& p)
        : env(suite, jtx::supported_amendments())
        , param(p)
        , validations(env.app().getValidations())
    {
        createNodes();
        if (!param.numLedgers)
            param.numLedgers = 256 * (param.negUNLSize + 1);
        goodHistory = createLedgerHistory();
    }

    void
    createNodes()
    {
        assert(param.numNodes <= 256);
        UNLKeys = createPublicKeys(param.numNodes);
        for (int i = 0; i < param.numNodes; ++i)
        {
            UNLKeySet.insert(UNLKeys[i]);
            UNLNodeIDs.push_back(calcNodeID(UNLKeys[i]));
            UNLNodeIDSet.insert(UNLNodeIDs.back());
        }
    }

    /**
     * create ledger history and apply needed ttUNL_MODIFY tx at flag ledgers
     * @return
     */
    bool
    createLedgerHistory()
    {
        static uint256 fake_amemdment;  // So we have different genesis ledgers
        auto l = std::make_shared<Ledger>(
            create_genesis,
            env.app().config(),
            std::vector<uint256>{fake_amemdment++},
            env.app().family());
        history.push_back(l);

        // When putting validators into the negative UNL, we start with
        // validator 0, then validator 1 ...
        int nidx = 0;
        while (l->seq() <= param.numLedgers)
        {
            l = std::make_shared<Ledger>(
                *l, env.app().timeKeeper().closeTime());
            history.push_back(l);

            if (isFlagLedger(l->seq()))
            {
                OpenView accum(&*l);
                if (l->negativeUnl().size() < param.negUNLSize)
                {
                    auto tx = createTx(true, l->seq(), UNLKeys[nidx]);
                    if (!applyAndTestResult(env, accum, tx, true))
                        break;
                    ++nidx;
                }
                else if (l->negativeUnl().size() == param.negUNLSize)
                {
                    if (param.hasToDisable)
                    {
                        auto tx = createTx(true, l->seq(), UNLKeys[nidx]);
                        if (!applyAndTestResult(env, accum, tx, true))
                            break;
                        ++nidx;
                    }
                    if (param.hasToReEnable)
                    {
                        auto tx = createTx(false, l->seq(), UNLKeys[0]);
                        if (!applyAndTestResult(env, accum, tx, true))
                            break;
                    }
                }
                accum.apply(*l);
            }
            l->updateSkipList();
        }
        return negUnlSizeTest(
            l, param.negUNLSize, param.hasToDisable, param.hasToReEnable);
    }

    /**
     * Create a validation
     * @param ledger the ledger the validation validates
     * @param v the validator
     * @return the validation
     */
    std::shared_ptr<STValidation>
    createSTVal(std::shared_ptr<Ledger const> const& ledger, NodeID const& v)
    {
        static auto keyPair = randomKeyPair(KeyType::secp256k1);
        return std::make_shared<STValidation>(
            env.app().timeKeeper().now(),
            keyPair.first,
            keyPair.second,
            v,
            [&](STValidation& v) {
                v.setFieldH256(sfLedgerHash, ledger->info().hash);
                v.setFieldU32(sfLedgerSequence, ledger->seq());
                v.setFlag(vfFullValidation);
            });
    };

    /**
     * Walk the ledger history and create validation messages for the ledgers
     *
     * @tparam NeedValidation a function to decided if a validation is needed
     * @param needVal if a validation is needed for this particular combination
     *        of ledger and validator
     */
    template <class NeedValidation>
    void
    walkHistoryAndAddValidations(NeedValidation&& needVal)
    {
        std::uint32_t curr = 0;
        std::size_t need = 256 + 1;
        // only last 256 + 1 ledgers need validations
        if (history.size() > need)
            curr = history.size() - need;
        for (; curr != history.size(); ++curr)
        {
            for (std::size_t i = 0; i < param.numNodes; ++i)
            {
                if (needVal(history[curr], i))
                {
                    RCLValidation v(createSTVal(history[curr], UNLNodeIDs[i]));
                    v.setTrusted();
                    validations.add(UNLNodeIDs[i], v);
                }
            }
        }
    }

    std::shared_ptr<Ledger const>
    lastLedger() const
    {
        return history.back();
    }

    jtx::Env env;
    Parameter param;
    RCLValidations& validations;
    std::vector<PublicKey> UNLKeys;
    hash_set<PublicKey> UNLKeySet;
    std::vector<NodeID> UNLNodeIDs;
    hash_set<NodeID> UNLNodeIDSet;
    LedgerHistory history;
    bool goodHistory;
};

auto defaultPreVote = [](NegativeUNLVote& vote) {};
/**
 * Create a NegativeUNLVote object. It then creates ttUNL_MODIFY Tx as its vote
 * on negative UNL changes.
 *
 * @tparam PreVote a function to be called before vote
 * @param history the ledger history
 * @param myId the voting validator
 * @param expect the number of ttUNL_MODIFY Tx expected
 * @param pre the PreVote function
 * @return true if the number of ttUNL_MODIFY Txes created meet expectation
 */
template <typename PreVote = decltype(defaultPreVote)>
bool
voteAndCheck(
    NetworkHistory& history,
    NodeID const& myId,
    std::size_t expect,
    PreVote const& pre = defaultPreVote)
{
    NegativeUNLVote vote(myId, history.env.journal);
    pre(vote);
    auto txSet = std::make_shared<SHAMap>(
        SHAMapType::TRANSACTION, history.env.app().family());
    vote.doVoting(
        history.lastLedger(), history.UNLKeySet, history.validations, txSet);
    return countTx(txSet) == expect;
}

/**
 * Test the private member functions of NegativeUNLVote
 */
class NegativeUNLVoteInternal_test : public beast::unit_test::suite
{
    void
    testAddTx()
    {
        testcase("Create UNLModify Tx");
        jtx::Env env(*this);

        NodeID myId(0xA0);
        NegativeUNLVote vote(myId, env.journal);

        // one add, one remove
        auto txSet = std::make_shared<SHAMap>(
            SHAMapType::TRANSACTION, env.app().family());
        PublicKey toDisableKey;
        PublicKey toReEnableKey;
        LedgerIndex seq(1234);
        BEAST_EXPECT(countTx(txSet) == 0);
        vote.addTx(seq, toDisableKey, true, txSet);
        BEAST_EXPECT(countTx(txSet) == 1);
        vote.addTx(seq, toReEnableKey, false, txSet);
        BEAST_EXPECT(countTx(txSet) == 2);
        // content of a tx is implicitly tested after applied to a ledger
        // in later test cases
    }

    void
    testPickOneCandidate()
    {
        testcase("Pick One Candidate");
        jtx::Env env(*this);

        NodeID myId(0xA0);
        NegativeUNLVote vote(myId, env.journal);

        uint256 pad_0(0);
        uint256 pad_f = ~pad_0;
        NodeID n_1(1);
        NodeID n_2(2);
        NodeID n_3(3);
        std::vector<NodeID> candidates({n_1});
        BEAST_EXPECT(vote.pickOneCandidate(pad_0, candidates) == n_1);
        BEAST_EXPECT(vote.pickOneCandidate(pad_f, candidates) == n_1);
        candidates.emplace_back(2);
        BEAST_EXPECT(vote.pickOneCandidate(pad_0, candidates) == n_1);
        BEAST_EXPECT(vote.pickOneCandidate(pad_f, candidates) == n_2);
        candidates.emplace_back(3);
        BEAST_EXPECT(vote.pickOneCandidate(pad_0, candidates) == n_1);
        BEAST_EXPECT(vote.pickOneCandidate(pad_f, candidates) == n_3);
    }

    void
    testBuildScoreTableSpecialCases()
    {
        testcase("Build Score Table");
        /*
         * 1. no skip list
         * 2. short skip list
         * 3. local node not enough history
         * 4. a node double validated some seq
         * 5. local node had enough validations but on a wrong chain
         * 6. a good case, long enough history and perfect scores
         */
        {
            // 1. no skip list
            NetworkHistory history = {*this, {10, 0, false, false, 1}};
            BEAST_EXPECT(history.goodHistory);
            if (history.goodHistory)
            {
                NegativeUNLVote vote(
                    history.UNLNodeIDs[3], history.env.journal);
                BEAST_EXPECT(!vote.buildScoreTable(
                    history.lastLedger(),
                    history.UNLNodeIDSet,
                    history.validations));
            }
        }

        {
            // 2. short skip list
            NetworkHistory history = {*this, {10, 0, false, false, 256 / 2}};
            BEAST_EXPECT(history.goodHistory);
            if (history.goodHistory)
            {
                NegativeUNLVote vote(
                    history.UNLNodeIDs[3], history.env.journal);
                BEAST_EXPECT(!vote.buildScoreTable(
                    history.lastLedger(),
                    history.UNLNodeIDSet,
                    history.validations));
            }
        }

        {
            // 3. local node not enough history
            NetworkHistory history = {*this, {10, 0, false, false, 256 + 2}};
            BEAST_EXPECT(history.goodHistory);
            if (history.goodHistory)
            {
                NodeID myId = history.UNLNodeIDs[3];
                history.walkHistoryAndAddValidations(
                    [&](std::shared_ptr<Ledger const> const& l,
                        std::size_t idx) -> bool {
                        // skip half my validations.
                        return !(
                            history.UNLNodeIDs[idx] == myId &&
                            l->seq() % 2 == 0);
                    });
                NegativeUNLVote vote(myId, history.env.journal);
                BEAST_EXPECT(!vote.buildScoreTable(
                    history.lastLedger(),
                    history.UNLNodeIDSet,
                    history.validations));
            }
        }

        {
            // 4. a node double validated some seq
            // 5. local node had enough validations but on a wrong chain
            NetworkHistory history = {*this, {10, 0, false, false, 256 + 2}};
            // We need two chains for these tests
            bool wrongChainSuccess = history.goodHistory;
            BEAST_EXPECT(wrongChainSuccess);
            NetworkHistory::LedgerHistory wrongChain =
                std::move(history.history);
            // Create a new chain and use it as the one that majority of nodes
            // follow
            history.createLedgerHistory();
            BEAST_EXPECT(history.goodHistory);

            if (history.goodHistory && wrongChainSuccess)
            {
                NodeID myId = history.UNLNodeIDs[3];
                NodeID badNode = history.UNLNodeIDs[4];
                history.walkHistoryAndAddValidations(
                    [&](std::shared_ptr<Ledger const> const& l,
                        std::size_t idx) -> bool {
                        // everyone but me
                        return !(history.UNLNodeIDs[idx] == myId);
                    });

                // local node validate wrong chain
                // a node double validates
                for (auto& l : wrongChain)
                {
                    RCLValidation v1(history.createSTVal(l, myId));
                    history.validations.add(myId, v1);
                    RCLValidation v2(history.createSTVal(l, badNode));
                    history.validations.add(badNode, v2);
                }

                NegativeUNLVote vote(myId, history.env.journal);

                // local node still on wrong chain, can build a scoreTable,
                // but all other nodes' scores are zero
                auto scoreTable = vote.buildScoreTable(
                    wrongChain.back(),
                    history.UNLNodeIDSet,
                    history.validations);
                BEAST_EXPECT(scoreTable);
                if (scoreTable)
                {
                    for (auto const& [n, score] : *scoreTable)
                    {
                        if (n == myId)
                            BEAST_EXPECT(score == 256);
                        else
                            BEAST_EXPECT(score == 0);
                    }
                }

                // if local node switched to right history, but cannot build
                // scoreTable because not enough local validations
                BEAST_EXPECT(!vote.buildScoreTable(
                    history.lastLedger(),
                    history.UNLNodeIDSet,
                    history.validations));
            }
        }

        {
            // 6. a good case
            NetworkHistory history = {*this, {10, 0, false, false, 256 + 1}};
            BEAST_EXPECT(history.goodHistory);
            if (history.goodHistory)
            {
                history.walkHistoryAndAddValidations(
                    [&](std::shared_ptr<Ledger const> const& l,
                        std::size_t idx) -> bool { return true; });
                NegativeUNLVote vote(
                    history.UNLNodeIDs[3], history.env.journal);
                auto scoreTable = vote.buildScoreTable(
                    history.lastLedger(),
                    history.UNLNodeIDSet,
                    history.validations);
                BEAST_EXPECT(scoreTable);
                if (scoreTable)
                {
                    for (auto const& [_, score] : *scoreTable)
                    {
                        (void)_;
                        BEAST_EXPECT(score == 256);
                    }
                }
            }
        }
    }

    /**
     * Find all candidates and check if the number of candidates meets
     * expectation
     *
     * @param vote the NegativeUNLVote object
     * @param unl the validators
     * @param negUnl the negative UNL validators
     * @param scoreTable the score table of validators
     * @param numDisable number of Disable candidates expected
     * @param numReEnable number of ReEnable candidates expected
     * @return true if the number of candidates meets expectation
     */
    bool
    checkCandidateSizes(
        NegativeUNLVote& vote,
        hash_set<NodeID> const& unl,
        hash_set<NodeID> const& negUnl,
        hash_map<NodeID, std::uint32_t> const& scoreTable,
        std::size_t numDisable,
        std::size_t numReEnable)
    {
        auto [disableCandidates, reEnableCandidates] =
            vote.findAllCandidates(unl, negUnl, scoreTable);
        bool rightDisable = disableCandidates.size() == numDisable;
        bool rightReEnable = reEnableCandidates.size() == numReEnable;
        return rightDisable && rightReEnable;
    };

    void
    testFindAllCandidates()
    {
        testcase("Find All Candidates");
        /*
         * -- unl size: 35
         * -- negUnl size: 3
         *
         * 0. all good scores
         * 1. all bad scores
         * 2. all between watermarks
         * 3. 2 good scorers in negUnl
         * 4. 2 bad scorers not in negUnl
         * 5. 2 in negUnl but not in unl, have a remove candidate from score
         * table
         * 6. 2 in negUnl but not in unl, no remove candidate from score table
         * 7. 2 new validators have good scores, already in negUnl
         * 8. 2 new validators have bad scores, not in negUnl
         * 9. expired the new validators have bad scores, not in negUnl
         */
        NetworkHistory history = {*this, {35, 0, false, false, 0}};

        hash_set<NodeID> negUnl_012;
        for (std::uint32_t i = 0; i < 3; ++i)
            negUnl_012.insert(history.UNLNodeIDs[i]);

        // build a good scoreTable to use, or copy and modify
        hash_map<NodeID, std::uint32_t> goodScoreTable;
        for (auto const& n : history.UNLNodeIDs)
            goodScoreTable[n] = NegativeUNLVote::negativeUnlHighWaterMark + 1;

        NegativeUNLVote vote(history.UNLNodeIDs[0], history.env.journal);

        {
            // all good scores
            BEAST_EXPECT(checkCandidateSizes(
                vote, history.UNLNodeIDSet, negUnl_012, goodScoreTable, 0, 3));
        }
        {
            // all bad scores
            hash_map<NodeID, std::uint32_t> scoreTable;
            for (auto& n : history.UNLNodeIDs)
                scoreTable[n] = NegativeUNLVote::negativeUnlLowWaterMark - 1;
            BEAST_EXPECT(checkCandidateSizes(
                vote, history.UNLNodeIDSet, negUnl_012, scoreTable, 35 - 3, 0));
        }
        {
            // all between watermarks
            hash_map<NodeID, std::uint32_t> scoreTable;
            for (auto& n : history.UNLNodeIDs)
                scoreTable[n] = NegativeUNLVote::negativeUnlLowWaterMark + 1;
            BEAST_EXPECT(checkCandidateSizes(
                vote, history.UNLNodeIDSet, negUnl_012, scoreTable, 0, 0));
        }

        {
            // 2 good scorers in negUnl
            auto scoreTable = goodScoreTable;
            scoreTable[*negUnl_012.begin()] =
                NegativeUNLVote::negativeUnlLowWaterMark + 1;
            BEAST_EXPECT(checkCandidateSizes(
                vote, history.UNLNodeIDSet, negUnl_012, scoreTable, 0, 2));
        }

        {
            // 2 bad scorers not in negUnl
            auto scoreTable = goodScoreTable;
            scoreTable[history.UNLNodeIDs[11]] =
                NegativeUNLVote::negativeUnlLowWaterMark - 1;
            scoreTable[history.UNLNodeIDs[12]] =
                NegativeUNLVote::negativeUnlLowWaterMark - 1;
            BEAST_EXPECT(checkCandidateSizes(
                vote, history.UNLNodeIDSet, negUnl_012, scoreTable, 2, 3));
        }

        {
            // 2 in negUnl but not in unl, have a remove candidate from score
            // table
            hash_set<NodeID> UNL_temp = history.UNLNodeIDSet;
            UNL_temp.erase(history.UNLNodeIDs[0]);
            UNL_temp.erase(history.UNLNodeIDs[1]);
            BEAST_EXPECT(checkCandidateSizes(
                vote, UNL_temp, negUnl_012, goodScoreTable, 0, 3));
        }

        {
            // 2 in negUnl but not in unl, no remove candidate from score table
            auto scoreTable = goodScoreTable;
            scoreTable.erase(history.UNLNodeIDs[0]);
            scoreTable.erase(history.UNLNodeIDs[1]);
            scoreTable[history.UNLNodeIDs[2]] =
                NegativeUNLVote::negativeUnlLowWaterMark + 1;
            hash_set<NodeID> UNL_temp = history.UNLNodeIDSet;
            UNL_temp.erase(history.UNLNodeIDs[0]);
            UNL_temp.erase(history.UNLNodeIDs[1]);
            BEAST_EXPECT(checkCandidateSizes(
                vote, UNL_temp, negUnl_012, scoreTable, 0, 2));
        }

        {
            // 2 new validators
            NodeID new_1(0xbead);
            NodeID new_2(0xbeef);
            hash_set<NodeID> nowTrusted = {new_1, new_2};
            hash_set<NodeID> UNL_temp = history.UNLNodeIDSet;
            UNL_temp.insert(new_1);
            UNL_temp.insert(new_2);
            vote.newValidators(256, nowTrusted);
            {
                // 2 new validators have good scores, already in negUnl
                auto scoreTable = goodScoreTable;
                scoreTable[new_1] =
                    NegativeUNLVote::negativeUnlHighWaterMark + 1;
                scoreTable[new_2] =
                    NegativeUNLVote::negativeUnlHighWaterMark + 1;
                hash_set<NodeID> negUnl_temp = negUnl_012;
                negUnl_temp.insert(new_1);
                negUnl_temp.insert(new_2);
                BEAST_EXPECT(checkCandidateSizes(
                    vote, UNL_temp, negUnl_temp, scoreTable, 0, 3 + 2));
            }
            {
                // 2 new validators have bad scores, not in negUnl
                auto scoreTable = goodScoreTable;
                scoreTable[new_1] = 0;
                scoreTable[new_2] = 0;
                BEAST_EXPECT(checkCandidateSizes(
                    vote, UNL_temp, negUnl_012, scoreTable, 0, 3));
            }
            {
                // expired the new validators have bad scores, not in negUnl
                vote.purgeNewValidators(
                    256 + NegativeUNLVote::newValidatorDisableSkip + 1);
                auto scoreTable = goodScoreTable;
                scoreTable[new_1] = 0;
                scoreTable[new_2] = 0;
                BEAST_EXPECT(checkCandidateSizes(
                    vote, UNL_temp, negUnl_012, scoreTable, 2, 3));
            }
        }
    }

    void
    testFindAllCandidatesCombination()
    {
        testcase("Find All Candidates Combination");
        /*
         * == combination 1:
         * -- unl size: 34, 35, 80
         * -- nUnl size: 0, 50%, all
         * -- score pattern: all 0, all negativeUnlLowWaterMark & +1 & -1, all
         * negativeUnlHighWaterMark & +1 & -1, all 100%
         *
         * == combination 2:
         * -- unl size: 34, 35, 80
         * -- negativeUnl size: 0, all
         * -- nUnl size: one on, one off, one on, one off,
         * -- score pattern: 2*(negativeUnlLowWaterMark, +1, -1) &
         * 2*(negativeUnlHighWaterMark, +1, -1) & rest
         * negativeUnlMinLocalValsToVote
         */

        jtx::Env env(*this);

        NodeID myId(0xA0);
        NegativeUNLVote vote(myId, env.journal);

        std::array<std::uint32_t, 3> unlSizes = {34, 35, 80};
        std::array<std::uint32_t, 3> nUnlPercent = {0, 50, 100};
        std::array<std::uint32_t, 8> scores = {
            0,
            NegativeUNLVote::negativeUnlLowWaterMark - 1,
            NegativeUNLVote::negativeUnlLowWaterMark,
            NegativeUNLVote::negativeUnlLowWaterMark + 1,
            NegativeUNLVote::negativeUnlHighWaterMark - 1,
            NegativeUNLVote::negativeUnlHighWaterMark,
            NegativeUNLVote::negativeUnlHighWaterMark + 1,
            NegativeUNLVote::negativeUnlMinLocalValsToVote};

        //== combination 1:
        {
            auto fillScoreTable =
                [&](std::uint32_t unl_size,
                    std::uint32_t nUnl_size,
                    std::uint32_t score,
                    hash_set<NodeID>& unl,
                    hash_set<NodeID>& negUnl,
                    hash_map<NodeID, std::uint32_t>& scoreTable) {
                    std::vector<NodeID> nodeIDs;
                    std::vector<PublicKey> keys = createPublicKeys(unl_size);
                    for (auto const& k : keys)
                    {
                        nodeIDs.emplace_back(calcNodeID(k));
                        unl.emplace(nodeIDs.back());
                        scoreTable[nodeIDs.back()] = score;
                    }
                    for (std::uint32_t i = 0; i < nUnl_size; ++i)
                        negUnl.insert(nodeIDs[i]);
                };

            for (auto us : unlSizes)
            {
                for (auto np : nUnlPercent)
                {
                    for (auto score : scores)
                    {
                        hash_set<NodeID> unl;
                        hash_set<NodeID> negUnl;
                        hash_map<NodeID, std::uint32_t> scoreTable;
                        fillScoreTable(
                            us, us * np / 100, score, unl, negUnl, scoreTable);
                        BEAST_EXPECT(unl.size() == us);
                        BEAST_EXPECT(negUnl.size() == us * np / 100);
                        BEAST_EXPECT(scoreTable.size() == us);

                        std::size_t toDisable_expect = 0;
                        std::size_t toReEnable_expect = 0;
                        if (np == 0)
                        {
                            if (score <
                                NegativeUNLVote::negativeUnlLowWaterMark)
                            {
                                toDisable_expect = us;
                            }
                        }
                        else if (np == 50)
                        {
                            if (score >
                                NegativeUNLVote::negativeUnlHighWaterMark)
                            {
                                toReEnable_expect = us * np / 100;
                            }
                        }
                        else
                        {
                            if (score >
                                NegativeUNLVote::negativeUnlHighWaterMark)
                            {
                                toReEnable_expect = us;
                            }
                        }
                        BEAST_EXPECT(checkCandidateSizes(
                            vote,
                            unl,
                            negUnl,
                            scoreTable,
                            toDisable_expect,
                            toReEnable_expect));
                    }
                }
            }

            //== combination 2:
            {
                auto fillScoreTable =
                    [&](std::uint32_t unl_size,
                        std::uint32_t nUnl_percent,
                        hash_set<NodeID>& unl,
                        hash_set<NodeID>& negUnl,
                        hash_map<NodeID, std::uint32_t>& scoreTable) {
                        std::vector<NodeID> nodeIDs;
                        std::vector<PublicKey> keys =
                            createPublicKeys(unl_size);
                        for (auto const& k : keys)
                        {
                            nodeIDs.emplace_back(calcNodeID(k));
                            unl.emplace(nodeIDs.back());
                        }

                        std::uint32_t nIdx = 0;
                        for (auto score : scores)
                        {
                            scoreTable[nodeIDs[nIdx++]] = score;
                            scoreTable[nodeIDs[nIdx++]] = score;
                        }
                        for (; nIdx < unl_size;)
                        {
                            scoreTable[nodeIDs[nIdx++]] = scores.back();
                        }

                        if (nUnl_percent == 100)
                        {
                            negUnl = unl;
                        }
                        else if (nUnl_percent == 50)
                        {
                            for (std::uint32_t i = 1; i < unl_size; i += 2)
                                negUnl.insert(nodeIDs[i]);
                        }
                    };

                for (auto us : unlSizes)
                {
                    for (auto np : nUnlPercent)
                    {
                        hash_set<NodeID> unl;
                        hash_set<NodeID> negUnl;
                        hash_map<NodeID, std::uint32_t> scoreTable;

                        fillScoreTable(us, np, unl, negUnl, scoreTable);
                        BEAST_EXPECT(unl.size() == us);
                        BEAST_EXPECT(negUnl.size() == us * np / 100);
                        BEAST_EXPECT(scoreTable.size() == us);

                        std::size_t toDisable_expect = 0;
                        std::size_t toReEnable_expect = 0;
                        if (np == 0)
                        {
                            toDisable_expect = 4;
                        }
                        else if (np == 50)
                        {
                            toReEnable_expect = negUnl.size() - 6;
                        }
                        else
                        {
                            toReEnable_expect = negUnl.size() - 12;
                        }
                        BEAST_EXPECT(checkCandidateSizes(
                            vote,
                            unl,
                            negUnl,
                            scoreTable,
                            toDisable_expect,
                            toReEnable_expect));
                    }
                }
            }
        }
    }

    void
    testNewValidators()
    {
        testcase("New Validators");
        jtx::Env env(*this);

        NodeID myId(0xA0);
        NegativeUNLVote vote(myId, env.journal);

        // test cases:
        // newValidators_ of the NegativeUNLVote empty, add one
        // add a new one and one already added
        // add a new one and some already added
        // purge and see some are expired

        NodeID n1(0xA1);
        NodeID n2(0xA2);
        NodeID n3(0xA3);

        vote.newValidators(2, {n1});
        BEAST_EXPECT(vote.newValidators_.size() == 1);
        if (vote.newValidators_.size() == 1)
        {
            BEAST_EXPECT(vote.newValidators_.begin()->first == n1);
            BEAST_EXPECT(vote.newValidators_.begin()->second == 2);
        }

        vote.newValidators(3, {n1, n2});
        BEAST_EXPECT(vote.newValidators_.size() == 2);
        if (vote.newValidators_.size() == 2)
        {
            BEAST_EXPECT(vote.newValidators_[n1] == 2);
            BEAST_EXPECT(vote.newValidators_[n2] == 3);
        }

        vote.newValidators(
            NegativeUNLVote::newValidatorDisableSkip, {n1, n2, n3});
        BEAST_EXPECT(vote.newValidators_.size() == 3);
        if (vote.newValidators_.size() == 3)
        {
            BEAST_EXPECT(vote.newValidators_[n1] == 2);
            BEAST_EXPECT(vote.newValidators_[n2] == 3);
            BEAST_EXPECT(
                vote.newValidators_[n3] ==
                NegativeUNLVote::newValidatorDisableSkip);
        }

        vote.purgeNewValidators(NegativeUNLVote::newValidatorDisableSkip + 2);
        BEAST_EXPECT(vote.newValidators_.size() == 3);
        vote.purgeNewValidators(NegativeUNLVote::newValidatorDisableSkip + 3);
        BEAST_EXPECT(vote.newValidators_.size() == 2);
        vote.purgeNewValidators(NegativeUNLVote::newValidatorDisableSkip + 4);
        BEAST_EXPECT(vote.newValidators_.size() == 1);
        BEAST_EXPECT(vote.newValidators_.begin()->first == n3);
        BEAST_EXPECT(
            vote.newValidators_.begin()->second ==
            NegativeUNLVote::newValidatorDisableSkip);
    }

    void
    run() override
    {
        testAddTx();
        testPickOneCandidate();
        testBuildScoreTableSpecialCases();
        testFindAllCandidates();
        testFindAllCandidatesCombination();
        testNewValidators();
    }
};

/**
 * Rest the build score table function of NegativeUNLVote.
 * This was a part of NegativeUNLVoteInternal. It is redundant and has long
 * runtime. So we separate it out as a manual test.
 */
class NegativeUNLVoteScoreTable_test : public beast::unit_test::suite
{
    void
    testBuildScoreTableCombination()
    {
        testcase("Build Score Table Combination");
        /*
         * local node good history, correct scores:
         * == combination:
         * -- unl size: 10, 34, 35, 50
         * -- score pattern: all 0, all 50%, all 100%, two 0% two 50% rest 100%
         */
        std::array<std::uint32_t, 4> unlSizes = {10, 34, 35, 50};
        std::array<std::array<std::uint32_t, 3>, 4> scorePattern = {
            {{{0, 0, 0}}, {{50, 50, 50}}, {{100, 100, 100}}, {{0, 50, 100}}}};

        for (auto unlSize : unlSizes)
        {
            for (std::uint32_t sp = 0; sp < 4; ++sp)
            {
                NetworkHistory history = {
                    *this, {unlSize, 0, false, false, 256 + 2}};
                BEAST_EXPECT(history.goodHistory);
                if (history.goodHistory)
                {
                    NodeID myId = history.UNLNodeIDs[3];
                    history.walkHistoryAndAddValidations(
                        [&](std::shared_ptr<Ledger const> const& l,
                            std::size_t idx) -> bool {
                            std::size_t k;
                            if (idx < 2)
                                k = 0;
                            else if (idx < 4)
                                k = 1;
                            else
                                k = 2;

                            bool add_50 =
                                scorePattern[sp][k] == 50 && l->seq() % 2 == 0;
                            bool add_100 = scorePattern[sp][k] == 100;
                            bool add_me = history.UNLNodeIDs[idx] == myId;
                            return add_50 || add_100 || add_me;
                        });

                    NegativeUNLVote vote(myId, history.env.journal);
                    auto scoreTable = vote.buildScoreTable(
                        history.lastLedger(),
                        history.UNLNodeIDSet,
                        history.validations);
                    BEAST_EXPECT(scoreTable);
                    if (scoreTable)
                    {
                        std::uint32_t i = 0;  // looping unl
                        auto checkScores = [&](std::uint32_t score,
                                               std::uint32_t k) -> bool {
                            if (history.UNLNodeIDs[i] == myId)
                                return score == 256;
                            if (scorePattern[sp][k] == 0)
                                return score == 0;
                            if (scorePattern[sp][k] == 50)
                                return score == 256 / 2;
                            if (scorePattern[sp][k] == 100)
                                return score == 256;
                            else
                                return false;
                        };
                        for (; i < 2; ++i)
                        {
                            BEAST_EXPECT(checkScores(
                                (*scoreTable)[history.UNLNodeIDs[i]], 0));
                        }
                        for (; i < 4; ++i)
                        {
                            BEAST_EXPECT(checkScores(
                                (*scoreTable)[history.UNLNodeIDs[i]], 1));
                        }
                        for (; i < unlSize; ++i)
                        {
                            BEAST_EXPECT(checkScores(
                                (*scoreTable)[history.UNLNodeIDs[i]], 2));
                        }
                    }
                }
            }
        }
    }

    void
    run() override
    {
        testBuildScoreTableCombination();
    }
};

/*
 * Test the doVoting function of NegativeUNLVote.
 * The test cases are split to 5 classes for parallel execution.
 *
 * Voting tests: (use hasToDisable and hasToReEnable in some of the cases)
 *
 * == all good score, nUnl empty
 * -- txSet.size = 0
 * == all good score, nUnl not empty (use hasToDisable)
 * -- txSet.size = 1
 *
 * == 2 nodes offline, nUnl empty (use hasToReEnable)
 * -- txSet.size = 1
 * == 2 nodes offline, in nUnl
 * -- txSet.size = 0
 *
 * == 2 nodes offline, not in nUnl, but maxListed
 * -- txSet.size = 0
 *
 * == 2 nodes offline including me, not in nUnl
 * -- txSet.size = 0
 * == 2 nodes offline, not in negativeUnl, but I'm not a validator
 * -- txSet.size = 0
 * == 2 in nUnl, but not in unl, no other remove candidates
 * -- txSet.size = 1
 *
 * == 2 new validators have bad scores
 * -- txSet.size = 0
 * == 2 expired new validators have bad scores
 * -- txSet.size = 1
 */

class NegativeUNLVoteGoodScore_test : public beast::unit_test::suite
{
    void
    testDoVoting()
    {
        testcase("Do Voting");

        {
            //== all good score, negativeUnl empty
            //-- txSet.size = 0
            NetworkHistory history = {*this, {51, 0, false, false, {}}};
            BEAST_EXPECT(history.goodHistory);
            if (history.goodHistory)
            {
                history.walkHistoryAndAddValidations(
                    [&](std::shared_ptr<Ledger const> const& l,
                        std::size_t idx) -> bool { return true; });
                BEAST_EXPECT(voteAndCheck(history, history.UNLNodeIDs[0], 0));
            }
        }

        {
            // all good score, negativeUnl not empty (use hasToDisable)
            //-- txSet.size = 1
            NetworkHistory history = {*this, {37, 0, true, false, {}}};
            BEAST_EXPECT(history.goodHistory);
            if (history.goodHistory)
            {
                history.walkHistoryAndAddValidations(
                    [&](std::shared_ptr<Ledger const> const& l,
                        std::size_t idx) -> bool { return true; });
                BEAST_EXPECT(voteAndCheck(history, history.UNLNodeIDs[0], 1));
            }
        }
    }

    void
    run() override
    {
        testDoVoting();
    }
};

class NegativeUNLVoteOffline_test : public beast::unit_test::suite
{
    void
    testDoVoting()
    {
        testcase("Do Voting");

        {
            //== 2 nodes offline, negativeUnl empty (use hasToReEnable)
            //-- txSet.size = 1
            NetworkHistory history = {*this, {29, 1, false, true, {}}};
            BEAST_EXPECT(history.goodHistory);
            if (history.goodHistory)
            {
                history.walkHistoryAndAddValidations(
                    [&](std::shared_ptr<Ledger const> const& l,
                        std::size_t idx) -> bool {
                        // skip node 0 and node 1
                        return idx > 1;
                    });
                BEAST_EXPECT(
                    voteAndCheck(history, history.UNLNodeIDs.back(), 1));
            }
        }

        {
            // 2 nodes offline, in negativeUnl
            //-- txSet.size = 0
            NetworkHistory history = {*this, {30, 1, true, false, {}}};
            BEAST_EXPECT(history.goodHistory);
            if (history.goodHistory)
            {
                NodeID n1 =
                    calcNodeID(*history.lastLedger()->negativeUnl().begin());
                NodeID n2 =
                    calcNodeID(*history.lastLedger()->negativeUnlToDisable());
                history.walkHistoryAndAddValidations(
                    [&](std::shared_ptr<Ledger const> const& l,
                        std::size_t idx) -> bool {
                        // skip node 0 and node 1
                        return history.UNLNodeIDs[idx] != n1 &&
                            history.UNLNodeIDs[idx] != n2;
                    });
                BEAST_EXPECT(
                    voteAndCheck(history, history.UNLNodeIDs.back(), 0));
            }
        }
    }

    void
    run() override
    {
        testDoVoting();
    }
};

class NegativeUNLVoteMaxListed_test : public beast::unit_test::suite
{
    void
    testDoVoting()
    {
        testcase("Do Voting");

        {
            // 2 nodes offline, not in negativeUnl, but maxListed
            //-- txSet.size = 0
            NetworkHistory history = {*this, {32, 8, true, true, {}}};
            BEAST_EXPECT(history.goodHistory);
            if (history.goodHistory)
            {
                history.walkHistoryAndAddValidations(
                    [&](std::shared_ptr<Ledger const> const& l,
                        std::size_t idx) -> bool {
                        // skip node 0 ~ 10
                        return idx > 10;
                    });
                BEAST_EXPECT(
                    voteAndCheck(history, history.UNLNodeIDs.back(), 0));
            }
        }
    }

    void
    run() override
    {
        testDoVoting();
    }
};

class NegativeUNLVoteRetiredValidator_test : public beast::unit_test::suite
{
    void
    testDoVoting()
    {
        testcase("Do Voting");

        {
            //== 2 nodes offline including me, not in negativeUnl
            //-- txSet.size = 0
            NetworkHistory history = {*this, {35, 0, false, false, {}}};
            BEAST_EXPECT(history.goodHistory);
            if (history.goodHistory)
            {
                history.walkHistoryAndAddValidations(
                    [&](std::shared_ptr<Ledger const> const& l,
                        std::size_t idx) -> bool { return idx > 1; });
                BEAST_EXPECT(voteAndCheck(history, history.UNLNodeIDs[0], 0));
            }
        }

        {
            // 2 nodes offline, not in negativeUnl, but I'm not a validator
            //-- txSet.size = 0
            NetworkHistory history = {*this, {40, 0, false, false, {}}};
            BEAST_EXPECT(history.goodHistory);
            if (history.goodHistory)
            {
                history.walkHistoryAndAddValidations(
                    [&](std::shared_ptr<Ledger const> const& l,
                        std::size_t idx) -> bool { return idx > 1; });
                BEAST_EXPECT(voteAndCheck(history, NodeID(0xdeadbeef), 0));
            }
        }

        {
            //== 2 in negativeUnl, but not in unl, no other remove candidates
            //-- txSet.size = 1
            NetworkHistory history = {*this, {25, 2, false, false, {}}};
            BEAST_EXPECT(history.goodHistory);
            if (history.goodHistory)
            {
                history.walkHistoryAndAddValidations(
                    [&](std::shared_ptr<Ledger const> const& l,
                        std::size_t idx) -> bool { return idx > 1; });
                BEAST_EXPECT(voteAndCheck(
                    history,
                    history.UNLNodeIDs.back(),
                    1,
                    [&](NegativeUNLVote& vote) {
                        history.UNLKeySet.erase(history.UNLKeys[0]);
                        history.UNLKeySet.erase(history.UNLKeys[1]);
                    }));
            }
        }
    }

    void
    run() override
    {
        testDoVoting();
    }
};

class NegativeUNLVoteNewValidator_test : public beast::unit_test::suite
{
    void
    testDoVoting()
    {
        testcase("Do Voting");

        {
            //== 2 new validators have bad scores
            //-- txSet.size = 0
            NetworkHistory history = {*this, {15, 0, false, false, {}}};
            BEAST_EXPECT(history.goodHistory);
            if (history.goodHistory)
            {
                history.walkHistoryAndAddValidations(
                    [&](std::shared_ptr<Ledger const> const& l,
                        std::size_t idx) -> bool { return true; });
                BEAST_EXPECT(voteAndCheck(
                    history,
                    history.UNLNodeIDs[0],
                    0,
                    [&](NegativeUNLVote& vote) {
                        auto extra_key_1 =
                            randomKeyPair(KeyType::ed25519).first;
                        auto extra_key_2 =
                            randomKeyPair(KeyType::ed25519).first;
                        history.UNLKeySet.insert(extra_key_1);
                        history.UNLKeySet.insert(extra_key_2);
                        hash_set<NodeID> nowTrusted;
                        nowTrusted.insert(calcNodeID(extra_key_1));
                        nowTrusted.insert(calcNodeID(extra_key_2));
                        vote.newValidators(
                            history.lastLedger()->seq(), nowTrusted);
                    }));
            }
        }

        {
            //== 2 expired new validators have bad scores
            //-- txSet.size = 1
            NetworkHistory history = {
                *this,
                {21,
                 0,
                 false,
                 false,
                 NegativeUNLVote::newValidatorDisableSkip * 2}};
            BEAST_EXPECT(history.goodHistory);
            if (history.goodHistory)
            {
                history.walkHistoryAndAddValidations(
                    [&](std::shared_ptr<Ledger const> const& l,
                        std::size_t idx) -> bool { return true; });
                BEAST_EXPECT(voteAndCheck(
                    history,
                    history.UNLNodeIDs[0],
                    1,
                    [&](NegativeUNLVote& vote) {
                        auto extra_key_1 =
                            randomKeyPair(KeyType::ed25519).first;
                        auto extra_key_2 =
                            randomKeyPair(KeyType::ed25519).first;
                        history.UNLKeySet.insert(extra_key_1);
                        history.UNLKeySet.insert(extra_key_2);
                        hash_set<NodeID> nowTrusted;
                        nowTrusted.insert(calcNodeID(extra_key_1));
                        nowTrusted.insert(calcNodeID(extra_key_2));
                        vote.newValidators(256, nowTrusted);
                    }));
            }
        }
    }

    void
    run() override
    {
        testDoVoting();
    }
};

class NegativeUNLVoteFilterValidations_test : public beast::unit_test::suite
{
    void
    testFilterValidations()
    {
        testcase("Filter Validations");

        NetworkHistory history = {*this, {28, 7, false, false, {}}};
        BEAST_EXPECT(history.goodHistory);
        if (history.goodHistory)
        {
            history.walkHistoryAndAddValidations(
                [&](std::shared_ptr<Ledger const> const& l,
                    std::size_t idx) -> bool {
                    return l->seq() == history.lastLedger()->seq();
                });

            auto l = history.lastLedger();
            auto nUnlKeys = l->negativeUnl();
            auto vals = history.validations.getTrustedForLedger(l->info().hash);
            BEAST_EXPECT(vals.size() == 28);
            hash_set<NodeID> negUnl;
            for (auto& k : nUnlKeys)
                negUnl.insert(calcNodeID(k));
            vals = negativeUNLFilter(vals, negUnl);
            BEAST_EXPECT(vals.size() == 28 - 7);
        }
    }

    void
    run() override
    {
        testFilterValidations();
    }
};

class NegativeUNLgRPC_test : public beast::unit_test::suite
{
    template <class T>
    std::string
    toByteString(T const& data)
    {
        const char* bytes = reinterpret_cast<const char*>(data.data());
        return {bytes, data.size()};
    }

    void
    testGRPC()
    {
        testcase("gRPC test");

        auto gRpcTest = [this](
                            std::uint32_t negUnlSize,
                            bool hasToDisable,
                            bool hasToReEnable) -> bool {
            NetworkHistory history = {
                *this, {20, negUnlSize, hasToDisable, hasToReEnable, {}}};
            if (!history.goodHistory)
                return false;

            auto const& negUnlObject =
                history.lastLedger()->read(keylet::negativeUNL());
            if (!negUnlSize && !hasToDisable && !hasToReEnable && !negUnlObject)
                return true;
            if (!negUnlObject)
                return false;

            org::xrpl::rpc::v1::NegativeUnl to;
            ripple::RPC::convert(to, *negUnlObject);
            bool goodSize = to.negative_unl_entries_size() == negUnlSize &&
                to.has_validator_to_disable() == hasToDisable &&
                to.has_validator_to_re_enable() == hasToReEnable;
            if (!goodSize)
                return false;

            if (negUnlSize)
            {
                if (!negUnlObject->isFieldPresent(sfNegativeUNL))
                    return false;
                auto const& nUnlData =
                    negUnlObject->getFieldArray(sfNegativeUNL);
                if (nUnlData.size() != negUnlSize)
                    return false;
                int idx = 0;
                for (auto const& n : nUnlData)
                {
                    if (!n.isFieldPresent(sfPublicKey) ||
                        !n.isFieldPresent(sfFirstLedgerSequence))
                        return false;

                    if (!to.negative_unl_entries(idx).has_ledger_sequence() ||
                        !to.negative_unl_entries(idx).has_public_key())
                        return false;

                    if (to.negative_unl_entries(idx).public_key().value() !=
                        toByteString(n.getFieldVL(sfPublicKey)))
                        return false;

                    if (to.negative_unl_entries(idx)
                            .ledger_sequence()
                            .value() != n.getFieldU32(sfFirstLedgerSequence))
                        return false;

                    ++idx;
                }
            }

            if (hasToDisable)
            {
                if (!negUnlObject->isFieldPresent(sfNegativeUNLToDisable))
                    return false;
                if (to.validator_to_disable().value() !=
                    toByteString(
                        negUnlObject->getFieldVL(sfNegativeUNLToDisable)))
                    return false;
            }

            if (hasToReEnable)
            {
                if (!negUnlObject->isFieldPresent(sfNegativeUNLToReEnable))
                    return false;
                if (to.validator_to_re_enable().value() !=
                    toByteString(
                        negUnlObject->getFieldVL(sfNegativeUNLToReEnable)))
                    return false;
            }

            return true;
        };

        BEAST_EXPECT(gRpcTest(0, false, false));
        BEAST_EXPECT(gRpcTest(2, true, true));
    }

    void
    run() override
    {
        testGRPC();
    }
};

BEAST_DEFINE_TESTSUITE(NegativeUNL, ledger, ripple);
BEAST_DEFINE_TESTSUITE(NegativeUNLNoAmendment, ledger, ripple);

BEAST_DEFINE_TESTSUITE(NegativeUNLVoteInternal, consensus, ripple);
BEAST_DEFINE_TESTSUITE_MANUAL(NegativeUNLVoteScoreTable, consensus, ripple);
BEAST_DEFINE_TESTSUITE_PRIO(NegativeUNLVoteGoodScore, consensus, ripple, 1);
BEAST_DEFINE_TESTSUITE_PRIO(NegativeUNLVoteOffline, consensus, ripple, 1);
BEAST_DEFINE_TESTSUITE_PRIO(NegativeUNLVoteMaxListed, consensus, ripple, 1);
BEAST_DEFINE_TESTSUITE_PRIO(
    NegativeUNLVoteRetiredValidator,
    consensus,
    ripple,
    1);
BEAST_DEFINE_TESTSUITE_PRIO(NegativeUNLVoteNewValidator, consensus, ripple, 1);
BEAST_DEFINE_TESTSUITE(NegativeUNLVoteFilterValidations, consensus, ripple);
BEAST_DEFINE_TESTSUITE(NegativeUNLgRPC, ledger, ripple);

///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
bool
negUnlSizeTest(
    std::shared_ptr<Ledger const> const& l,
    size_t size,
    bool hasToDisable,
    bool hasToReEnable)
{
    bool sameSize = l->negativeUnl().size() == size;
    bool sameToDisable =
        (l->negativeUnlToDisable() != boost::none) == hasToDisable;
    bool sameToReEnable =
        (l->negativeUnlToReEnable() != boost::none) == hasToReEnable;

    return sameSize && sameToDisable && sameToReEnable;
}

bool
applyAndTestResult(jtx::Env& env, OpenView& view, STTx const& tx, bool pass)
{
    auto res = apply(env.app(), view, tx, ApplyFlags::tapNONE, env.journal);
    if (pass)
        return res.first == tesSUCCESS;
    else
        return res.first == tefFAILURE || res.first == temDISABLED;
}

bool
VerifyPubKeyAndSeq(
    std::shared_ptr<Ledger const> const& l,
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
}

std::size_t
countTx(std::shared_ptr<SHAMap> const& txSet)
{
    std::size_t count = 0;
    for (auto i = txSet->begin(); i != txSet->end(); ++i)
    {
        ++count;
    }
    return count;
};

std::vector<PublicKey>
createPublicKeys(std::size_t n)
{
    std::vector<PublicKey> keys;
    std::size_t ss = 33;
    std::vector<uint8_t> data(ss, 0);
    data[0] = 0xED;
    for (int i = 0; i < n; ++i)
    {
        data[1]++;
        Slice s(data.data(), ss);
        keys.emplace_back(s);
    }
    return keys;
}

STTx
createTx(bool disabling, LedgerIndex seq, PublicKey const& txKey)
{
    auto fill = [&](auto& obj) {
        obj.setFieldU8(sfUNLModifyDisabling, disabling ? 1 : 0);
        obj.setFieldU32(sfLedgerSequence, seq);
        obj.setFieldVL(sfUNLModifyValidator, txKey);
    };
    return STTx(ttUNL_MODIFY, fill);
}

}  // namespace test
}  // namespace ripple
