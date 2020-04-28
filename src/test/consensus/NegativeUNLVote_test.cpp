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
    bool hasToRemove);
bool
applyAndTestResult(jtx::Env& env, OpenView& view, STTx const& tx, bool pass);

unsigned int
countTx(std::shared_ptr<SHAMap> const& txSet)
{
    unsigned int count = 0;
    for (auto i = txSet->begin(); i != txSet->end(); ++i)
    {
        ++count;
    }
    return count;
};

STValidation::pointer
createSTVal(jtx::Env& env, std::shared_ptr<Ledger> ledger, NodeID const& n)
{
    static auto keyPair = randomKeyPair(KeyType::secp256k1);
    static uint256 consensusHash;
    static STValidation::FeeSettings fees;
    static std::vector<uint256> amendments;
    return std::make_shared<STValidation>(
        ledger->info().hash,
        ledger->seq(),
        consensusHash,
        env.app().timeKeeper().now(),
        keyPair.first,
        keyPair.second,
        n,
        true,
        fees,
        amendments);
};

void
createNodeIDs(
    int numNodes,
    std::vector<NodeID>& nodeIDs,
    std::vector<PublicKey>& UNLKeys)
{
    assert(numNodes <= 256);
    std::size_t ss = 33;
    std::vector<uint8_t> data(ss, 0);
    data[0] = 0xED;
    for (int i = 0; i < numNodes; ++i)
    {
        data[1]++;
        Slice s(data.data(), ss);
        PublicKey k(s);
        UNLKeys.push_back(k);
        NodeID nid = calcNodeID(k);
        nodeIDs.push_back(nid);
    }
}

/*
 * only reasonable values can be honored,
 * e.g cannot hasToRemove when nUNLSize == 0
 */
using LedgerHistory = std::vector<std::shared_ptr<Ledger>>;
bool
createLedgerHistory(
    LedgerHistory& history,  // out
    jtx::Env& env,
    std::vector<PublicKey> const& nodes,
    int nUNLSize,
    bool hasToAdd,
    bool hasToRemove,
    int numLedgers = 0)
{
    static uint256 fake_amemdment;
    auto l = std::make_shared<Ledger>(
        create_genesis,
        env.app().config(),
        std::vector<uint256>{fake_amemdment++},
        env.app().family());
    history.push_back(l);
    bool adding = true;
    int nidx = 0;
    auto fill = [&](auto& obj) {
        obj.setFieldU8(sfUNLModifyDisabling, adding ? 1 : 0);
        obj.setFieldU32(sfLedgerSequence, l->seq());
        obj.setFieldVL(sfUNLModifyValidator, nodes[nidx]);
    };

    if (!numLedgers)
        numLedgers = 256 * (nUNLSize + 1);

    while (l->seq() <= numLedgers)
    {
        auto next =
            std::make_shared<Ledger>(*l, env.app().timeKeeper().closeTime());
        l = next;
        history.push_back(l);

        if (l->seq() % 256 == 0)
        {
            OpenView accum(&*l);
            if (l->nUnl().size() < nUNLSize)
            {
                STTx tx(ttUNL_MODIDY, fill);
                if (!applyAndTestResult(env, accum, tx, true))
                    break;
                ++nidx;
            }
            else if (l->nUnl().size() == nUNLSize)
            {
                if (hasToAdd)
                {
                    STTx tx(ttUNL_MODIDY, fill);
                    if (!applyAndTestResult(env, accum, tx, true))
                        break;
                    ++nidx;
                }
                if (hasToRemove)
                {
                    adding = false;
                    nidx = 0;
                    STTx tx(ttUNL_MODIDY, fill);
                    if (!applyAndTestResult(env, accum, tx, true))
                        break;
                }
            }
            accum.apply(*l);
        }
        l->updateSkipList();
    }
    return nUnlSizeTest(env, l, nUNLSize, hasToAdd, hasToRemove);
}

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
         * 4. local node double validated some seq
         * 5. local node good history, but not a validator,
         */
        {
            jtx::Env env(*this);
            RCLValidations& validations = env.app().getValidations();

            std::vector<NodeID> nodeIDs;
            std::vector<PublicKey> UNLKeys;
            createNodeIDs(10, nodeIDs, UNLKeys);
            hash_set<NodeID> UNLNodeIDs(nodeIDs.begin(), nodeIDs.end());

            LedgerHistory history;
            bool goodHistory =
                createLedgerHistory(history, env, UNLKeys, 0, false, false, 1);
            BEAST_EXPECT(goodHistory);
            if (goodHistory)
            {
                NodeID myId = nodeIDs[3];
                NegativeUNLVote vote(myId, env.journal);
                hash_map<NodeID, unsigned int> scoreTable;
                BEAST_EXPECT(!vote.buildScoreTable(
                    history[0], UNLNodeIDs, validations, scoreTable));
            }
        }

        {
            jtx::Env env(*this);
            RCLValidations& validations = env.app().getValidations();

            std::vector<NodeID> nodeIDs;
            std::vector<PublicKey> UNLKeys;
            createNodeIDs(10, nodeIDs, UNLKeys);
            hash_set<NodeID> UNLNodeIDs(nodeIDs.begin(), nodeIDs.end());

            LedgerHistory history;
            bool goodHistory = createLedgerHistory(
                history, env, UNLKeys, 0, false, false, 256 / 2);
            BEAST_EXPECT(goodHistory);
            if (goodHistory)
            {
                NodeID myId = nodeIDs[3];
                NegativeUNLVote vote(myId, env.journal);
                hash_map<NodeID, unsigned int> scoreTable;
                BEAST_EXPECT(!vote.buildScoreTable(
                    history.back(), UNLNodeIDs, validations, scoreTable));
            }
        }

        {
            jtx::Env env(*this);
            RCLValidations& validations = env.app().getValidations();

            std::vector<NodeID> nodeIDs;
            std::vector<PublicKey> UNLKeys;
            createNodeIDs(10, nodeIDs, UNLKeys);
            hash_set<NodeID> UNLNodeIDs(nodeIDs.begin(), nodeIDs.end());

            LedgerHistory history;
            bool goodHistory = createLedgerHistory(
                history, env, UNLKeys, 0, false, false, 256 + 2);
            BEAST_EXPECT(goodHistory);
            if (goodHistory)
            {
                NodeID myId = nodeIDs[3];
                for (auto& l : history)
                {
                    unsigned int unlSize = UNLNodeIDs.size();
                    for (unsigned int i = 0; i < unlSize; ++i)
                    {
                        if (nodeIDs[i] == myId && l->seq() % 2 == 0)
                            continue;
                        RCLValidation v(createSTVal(env, l, nodeIDs[i]));
                        validations.add(nodeIDs[i], v);
                    }
                }
                NegativeUNLVote vote(myId, env.journal);
                hash_map<NodeID, unsigned int> scoreTable;
                BEAST_EXPECT(!vote.buildScoreTable(
                    history.back(), UNLNodeIDs, validations, scoreTable));
            }
        }

        {
            jtx::Env env(*this);
            RCLValidations& validations = env.app().getValidations();

            std::vector<NodeID> nodeIDs;
            std::vector<PublicKey> UNLKeys;
            createNodeIDs(10, nodeIDs, UNLKeys);
            hash_set<NodeID> UNLNodeIDs(nodeIDs.begin(), nodeIDs.end());

            std::shared_ptr<Ledger const> firstRound;
            {
                LedgerHistory history;
                bool goodHistory = createLedgerHistory(
                    history, env, UNLKeys, 0, false, false, 256 + 2);
                BEAST_EXPECT(goodHistory);
                if (goodHistory)
                {
                    NodeID myId = nodeIDs[3];
                    for (auto& l : history)
                    {
                        unsigned int unlSize = UNLNodeIDs.size();
                        for (unsigned int i = 0; i < unlSize; ++i)
                        {
                            RCLValidation v(createSTVal(env, l, nodeIDs[i]));
                            validations.add(nodeIDs[i], v);
                        }
                    }
                    NegativeUNLVote vote(myId, env.journal);
                    hash_map<NodeID, unsigned int> scoreTable;
                    BEAST_EXPECT(vote.buildScoreTable(
                        history.back(), UNLNodeIDs, validations, scoreTable));
                    for (auto& s : scoreTable)
                    {
                        BEAST_EXPECT(s.second == 256);
                    }
                    firstRound = history.back();
                }
            }

            {
                LedgerHistory history;
                bool goodHistory = createLedgerHistory(
                    history, env, UNLKeys, 0, false, false, 256 + 2);
                BEAST_EXPECT(goodHistory);
                if (goodHistory)
                {
                    NodeID myId = nodeIDs[3];
                    for (auto& l : history)
                    {
                        RCLValidation v(createSTVal(env, l, myId));
                        validations.add(myId, v);
                    }
                    NegativeUNLVote vote(myId, env.journal);
                    hash_map<NodeID, unsigned int> scoreTable;
                    BEAST_EXPECT(!vote.buildScoreTable(
                        history.back(), UNLNodeIDs, validations, scoreTable));
                    scoreTable.clear();
                    BEAST_EXPECT(vote.buildScoreTable(
                        firstRound, UNLNodeIDs, validations, scoreTable));
                    for (auto& s : scoreTable)
                    {
                        BEAST_EXPECT(s.second == 256);
                    }
                }
            }
        }

        {
            jtx::Env env(*this);

            RCLValidations& validations = env.app().getValidations();

            std::vector<NodeID> nodeIDs;
            std::vector<PublicKey> UNLKeys;
            createNodeIDs(10, nodeIDs, UNLKeys);
            hash_set<NodeID> UNLNodeIDs(nodeIDs.begin(), nodeIDs.end());

            LedgerHistory history;
            bool goodHistory = createLedgerHistory(
                history, env, UNLKeys, 0, false, false, 256 + 2);
            BEAST_EXPECT(goodHistory);
            if (goodHistory)
            {
                NodeID myId(0xdeadbeef);
                for (auto& l : history)
                {
                    unsigned int unlSize = UNLNodeIDs.size();
                    for (unsigned int i = 0; i < unlSize; ++i)
                    {
                        RCLValidation v(createSTVal(env, l, nodeIDs[i]));
                        validations.add(nodeIDs[i], v);
                    }
                }
                NegativeUNLVote vote(myId, env.journal);
                hash_map<NodeID, unsigned int> scoreTable;
                BEAST_EXPECT(!vote.buildScoreTable(
                    history.back(), UNLNodeIDs, validations, scoreTable));
            }
        }
    }

    void
    testFindAllCandidates()
    {
        testcase("Find All Candidates");
        /*
         * -- unl size: 35
         * -- nUnl size: 3
         *
         * 0. all good scores
         * 1. all bad scores
         * 2. all between watermarks
         * 3. 2 good scorers in nUnl
         * 4. 2 bad scorers not in nUnl
         * 5. 2 in nUnl but not in unl, have a remove candidate from score table
         * 6. 2 in nUnl but not in unl, no remove candidate from score table
         * 7. 2 new validators have good scores, already in nUnl
         * 8. 2 new validators have bad scores, not in nUnl
         * 9. expired the new validators have bad scores, not in nUnl
         */

        jtx::Env env(*this);

        std::vector<NodeID> nodeIDs;
        std::vector<PublicKey> UNLKeys;
        createNodeIDs(35, nodeIDs, UNLKeys);
        hash_set<NodeID> UNL(nodeIDs.begin(), nodeIDs.end());

        hash_set<NodeID> nUnl;
        for (uint i = 0; i < 3; ++i)
            nUnl.insert(nodeIDs[i]);
        hash_map<NodeID, unsigned int> goodScoreTable;
        for (auto& n : nodeIDs)
            goodScoreTable[n] = NegativeUNLVote::nUnlHighWaterMark + 1;
        NodeID myId = nodeIDs[0];
        NegativeUNLVote vote(myId, env.journal);

        {
            // all good scores
            hash_map<NodeID, unsigned int> scoreTable = goodScoreTable;
            std::vector<NodeID> addCandidates;
            std::vector<NodeID> removeCandidates;
            vote.findAllCandidates(
                UNL, nUnl, scoreTable, addCandidates, removeCandidates);
            BEAST_EXPECT(addCandidates.size() == 0);
            BEAST_EXPECT(removeCandidates.size() == 3);
        }
        {
            // all bad scores
            hash_map<NodeID, unsigned int> scoreTable;
            for (auto& n : nodeIDs)
                scoreTable[n] = NegativeUNLVote::nUnlLowWaterMark - 1;
            std::vector<NodeID> addCandidates;
            std::vector<NodeID> removeCandidates;
            vote.findAllCandidates(
                UNL, nUnl, scoreTable, addCandidates, removeCandidates);
            BEAST_EXPECT(addCandidates.size() == 35 - 3);
            BEAST_EXPECT(removeCandidates.size() == 0);
        }
        {
            // all between watermarks
            hash_map<NodeID, unsigned int> scoreTable;
            for (auto& n : nodeIDs)
                scoreTable[n] = NegativeUNLVote::nUnlLowWaterMark + 1;
            std::vector<NodeID> addCandidates;
            std::vector<NodeID> removeCandidates;
            vote.findAllCandidates(
                UNL, nUnl, scoreTable, addCandidates, removeCandidates);
            BEAST_EXPECT(addCandidates.size() == 0);
            BEAST_EXPECT(removeCandidates.size() == 0);
        }

        {
            // 2 good scorers in nUnl
            hash_map<NodeID, unsigned int> scoreTable = goodScoreTable;
            scoreTable[nodeIDs[2]] = NegativeUNLVote::nUnlLowWaterMark + 1;
            std::vector<NodeID> addCandidates;
            std::vector<NodeID> removeCandidates;
            vote.findAllCandidates(
                UNL, nUnl, scoreTable, addCandidates, removeCandidates);
            BEAST_EXPECT(addCandidates.size() == 0);
            BEAST_EXPECT(removeCandidates.size() == 2);
        }

        {
            // 2 bad scorers not in nUnl
            hash_map<NodeID, unsigned int> scoreTable = goodScoreTable;
            scoreTable[nodeIDs[11]] = NegativeUNLVote::nUnlLowWaterMark - 1;
            scoreTable[nodeIDs[12]] = NegativeUNLVote::nUnlLowWaterMark - 1;
            std::vector<NodeID> addCandidates;
            std::vector<NodeID> removeCandidates;
            vote.findAllCandidates(
                UNL, nUnl, scoreTable, addCandidates, removeCandidates);
            BEAST_EXPECT(addCandidates.size() == 2);
            BEAST_EXPECT(removeCandidates.size() == 3);
        }

        {
            // 2 in nUnl but not in unl, have a remove candidate from score
            // table
            hash_map<NodeID, unsigned int> scoreTable = goodScoreTable;
            hash_set<NodeID> UNL_temp = UNL;
            UNL_temp.erase(nodeIDs[0]);
            UNL_temp.erase(nodeIDs[1]);
            std::vector<NodeID> addCandidates;
            std::vector<NodeID> removeCandidates;
            vote.findAllCandidates(
                UNL_temp, nUnl, scoreTable, addCandidates, removeCandidates);
            BEAST_EXPECT(addCandidates.size() == 0);
            BEAST_EXPECT(removeCandidates.size() == 3);
        }

        {
            // 2 in nUnl but not in unl, no remove candidate from score table
            hash_map<NodeID, unsigned int> scoreTable = goodScoreTable;
            scoreTable.erase(nodeIDs[0]);
            scoreTable.erase(nodeIDs[1]);
            scoreTable[nodeIDs[2]] = NegativeUNLVote::nUnlLowWaterMark + 1;
            hash_set<NodeID> UNL_temp = UNL;
            UNL_temp.erase(nodeIDs[0]);
            UNL_temp.erase(nodeIDs[1]);
            std::vector<NodeID> addCandidates;
            std::vector<NodeID> removeCandidates;
            vote.findAllCandidates(
                UNL_temp, nUnl, scoreTable, addCandidates, removeCandidates);
            BEAST_EXPECT(addCandidates.size() == 0);
            BEAST_EXPECT(removeCandidates.size() == 2);
        }

        {
            // 2 new validators
            NodeID new_1(0xbead);
            NodeID new_2(0xbeef);
            hash_set<NodeID> nowTrusted = {new_1, new_2};
            hash_set<NodeID> UNL_temp = UNL;
            UNL_temp.insert(new_1);
            UNL_temp.insert(new_2);
            vote.newValidators(256, nowTrusted);
            {
                // 2 new validators have good scores, already in nUnl
                hash_map<NodeID, unsigned int> scoreTable = goodScoreTable;
                scoreTable[new_1] = NegativeUNLVote::nUnlHighWaterMark + 1;
                scoreTable[new_2] = NegativeUNLVote::nUnlHighWaterMark + 1;
                hash_set<NodeID> nUnl_temp = nUnl;
                nUnl_temp.insert(new_1);
                nUnl_temp.insert(new_2);
                std::vector<NodeID> addCandidates;
                std::vector<NodeID> removeCandidates;
                vote.findAllCandidates(
                    UNL_temp,
                    nUnl_temp,
                    scoreTable,
                    addCandidates,
                    removeCandidates);
                BEAST_EXPECT(addCandidates.size() == 0);
                BEAST_EXPECT(removeCandidates.size() == 3 + 2);
            }
            {
                // 2 new validators have bad scores, not in nUnl
                hash_map<NodeID, unsigned int> scoreTable = goodScoreTable;
                scoreTable[new_1] = 0;
                scoreTable[new_2] = 0;
                std::vector<NodeID> addCandidates;
                std::vector<NodeID> removeCandidates;
                vote.findAllCandidates(
                    UNL_temp,
                    nUnl,
                    scoreTable,
                    addCandidates,
                    removeCandidates);
                BEAST_EXPECT(addCandidates.size() == 0);
                BEAST_EXPECT(removeCandidates.size() == 3);
            }
            {
                // expired the new validators have bad scores, not in nUnl
                vote.purgeNewValidators(
                    256 + NegativeUNLVote::newValidatorDisableSkip + 1);
                hash_map<NodeID, unsigned int> scoreTable = goodScoreTable;
                scoreTable[new_1] = 0;
                scoreTable[new_2] = 0;
                std::vector<NodeID> addCandidates;
                std::vector<NodeID> removeCandidates;
                vote.findAllCandidates(
                    UNL_temp,
                    nUnl,
                    scoreTable,
                    addCandidates,
                    removeCandidates);
                BEAST_EXPECT(addCandidates.size() == 2);
                BEAST_EXPECT(removeCandidates.size() == 3);
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
         * -- score pattern: all 0, all nUnlLowWaterMark & +1 & -1, all
         * nUnlHighWaterMark & +1 & -1, all 100%
         *
         * == combination 2:
         * -- unl size: 34, 35, 80
         * -- nUnl size: 0, all
         * -- nUnl size: one on, one off, one on, one off,
         * -- score pattern: 2*(nUnlLowWaterMark, +1, -1) &
         * 2*(nUnlHighWaterMark, +1, -1) & rest nUnlMinLocalValsToVote
         */

        jtx::Env env(*this);

        NodeID myId(0xA0);
        NegativeUNLVote vote(myId, env.journal);

        std::array<uint, 3> unlSizes({34, 35, 80});
        std::array<uint, 3> nUnlPercent({0, 50, 100});
        std::array<uint, 8> scores(
            {0,
             NegativeUNLVote::nUnlLowWaterMark - 1,
             NegativeUNLVote::nUnlLowWaterMark,
             NegativeUNLVote::nUnlLowWaterMark + 1,
             NegativeUNLVote::nUnlHighWaterMark - 1,
             NegativeUNLVote::nUnlHighWaterMark,
             NegativeUNLVote::nUnlHighWaterMark + 1,
             NegativeUNLVote::nUnlMinLocalValsToVote});

        //== combination 1:
        {
            auto fillScoreTable =
                [&](uint unl_size,
                    uint nUnl_size,
                    uint score,
                    hash_set<NodeID>& UNL,
                    hash_set<NodeID>& nUnl,
                    hash_map<NodeID, unsigned int>& scoreTable) {
                    std::vector<NodeID> nodeIDs;
                    std::vector<PublicKey> UNLKeys;
                    createNodeIDs(unl_size, nodeIDs, UNLKeys);
                    UNL.insert(nodeIDs.begin(), nodeIDs.end());

                    for (auto& n : UNL)
                        scoreTable[n] = score;
                    for (uint i = 0; i < nUnl_size; ++i)
                        nUnl.insert(nodeIDs[i]);
                };

            for (auto us : unlSizes)
            {
                for (auto np : nUnlPercent)
                {
                    for (auto score : scores)
                    {
                        hash_set<NodeID> UNL;
                        hash_set<NodeID> nUnl;
                        hash_map<NodeID, unsigned int> scoreTable;

                        fillScoreTable(
                            us, us * np / 100, score, UNL, nUnl, scoreTable);
                        BEAST_EXPECT(UNL.size() == us);
                        BEAST_EXPECT(nUnl.size() == us * np / 100);
                        BEAST_EXPECT(scoreTable.size() == us);
                        std::vector<NodeID> addCandidates;
                        std::vector<NodeID> removeCandidates;
                        vote.findAllCandidates(
                            UNL,
                            nUnl,
                            scoreTable,
                            addCandidates,
                            removeCandidates);

                        if (np == 0)
                        {
                            if (score < NegativeUNLVote::nUnlLowWaterMark)
                            {
                                BEAST_EXPECT(addCandidates.size() == us);
                            }
                            else
                            {
                                BEAST_EXPECT(addCandidates.size() == 0);
                            }
                            BEAST_EXPECT(removeCandidates.size() == 0);
                        }
                        else if (np == 50)
                        {
                            BEAST_EXPECT(addCandidates.size() == 0);
                            if (score > NegativeUNLVote::nUnlHighWaterMark)
                            {
                                BEAST_EXPECT(
                                    removeCandidates.size() == us * np / 100);
                            }
                            else
                            {
                                BEAST_EXPECT(removeCandidates.size() == 0);
                            }
                        }
                        else
                        {
                            BEAST_EXPECT(addCandidates.size() == 0);
                            if (score > NegativeUNLVote::nUnlHighWaterMark)
                            {
                                BEAST_EXPECT(removeCandidates.size() == us);
                            }
                            else
                            {
                                BEAST_EXPECT(removeCandidates.size() == 0);
                            }
                        }
                    }
                }
            }
        }

        //== combination 2:
        {
            auto fillScoreTable =
                [&](uint unl_size,
                    uint nUnl_percent,
                    hash_set<NodeID>& UNL,
                    hash_set<NodeID>& nUnl,
                    hash_map<NodeID, unsigned int>& scoreTable) {
                    std::vector<NodeID> nodeIDs;
                    std::vector<PublicKey> UNLKeys;
                    createNodeIDs(unl_size, nodeIDs, UNLKeys);
                    UNL.insert(nodeIDs.begin(), nodeIDs.end());

                    uint nIdx = 0;
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
                        nUnl = UNL;
                    }
                    else if (nUnl_percent == 50)
                    {
                        for (uint i = 1; i < unl_size; i += 2)
                            nUnl.insert(nodeIDs[i]);
                    }
                };

            for (auto us : unlSizes)
            {
                for (auto np : nUnlPercent)
                {
                    hash_set<NodeID> UNL;
                    hash_set<NodeID> nUnl;
                    hash_map<NodeID, unsigned int> scoreTable;

                    fillScoreTable(us, np, UNL, nUnl, scoreTable);
                    BEAST_EXPECT(UNL.size() == us);
                    BEAST_EXPECT(nUnl.size() == us * np / 100);
                    BEAST_EXPECT(scoreTable.size() == us);
                    std::vector<NodeID> addCandidates;
                    std::vector<NodeID> removeCandidates;
                    vote.findAllCandidates(
                        UNL, nUnl, scoreTable, addCandidates, removeCandidates);

                    if (np == 0)
                    {
                        BEAST_EXPECT(addCandidates.size() == 4);
                        BEAST_EXPECT(removeCandidates.size() == 0);
                    }
                    else if (np == 50)
                    {
                        BEAST_EXPECT(
                            addCandidates.size() ==
                            0);  // already have maxNegativeListed
                        BEAST_EXPECT(
                            removeCandidates.size() == nUnl.size() - 6);
                    }
                    else
                    {
                        BEAST_EXPECT(addCandidates.size() == 0);
                        BEAST_EXPECT(
                            removeCandidates.size() == nUnl.size() - 12);
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

        // empty, add
        // not empty, add new, add same
        // not empty, purge
        // three, 0, 1, 2, 3 expired

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
        std::array<uint, 4> unlSizes({10, 34, 35, 50});
        std::array<std::array<uint, 3>, 4> scorePattern = {
            {{{0, 0, 0}}, {{50, 50, 50}}, {{100, 100, 100}}, {{0, 50, 100}}}};

        for (uint us = 0; us < 4; ++us)
        {
            for (uint sp = 0; sp < 4; ++sp)
            {
                jtx::Env env(*this);

                RCLValidations& validations = env.app().getValidations();

                std::vector<NodeID> nodeIDs;
                std::vector<PublicKey> UNLKeys;
                createNodeIDs(unlSizes[us], nodeIDs, UNLKeys);
                hash_set<NodeID> UNLNodeIDs(nodeIDs.begin(), nodeIDs.end());

                LedgerHistory history;
                bool goodHistory = createLedgerHistory(
                    history, env, UNLKeys, 0, false, false, 256);
                BEAST_EXPECT(goodHistory);
                if (goodHistory)
                {
                    NodeID myId = nodeIDs[3];  // Note 3
                    uint unlSize = UNLNodeIDs.size();
                    for (auto& l : history)
                    {
                        uint i = 0;  // looping unl
                        auto add_v = [&](uint k) {
                            if ((scorePattern[sp][k] == 50 &&
                                 l->seq() % 2 == 0) ||
                                scorePattern[sp][k] == 100 ||
                                nodeIDs[i] == myId)
                            {
                                RCLValidation v(
                                    createSTVal(env, l, nodeIDs[i]));
                                validations.add(nodeIDs[i], v);
                            }
                        };
                        for (; i < 2; ++i)
                        {
                            add_v(0);
                        }
                        for (; i < 4; ++i)
                        {
                            add_v(1);
                        }
                        for (; i < unlSize; ++i)
                        {
                            add_v(2);
                        }
                    }
                    NegativeUNLVote vote(myId, env.journal);
                    hash_map<NodeID, uint> scoreTable;
                    BEAST_EXPECT(vote.buildScoreTable(
                        history.back(), UNLNodeIDs, validations, scoreTable));
                    uint i = 0;  // looping unl
                    auto checkScores = [&](uint score, uint k) -> bool {
                        if (nodeIDs[i] == myId)
                            return score == 256;
                        if (scorePattern[sp][k] == 0)
                            return score == 0;
                        if (scorePattern[sp][k] == 50)
                            return score == 256 / 2;
                        if (scorePattern[sp][k] == 100)
                            return score == 256;
                        else
                            assert(0);
                        return false;
                    };
                    for (; i < 2; ++i)
                    {
                        BEAST_EXPECT(checkScores(scoreTable[nodeIDs[i]], 0));
                    }
                    for (; i < 4; ++i)
                    {
                        BEAST_EXPECT(checkScores(scoreTable[nodeIDs[i]], 1));
                    }
                    for (; i < unlSize; ++i)
                    {
                        BEAST_EXPECT(checkScores(scoreTable[nodeIDs[i]], 2));
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
 * Voting tests:
 * == use hasToAdd and hasToRemove in some of the cases
 *
 * == all good score, nUnl empty
 * -- txSet.size = 0
 * == all good score, nUnl not empty (use hasToAdd)
 * -- txSet.size = 1
 *
 * == 2 nodes offline, nUnl empty (use hasToRemove)
 * -- txSet.size = 1
 * == 2 nodes offline, in nUnl
 * -- txSet.size = 0
 *
 * == 2 nodes offline, not in nUnl, but maxListed
 * -- txSet.size = 0
 *
 * == 2 nodes offline including me, not in nUnl
 * -- txSet.size = 0
 * == 2 nodes offline, not in nUnl, but I'm not a validator
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
            //== all good score, nUnl empty
            //-- txSet.size = 0
            jtx::Env env(*this, jtx::supported_amendments());
            RCLValidations& validations = env.app().getValidations();
            std::vector<NodeID> nodeIDs;
            std::vector<PublicKey> UNLKeys;
            createNodeIDs(51, nodeIDs, UNLKeys);
            hash_set<PublicKey> keySet(UNLKeys.begin(), UNLKeys.end());

            LedgerHistory history;
            bool goodHistory =
                createLedgerHistory(history, env, UNLKeys, 0, false, false);
            BEAST_EXPECT(goodHistory);
            if (goodHistory)
            {
                for (auto& l : history)
                {
                    for (auto& n : nodeIDs)
                    {
                        RCLValidation v(createSTVal(env, l, n));
                        validations.add(n, v);
                    }
                }
                NegativeUNLVote vote(nodeIDs[0], env.journal);
                auto txSet = std::make_shared<SHAMap>(
                    SHAMapType::TRANSACTION, env.app().family());
                vote.doVoting(history.back(), keySet, validations, txSet);
                BEAST_EXPECT(countTx(txSet) == 0);
            }
        }

        {
            // all good score, nUnl not empty (use hasToAdd)
            //-- txSet.size = 1
            jtx::Env env(*this, jtx::supported_amendments());

            RCLValidations& validations = env.app().getValidations();

            std::vector<NodeID> nodeIDs;
            std::vector<PublicKey> UNLKeys;
            createNodeIDs(37, nodeIDs, UNLKeys);
            hash_set<PublicKey> keySet(UNLKeys.begin(), UNLKeys.end());

            LedgerHistory history;
            bool goodHistory =
                createLedgerHistory(history, env, UNLKeys, 0, true, false);
            BEAST_EXPECT(goodHistory);
            if (goodHistory)
            {
                for (auto& l : history)
                {
                    for (auto& n : nodeIDs)
                    {
                        RCLValidation v(createSTVal(env, l, n));
                        validations.add(n, v);
                    }
                }
                NegativeUNLVote vote(nodeIDs[0], env.journal);
                auto txSet = std::make_shared<SHAMap>(
                    SHAMapType::TRANSACTION, env.app().family());
                vote.doVoting(history.back(), keySet, validations, txSet);
                BEAST_EXPECT(countTx(txSet) == 1);
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
            //== 2 nodes offline, nUnl empty (use hasToRemove)
            //-- txSet.size = 1
            jtx::Env env(*this, jtx::supported_amendments());

            RCLValidations& validations = env.app().getValidations();

            std::vector<NodeID> nodeIDs;
            std::vector<PublicKey> UNLKeys;
            createNodeIDs(29, nodeIDs, UNLKeys);
            hash_set<PublicKey> keySet(UNLKeys.begin(), UNLKeys.end());

            LedgerHistory history;
            bool goodHistory =
                createLedgerHistory(history, env, UNLKeys, 1, false, true);
            BEAST_EXPECT(goodHistory);
            if (goodHistory)
            {
                for (auto& l : history)
                {
                    for (auto& n : nodeIDs)
                    {
                        if (n == nodeIDs[0] || n == nodeIDs[1])
                            continue;
                        RCLValidation v(createSTVal(env, l, n));
                        validations.add(n, v);
                    }
                }
                NegativeUNLVote vote(nodeIDs.back(), env.journal);
                auto txSet = std::make_shared<SHAMap>(
                    SHAMapType::TRANSACTION, env.app().family());
                vote.doVoting(history.back(), keySet, validations, txSet);
                BEAST_EXPECT(countTx(txSet) == 1);
            }
        }

        {
            // 2 nodes offline, in nUnl
            //-- txSet.size = 0
            jtx::Env env(*this, jtx::supported_amendments());

            RCLValidations& validations = env.app().getValidations();

            std::vector<NodeID> nodeIDs;
            std::vector<PublicKey> UNLKeys;
            createNodeIDs(30, nodeIDs, UNLKeys);
            hash_set<PublicKey> keySet(UNLKeys.begin(), UNLKeys.end());

            LedgerHistory history;
            bool goodHistory =
                createLedgerHistory(history, env, UNLKeys, 1, true, false);
            BEAST_EXPECT(goodHistory);
            if (goodHistory)
            {
                NodeID n1 = calcNodeID(*history.back()->nUnl().begin());
                NodeID n2 = calcNodeID(*history.back()->nUnlToDisable());
                for (auto& l : history)
                {
                    for (auto& n : nodeIDs)
                    {
                        if (n == n1 || n == n2)
                            continue;
                        RCLValidation v(createSTVal(env, l, n));
                        validations.add(n, v);
                    }
                }
                NegativeUNLVote vote(nodeIDs.back(), env.journal);
                auto txSet = std::make_shared<SHAMap>(
                    SHAMapType::TRANSACTION, env.app().family());
                vote.doVoting(history.back(), keySet, validations, txSet);
                BEAST_EXPECT(countTx(txSet) == 0);
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
            // 2 nodes offline, not in nUnl, but maxListed
            //-- txSet.size = 0
            jtx::Env env(*this, jtx::supported_amendments());

            RCLValidations& validations = env.app().getValidations();

            std::vector<NodeID> nodeIDs;
            std::vector<PublicKey> UNLKeys;
            createNodeIDs(32, nodeIDs, UNLKeys);
            hash_set<PublicKey> keySet(UNLKeys.begin(), UNLKeys.end());

            LedgerHistory history;
            bool goodHistory =
                createLedgerHistory(history, env, UNLKeys, 8, true, true);
            BEAST_EXPECT(goodHistory);
            if (goodHistory)
            {
                for (auto& l : history)
                {
                    for (uint i = 11; i < 32; ++i)
                    {
                        RCLValidation v(createSTVal(env, l, nodeIDs[i]));
                        validations.add(nodeIDs[i], v);
                    }
                }
                NegativeUNLVote vote(nodeIDs.back(), env.journal);
                auto txSet = std::make_shared<SHAMap>(
                    SHAMapType::TRANSACTION, env.app().family());
                vote.doVoting(history.back(), keySet, validations, txSet);
                BEAST_EXPECT(countTx(txSet) == 0);
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
            //== 2 nodes offline including me, not in nUnl
            //-- txSet.size = 0
            jtx::Env env(*this, jtx::supported_amendments());

            RCLValidations& validations = env.app().getValidations();
            std::vector<NodeID> nodeIDs;
            std::vector<PublicKey> UNLKeys;
            createNodeIDs(35, nodeIDs, UNLKeys);
            hash_set<PublicKey> keySet(UNLKeys.begin(), UNLKeys.end());

            LedgerHistory history;
            bool goodHistory =
                createLedgerHistory(history, env, UNLKeys, 0, false, false);
            BEAST_EXPECT(goodHistory);
            if (goodHistory)
            {
                for (auto& l : history)
                {
                    for (auto& n : nodeIDs)
                    {
                        if (n == nodeIDs[0] || n == nodeIDs[1])
                            continue;
                        RCLValidation v(createSTVal(env, l, n));
                        validations.add(n, v);
                    }
                }
                NegativeUNLVote vote(nodeIDs[0], env.journal);
                auto txSet = std::make_shared<SHAMap>(
                    SHAMapType::TRANSACTION, env.app().family());
                vote.doVoting(history.back(), keySet, validations, txSet);
                BEAST_EXPECT(countTx(txSet) == 0);
            }
        }

        {
            // 2 nodes offline, not in nUnl, but I'm not a validator
            //-- txSet.size = 0
            jtx::Env env(*this, jtx::supported_amendments());

            RCLValidations& validations = env.app().getValidations();
            std::vector<NodeID> nodeIDs;
            std::vector<PublicKey> UNLKeys;
            createNodeIDs(40, nodeIDs, UNLKeys);
            hash_set<PublicKey> keySet(UNLKeys.begin(), UNLKeys.end());

            LedgerHistory history;
            bool goodHistory =
                createLedgerHistory(history, env, UNLKeys, 0, false, false);
            BEAST_EXPECT(goodHistory);
            if (goodHistory)
            {
                for (auto& l : history)
                {
                    for (auto& n : nodeIDs)
                    {
                        if (n == nodeIDs[0] || n == nodeIDs[1])
                            continue;
                        RCLValidation v(createSTVal(env, l, n));
                        validations.add(n, v);
                    }
                }
                NegativeUNLVote vote(NodeID(0xdeadbeef), env.journal);
                auto txSet = std::make_shared<SHAMap>(
                    SHAMapType::TRANSACTION, env.app().family());
                vote.doVoting(history.back(), keySet, validations, txSet);
                BEAST_EXPECT(countTx(txSet) == 0);
            }
        }

        {
            //== 2 in nUnl, but not in unl, no other remove candidates
            //-- txSet.size = 1
            jtx::Env env(*this, jtx::supported_amendments());

            RCLValidations& validations = env.app().getValidations();
            std::vector<NodeID> nodeIDs;
            std::vector<PublicKey> UNLKeys;
            createNodeIDs(25, nodeIDs, UNLKeys);
            hash_set<PublicKey> keySet(UNLKeys.begin(), UNLKeys.end());

            LedgerHistory history;
            bool goodHistory =
                createLedgerHistory(history, env, UNLKeys, 2, false, false);
            BEAST_EXPECT(goodHistory);
            if (goodHistory)
            {
                for (auto& l : history)
                {
                    for (auto& n : nodeIDs)
                    {
                        if (n == nodeIDs[0] || n == nodeIDs[1])
                            continue;
                        RCLValidation v(createSTVal(env, l, n));
                        validations.add(n, v);
                    }
                }
                NegativeUNLVote vote(nodeIDs.back(), env.journal);
                keySet.erase(UNLKeys[0]);
                keySet.erase(UNLKeys[1]);
                auto txSet = std::make_shared<SHAMap>(
                    SHAMapType::TRANSACTION, env.app().family());
                vote.doVoting(history.back(), keySet, validations, txSet);
                BEAST_EXPECT(countTx(txSet) == 1);
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
            jtx::Env env(*this, jtx::supported_amendments());

            RCLValidations& validations = env.app().getValidations();
            std::vector<NodeID> nodeIDs;
            std::vector<PublicKey> UNLKeys;
            createNodeIDs(15, nodeIDs, UNLKeys);
            hash_set<PublicKey> keySet(UNLKeys.begin(), UNLKeys.end());

            LedgerHistory history;
            bool goodHistory =
                createLedgerHistory(history, env, UNLKeys, 0, false, false);
            BEAST_EXPECT(goodHistory);
            if (goodHistory)
            {
                for (auto& l : history)
                {
                    for (auto& n : nodeIDs)
                    {
                        RCLValidation v(createSTVal(env, l, n));
                        validations.add(n, v);
                    }
                }
                NegativeUNLVote vote(nodeIDs[0], env.journal);
                auto extra_key_1 = randomKeyPair(KeyType::ed25519).first;
                auto extra_key_2 = randomKeyPair(KeyType::ed25519).first;
                keySet.insert(extra_key_1);
                keySet.insert(extra_key_2);
                hash_set<NodeID> nowTrusted;
                nowTrusted.insert(calcNodeID(extra_key_1));
                nowTrusted.insert(calcNodeID(extra_key_2));
                vote.newValidators(history.back()->seq(), nowTrusted);
                auto txSet = std::make_shared<SHAMap>(
                    SHAMapType::TRANSACTION, env.app().family());
                vote.doVoting(history.back(), keySet, validations, txSet);
                BEAST_EXPECT(countTx(txSet) == 0);
            }
        }

        {
            //== 2 expired new validators have bad scores
            //-- txSet.size = 1
            jtx::Env env(*this, jtx::supported_amendments());

            RCLValidations& validations = env.app().getValidations();
            std::vector<NodeID> nodeIDs;
            std::vector<PublicKey> UNLKeys;
            createNodeIDs(21, nodeIDs, UNLKeys);
            hash_set<PublicKey> keySet(UNLKeys.begin(), UNLKeys.end());

            LedgerHistory history;
            bool goodHistory = createLedgerHistory(
                history,
                env,
                UNLKeys,
                0,
                false,
                false,
                NegativeUNLVote::newValidatorDisableSkip * 2);
            BEAST_EXPECT(goodHistory);
            if (goodHistory)
            {
                for (auto& l : history)
                {
                    for (auto& n : nodeIDs)
                    {
                        RCLValidation v(createSTVal(env, l, n));
                        validations.add(n, v);
                    }
                }
                NegativeUNLVote vote(nodeIDs[0], env.journal);
                auto extra_key_1 = randomKeyPair(KeyType::ed25519).first;
                auto extra_key_2 = randomKeyPair(KeyType::ed25519).first;
                keySet.insert(extra_key_1);
                keySet.insert(extra_key_2);
                hash_set<NodeID> nowTrusted;
                nowTrusted.insert(calcNodeID(extra_key_1));
                nowTrusted.insert(calcNodeID(extra_key_2));
                vote.newValidators(256, nowTrusted);
                auto txSet = std::make_shared<SHAMap>(
                    SHAMapType::TRANSACTION, env.app().family());
                vote.doVoting(history.back(), keySet, validations, txSet);
                BEAST_EXPECT(countTx(txSet) == 1);
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
        jtx::Env env(*this, jtx::supported_amendments());

        RCLValidations& validations = env.app().getValidations();
        std::vector<NodeID> nodeIDs;
        std::vector<PublicKey> UNLKeys;
        createNodeIDs(10, nodeIDs, UNLKeys);

        LedgerHistory history;
        bool goodHistory =
            createLedgerHistory(history, env, UNLKeys, 1, false, false);
        BEAST_EXPECT(goodHistory);
        if (goodHistory)
        {
            for (auto& l : history)
            {
                for (auto& n : nodeIDs)
                {
                    RCLValidation v(createSTVal(env, l, n));
                    v.setTrusted();
                    validations.add(n, v);
                }
            }
            auto l = history.back();
            auto nUnlKeys = l->nUnl();
            auto vals = validations.getTrustedForLedger(l->info().hash);
            BEAST_EXPECT(vals.size() == 10);
            hash_set<NodeID> nUnl;
            for (auto& k : nUnlKeys)
                nUnl.insert(calcNodeID(k));
            filterValsWithnUnl(vals, nUnl);
            BEAST_EXPECT(vals.size() == 10 - 1);
        }
    }

    void
    run() override
    {
        testFilterValidations();
    }
};

class NegativeUNLVoteFilterValidationsLongList_test
    : public beast::unit_test::suite
{
    void
    testFilterValidations()
    {
        testcase("Filter Validations");
        jtx::Env env(*this, jtx::supported_amendments());

        RCLValidations& validations = env.app().getValidations();
        std::vector<NodeID> nodeIDs;
        std::vector<PublicKey> UNLKeys;
        createNodeIDs(30, nodeIDs, UNLKeys);

        LedgerHistory history;
        bool goodHistory =
            createLedgerHistory(history, env, UNLKeys, 3, false, false);
        BEAST_EXPECT(goodHistory);
        if (goodHistory)
        {
            for (auto& l : history)
            {
                for (auto& n : nodeIDs)
                {
                    RCLValidation v(createSTVal(env, l, n));
                    v.setTrusted();
                    validations.add(n, v);
                }
            }
            auto l = history.back();
            auto nUnlKeys = l->nUnl();
            auto vals = validations.getTrustedForLedger(l->info().hash);
            BEAST_EXPECT(vals.size() == 30);
            hash_set<NodeID> nUnl;
            for (auto& k : nUnlKeys)
                nUnl.insert(calcNodeID(k));
            filterValsWithnUnl(vals, nUnl);
            BEAST_EXPECT(vals.size() == 30 - 3);
        }
    }

    void
    run() override
    {
        testFilterValidations();
    }
};

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
BEAST_DEFINE_TESTSUITE_MANUAL(
    NegativeUNLVoteFilterValidationsLongList,
    consensus,
    ripple);

}  // namespace test
}  // namespace ripple
