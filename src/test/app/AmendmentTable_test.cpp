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

#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/chrono.h>
#include <ripple/beast/unit_test.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/jss.h>
#include <test/jtx/Env.h>
#include <test/unit_test/SuiteJournal.h>

namespace ripple {

class AmendmentTable_test final : public beast::unit_test::suite
{
private:
    static uint256
    amendmentId(std::string in)
    {
        sha256_hasher h;
        using beast::hash_append;
        hash_append(h, in);
        auto const d = static_cast<sha256_hasher::result_type>(h);
        uint256 result;
        std::memcpy(result.data(), d.data(), d.size());
        return result;
    }

    static Section
    makeSection(
        std::string const& name,
        std::vector<std::string> const& amendments)
    {
        Section section(name);
        for (auto const& a : amendments)
            section.append(to_string(amendmentId(a)) + " " + a);
        return section;
    }

    static Section
    makeSection(std::vector<std::string> const& amendments)
    {
        return makeSection("Test", amendments);
    }

    static Section
    makeSection(uint256 const& amendment)
    {
        Section section("Test");
        section.append(to_string(amendment) + " " + to_string(amendment));
        return section;
    }

    std::unique_ptr<Config>
    makeConfig()
    {
        auto cfg = test::jtx::envconfig();
        cfg->section(SECTION_AMENDMENTS) =
            makeSection(SECTION_AMENDMENTS, enabled_);
        cfg->section(SECTION_VETO_AMENDMENTS) =
            makeSection(SECTION_VETO_AMENDMENTS, vetoed_);
        return cfg;
    }

    // All useful amendments are supported amendments.
    // Enabled amendments are typically a subset of supported amendments.
    // Vetoed amendments should be supported but not enabled.
    // Unsupported amendments may be added to the AmendmentTable.
    std::vector<std::string> const supported_{
        "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k",
        "l", "m", "n", "o", "p", "q", "r", "s", "t", "u"};
    std::vector<std::string> const
        enabled_{"b", "d", "f", "h", "j", "l", "n", "p"};
    std::vector<std::string> const vetoed_{"a", "c", "e"};
    std::vector<std::string> const unsupported_{"v", "w", "x"};
    std::vector<std::string> const unsupportedMajority_{"y", "z"};

    Section const emptySection;

    test::SuiteJournal journal;

public:
    AmendmentTable_test() : journal("AmendmentTable_test", *this)
    {
    }

    std::unique_ptr<AmendmentTable>
    makeTable(
        Application& app,
        std::chrono::seconds majorityTime,
        Section const supported,
        Section const enabled,
        Section const vetoed)
    {
        auto t = make_AmendmentTable(
            app, majorityTime, supported, enabled, vetoed, journal);
        return t;
    }

    std::unique_ptr<AmendmentTable>
    makeTable(
        test::jtx::Env& env,
        std::chrono::seconds majorityTime,
        Section const supported,
        Section const enabled,
        Section const vetoed)
    {
        return make_AmendmentTable(
            env.app(), majorityTime, supported, enabled, vetoed, journal);
    }

    std::unique_ptr<AmendmentTable>
    makeTable(test::jtx::Env& env, std::chrono::seconds majorityTime)
    {
        return makeTable(
            env.app(),
            majorityTime,
            makeSection(supported_),
            makeSection(enabled_),
            makeSection(vetoed_));
    }

    void
    testConstruct()
    {
        testcase("Construction");
        test::jtx::Env env{*this, makeConfig()};
        auto table = makeTable(env, weeks(1));

        for (auto const& a : supported_)
        {
            BEAST_EXPECT(table->isSupported(amendmentId(a)));
        }

        for (auto const& a : enabled_)
        {
            BEAST_EXPECT(table->isSupported(amendmentId(a)));
        }

        for (auto const& a : vetoed_)
        {
            BEAST_EXPECT(table->isSupported(amendmentId(a)));
            BEAST_EXPECT(!table->isEnabled(amendmentId(a)));
        }
    }

    void
    testGet()
    {
        testcase("Name to ID mapping");

        test::jtx::Env env{*this, makeConfig()};
        auto table = makeTable(env, weeks(1));

        for (auto const& a : supported_)
            BEAST_EXPECT(table->find(a) == amendmentId(a));
        for (auto const& a : enabled_)
            BEAST_EXPECT(table->find(a) == amendmentId(a));

        for (auto const& a : vetoed_)
            BEAST_EXPECT(table->find(a) == amendmentId(a));
        for (auto const& a : unsupported_)
            BEAST_EXPECT(!table->find(a));
        for (auto const& a : unsupportedMajority_)
            BEAST_EXPECT(!table->find(a));

        // Vetoing an unsupported amendment should add the amendment to table.
        // Verify that unsupportedID is not in table.
        uint256 const unsupportedID = amendmentId(unsupported_[0]);
        {
            Json::Value const unsupp =
                table->getJson(unsupportedID)[to_string(unsupportedID)];
            BEAST_EXPECT(unsupp.size() == 0);
        }

        // After vetoing unsupportedID verify that it is in table.
        table->veto(unsupportedID);
        {
            Json::Value const unsupp =
                table->getJson(unsupportedID)[to_string(unsupportedID)];
            BEAST_EXPECT(unsupp[jss::vetoed].asBool());
        }
    }

    void
    testBadConfig()
    {
        auto const section = makeSection(supported_);
        auto const id = to_string(amendmentId(enabled_[0]));

        testcase("Bad Config");

        {  // Two arguments are required - we pass one
            Section test = section;
            test.append(id);

            try
            {
                test::jtx::Env env{*this, makeConfig()};
                if (makeTable(env, weeks(2), test, emptySection, emptySection))
                    fail("Accepted only amendment ID");
            }
            catch (...)
            {
                pass();
            }
        }

        {  // Two arguments are required - we pass three
            Section test = section;
            test.append(id + " Test Name");

            try
            {
                test::jtx::Env env{*this, makeConfig()};
                if (makeTable(env, weeks(2), test, emptySection, emptySection))
                    fail("Accepted extra arguments");
            }
            catch (...)
            {
                pass();
            }
        }

        {
            auto sid = id;
            sid.resize(sid.length() - 1);

            Section test = section;
            test.append(sid + " Name");

            try
            {
                test::jtx::Env env{*this, makeConfig()};
                if (makeTable(env, weeks(2), test, emptySection, emptySection))
                    fail("Accepted short amendment ID");
            }
            catch (...)
            {
                pass();
            }
        }

        {
            auto sid = id;
            sid.resize(sid.length() + 1, '0');

            Section test = section;
            test.append(sid + " Name");

            try
            {
                test::jtx::Env env{*this, makeConfig()};
                if (makeTable(env, weeks(2), test, emptySection, emptySection))
                    fail("Accepted long amendment ID");
            }
            catch (...)
            {
                pass();
            }
        }

        {
            auto sid = id;
            sid.resize(sid.length() - 1);
            sid.push_back('Q');

            Section test = section;
            test.append(sid + " Name");

            try
            {
                test::jtx::Env env{*this, makeConfig()};
                if (makeTable(env, weeks(2), test, emptySection, emptySection))
                    fail("Accepted non-hex amendment ID");
            }
            catch (...)
            {
                pass();
            }
        }
    }

    void
    testEnableVeto()
    {
        testcase("enable and veto");

        test::jtx::Env env{*this, makeConfig()};
        std::unique_ptr<AmendmentTable> table = makeTable(env, weeks(2));

        // Note which entries are pre-enabled.
        std::set<uint256> allEnabled;
        for (std::string const& a : enabled_)
            allEnabled.insert(amendmentId(a));

        // Subset of amendments to late-enable
        std::set<uint256> lateEnabled;
        lateEnabled.insert(amendmentId(supported_[0]));
        lateEnabled.insert(amendmentId(enabled_[0]));
        lateEnabled.insert(amendmentId(vetoed_[0]));

        // Do the late enabling.
        for (uint256 const& a : lateEnabled)
            table->enable(a);

        // So far all enabled amendments are supported.
        BEAST_EXPECT(!table->hasUnsupportedEnabled());

        // Verify all pre- and late-enables are enabled and nothing else.
        //         allEnabled.insert(lateEnabled.begin(), lateEnabled.end());
        //         for (std::string const& a : supported_)
        //         {
        //             uint256 const supportedID = amendmentId(a);
        //             BEAST_EXPECT(
        //                 table->isEnabled(supportedID) ==
        //                 (allEnabled.find(supportedID) != allEnabled.end()));
        //         }

        // All supported and unVetoed amendments should be returned as desired.
        {
            std::set<uint256> vetoed;
            for (std::string const& a : vetoed_)
                vetoed.insert(amendmentId(a));

            std::vector<uint256> const desired = table->getDesired();
            for (uint256 const& a : desired)
                BEAST_EXPECT(vetoed.count(a) == 0);

            // Unveto an amendment that is already not vetoed.  Shouldn't
            // hurt anything, but the values returned by getDesired()
            // shouldn't change.
            table->unVeto(amendmentId(supported_[1]));
            BEAST_EXPECT(desired == table->getDesired());
        }

        // UnVeto one of the vetoed amendments.  It should now be desired.
        {
            uint256 const unvetoedID = amendmentId(vetoed_[0]);
            table->unVeto(unvetoedID);

            std::vector<uint256> const desired = table->getDesired();
            BEAST_EXPECT(
                std::find(desired.begin(), desired.end(), unvetoedID) !=
                desired.end());
        }

        // Veto all supported amendments.  Now desired should be empty.
        for (std::string const& a : supported_)
        {
            table->veto(amendmentId(a));
        }
        BEAST_EXPECT(table->getDesired().empty());

        // Enable an unsupported amendment.
        {
            BEAST_EXPECT(!table->hasUnsupportedEnabled());
            table->enable(amendmentId(unsupported_[0]));
            BEAST_EXPECT(table->hasUnsupportedEnabled());
        }
    }

    std::vector<std::pair<PublicKey, SecretKey>>
    makeValidators(int num)
    {
        std::vector<std::pair<PublicKey, SecretKey>> ret;
        ret.reserve(num);
        for (int i = 0; i < num; ++i)
        {
            ret.emplace_back(randomKeyPair(KeyType::secp256k1));
        }
        return ret;
    }

    static NetClock::time_point
    weekTime(weeks w)
    {
        return NetClock::time_point{w};
    }

    // Execute a pretend consensus round for a flag ledger
    void
    doRound(
        uint256 const& feat,
        AmendmentTable& table,
        weeks week,
        std::vector<std::pair<PublicKey, SecretKey>> const& validators,
        std::vector<std::pair<uint256, int>> const& votes,
        std::vector<uint256>& ourVotes,
        std::set<uint256>& enabled,
        majorityAmendments_t& majority)
    {
        // Do a round at the specified time
        // Returns the amendments we voted for

        // Parameters:
        // table:      Our table of known and vetoed amendments
        // validators: The addreses of validators we trust
        // votes:      Amendments and the number of validators who vote for them
        // ourVotes:   The amendments we vote for in our validation
        // enabled:    In/out enabled amendments
        // majority:   In/our majority amendments (and when they got a majority)

        auto const roundTime = weekTime(week);

        // Build validations
        std::vector<std::shared_ptr<STValidation>> validations;
        validations.reserve(validators.size());

        int i = 0;
        for (auto const& [pub, sec] : validators)
        {
            ++i;
            std::vector<uint256> field;

            for (auto const& [hash, nVotes] : votes)
            {
                if (feat == fixAmendmentMajorityCalc ? nVotes >= i : nVotes > i)
                {
                    // We vote yes on this amendment
                    field.push_back(hash);
                }
            }

            auto v = std::make_shared<STValidation>(
                ripple::NetClock::time_point{},
                pub,
                sec,
                calcNodeID(pub),
                [&field](STValidation& v) {
                    if (!field.empty())
                        v.setFieldV256(
                            sfAmendments, STVector256(sfAmendments, field));
                    v.setFieldU32(sfLedgerSequence, 6180339);
                });

            validations.emplace_back(v);
        }

        ourVotes = table.doValidation(enabled);

        auto actions = table.doVoting(
            Rules({feat}), roundTime, enabled, majority, validations);
        for (auto const& [hash, action] : actions)
        {
            // This code assumes other validators do as we do

            switch (action)
            {
                case 0:
                    // amendment goes from majority to enabled
                    if (enabled.find(hash) != enabled.end())
                        Throw<std::runtime_error>("enabling already enabled");
                    if (majority.find(hash) == majority.end())
                        Throw<std::runtime_error>("enabling without majority");
                    enabled.insert(hash);
                    majority.erase(hash);
                    break;

                case tfGotMajority:
                    if (majority.find(hash) != majority.end())
                        Throw<std::runtime_error>(
                            "got majority while having majority");
                    majority[hash] = roundTime;
                    break;

                case tfLostMajority:
                    if (majority.find(hash) == majority.end())
                        Throw<std::runtime_error>(
                            "lost majority without majority");
                    majority.erase(hash);
                    break;

                default:
                    Throw<std::runtime_error>("unknown action");
            }
        }
    }

    // No vote on unknown amendment
    void
    testNoOnUnknown(uint256 const& feat)
    {
        testcase("Vote NO on unknown");

        auto const testAmendment = amendmentId("TestAmendment");
        auto const validators = makeValidators(10);

        test::jtx::Env env{*this};
        auto table =
            makeTable(env, weeks(2), emptySection, emptySection, emptySection);

        std::vector<std::pair<uint256, int>> votes;
        std::vector<uint256> ourVotes;
        std::set<uint256> enabled;
        majorityAmendments_t majority;

        doRound(
            feat,
            *table,
            weeks{1},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(ourVotes.empty());
        BEAST_EXPECT(enabled.empty());
        BEAST_EXPECT(majority.empty());

        votes.emplace_back(testAmendment, validators.size());

        doRound(
            feat,
            *table,
            weeks{2},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(ourVotes.empty());
        BEAST_EXPECT(enabled.empty());

        majority[testAmendment] = weekTime(weeks{1});

        // Note that the simulation code assumes others behave as we do,
        // so the amendment won't get enabled
        doRound(
            feat,
            *table,
            weeks{5},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(ourVotes.empty());
        BEAST_EXPECT(enabled.empty());
    }

    // No vote on vetoed amendment
    void
    testNoOnVetoed(uint256 const& feat)
    {
        testcase("Vote NO on vetoed");

        auto const testAmendment = amendmentId("vetoedAmendment");

        test::jtx::Env env{*this};
        auto table = makeTable(
            env,
            weeks(2),
            emptySection,
            emptySection,
            makeSection(testAmendment));

        auto const validators = makeValidators(10);

        std::vector<std::pair<uint256, int>> votes;
        std::vector<uint256> ourVotes;
        std::set<uint256> enabled;
        majorityAmendments_t majority;

        doRound(
            feat,
            *table,
            weeks{1},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(ourVotes.empty());
        BEAST_EXPECT(enabled.empty());
        BEAST_EXPECT(majority.empty());

        votes.emplace_back(testAmendment, validators.size());

        doRound(
            feat,
            *table,
            weeks{2},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(ourVotes.empty());
        BEAST_EXPECT(enabled.empty());

        majority[testAmendment] = weekTime(weeks{1});

        doRound(
            feat,
            *table,
            weeks{5},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(ourVotes.empty());
        BEAST_EXPECT(enabled.empty());
    }

    // Vote on and enable known, not-enabled amendment
    void
    testVoteEnable(uint256 const& feat)
    {
        testcase("voteEnable");

        test::jtx::Env env{*this};
        auto table = makeTable(
            env, weeks(2), makeSection(supported_), emptySection, emptySection);

        auto const validators = makeValidators(10);
        std::vector<std::pair<uint256, int>> votes;
        std::vector<uint256> ourVotes;
        std::set<uint256> enabled;
        majorityAmendments_t majority;

        // Week 1: We should vote for all known amendments not enabled
        doRound(
            feat,
            *table,
            weeks{1},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(ourVotes.size() == supported_.size());
        BEAST_EXPECT(enabled.empty());
        for (auto const& i : supported_)
            BEAST_EXPECT(majority.find(amendmentId(i)) == majority.end());

        // Now, everyone votes for this feature
        for (auto const& i : supported_)
            votes.emplace_back(amendmentId(i), validators.size());

        // Week 2: We should recognize a majority
        doRound(
            feat,
            *table,
            weeks{2},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(ourVotes.size() == supported_.size());
        BEAST_EXPECT(enabled.empty());

        for (auto const& i : supported_)
            BEAST_EXPECT(majority[amendmentId(i)] == weekTime(weeks{2}));

        // Week 5: We should enable the amendment
        doRound(
            feat,
            *table,
            weeks{5},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(enabled.size() == supported_.size());

        // Week 6: We should remove it from our votes and from having a majority
        doRound(
            feat,
            *table,
            weeks{6},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(enabled.size() == supported_.size());
        BEAST_EXPECT(ourVotes.empty());
        for (auto const& i : supported_)
            BEAST_EXPECT(majority.find(amendmentId(i)) == majority.end());
    }

    // Detect majority at 80%, enable later
    void
    testDetectMajority(uint256 const& feat)
    {
        testcase("detectMajority");

        auto const testAmendment = amendmentId("detectMajority");
        test::jtx::Env env{*this};
        auto table = makeTable(
            env,
            weeks(2),
            makeSection(testAmendment),
            emptySection,
            emptySection);

        auto const validators = makeValidators(16);

        std::set<uint256> enabled;
        majorityAmendments_t majority;

        for (int i = 0; i <= 17; ++i)
        {
            std::vector<std::pair<uint256, int>> votes;
            std::vector<uint256> ourVotes;

            if ((i > 0) && (i < 17))
                votes.emplace_back(testAmendment, i);

            doRound(
                feat,
                *table,
                weeks{i},
                validators,
                votes,
                ourVotes,
                enabled,
                majority);

            if (i < 13)  // 13 => 13/16 = 0.8125 => > 80%
            {
                // We are voting yes, not enabled, no majority
                BEAST_EXPECT(!ourVotes.empty());
                BEAST_EXPECT(enabled.empty());
                BEAST_EXPECT(majority.empty());
            }
            else if (i < 15)
            {
                // We have a majority, not enabled, keep voting
                BEAST_EXPECT(!ourVotes.empty());
                BEAST_EXPECT(!majority.empty());
                BEAST_EXPECT(enabled.empty());
            }
            else if (i == 15)
            {
                // enable, keep voting, remove from majority
                BEAST_EXPECT(!ourVotes.empty());
                BEAST_EXPECT(majority.empty());
                BEAST_EXPECT(!enabled.empty());
            }
            else
            {
                // Done, we should be enabled and not voting
                BEAST_EXPECT(ourVotes.empty());
                BEAST_EXPECT(majority.empty());
                BEAST_EXPECT(!enabled.empty());
            }
        }
    }

    // Detect loss of majority
    void
    testLostMajority(uint256 const& feat)
    {
        testcase("lostMajority");

        auto const testAmendment = amendmentId("lostMajority");
        auto const validators = makeValidators(16);

        test::jtx::Env env{*this};
        auto table = makeTable(
            env,
            weeks(8),
            makeSection(testAmendment),
            emptySection,
            emptySection);

        std::set<uint256> enabled;
        majorityAmendments_t majority;

        {
            // establish majority
            std::vector<std::pair<uint256, int>> votes;
            std::vector<uint256> ourVotes;

            votes.emplace_back(testAmendment, validators.size());

            doRound(
                feat,
                *table,
                weeks{1},
                validators,
                votes,
                ourVotes,
                enabled,
                majority);

            BEAST_EXPECT(enabled.empty());
            BEAST_EXPECT(!majority.empty());
        }

        for (int i = 1; i < 8; ++i)
        {
            std::vector<std::pair<uint256, int>> votes;
            std::vector<uint256> ourVotes;

            // Gradually reduce support
            votes.emplace_back(testAmendment, validators.size() - i);

            doRound(
                feat,
                *table,
                weeks{i + 1},
                validators,
                votes,
                ourVotes,
                enabled,
                majority);

            if (i < 4)  // 16 - 3 = 13 => 13/16 = 0.8125 => > 80%
            {           // 16 - 4 = 12 => 12/16 = 0.75 => < 80%
                // We are voting yes, not enabled, majority
                BEAST_EXPECT(!ourVotes.empty());
                BEAST_EXPECT(enabled.empty());
                BEAST_EXPECT(!majority.empty());
            }
            else
            {
                // No majority, not enabled, keep voting
                BEAST_EXPECT(!ourVotes.empty());
                BEAST_EXPECT(majority.empty());
                BEAST_EXPECT(enabled.empty());
            }
        }
    }

    void
    testHasUnsupported()
    {
        testcase("hasUnsupportedEnabled");

        using namespace std::chrono_literals;
        weeks constexpr w(1);
        test::jtx::Env env{*this, makeConfig()};
        auto table = makeTable(env, w);
        BEAST_EXPECT(!table->hasUnsupportedEnabled());
        BEAST_EXPECT(!table->firstUnsupportedExpected());
        BEAST_EXPECT(table->needValidatedLedger(1));

        std::set<uint256> enabled;
        std::for_each(
            unsupported_.begin(),
            unsupported_.end(),
            [&enabled](auto const& s) { enabled.insert(amendmentId(s)); });

        majorityAmendments_t majority;
        table->doValidatedLedger(1, enabled, majority);
        BEAST_EXPECT(table->hasUnsupportedEnabled());
        BEAST_EXPECT(!table->firstUnsupportedExpected());

        NetClock::duration t{1000s};
        std::for_each(
            unsupportedMajority_.begin(),
            unsupportedMajority_.end(),
            [&majority, &t](auto const& s) {
                majority[amendmentId(s)] = NetClock::time_point{--t};
            });

        table->doValidatedLedger(1, enabled, majority);
        BEAST_EXPECT(table->hasUnsupportedEnabled());
        BEAST_EXPECT(
            table->firstUnsupportedExpected() &&
            *table->firstUnsupportedExpected() == NetClock::time_point{t} + w);

        // Make sure the table knows when it needs an update.
        BEAST_EXPECT(!table->needValidatedLedger(256));
        BEAST_EXPECT(table->needValidatedLedger(257));
    }

    void
    testFeature(uint256 const& feat)
    {
        testNoOnUnknown(feat);
        testNoOnVetoed(feat);
        testVoteEnable(feat);
        testDetectMajority(feat);
        testLostMajority(feat);
    }

    void
    run() override
    {
        testConstruct();
        testGet();
        testBadConfig();
        testEnableVeto();
        testHasUnsupported();
        testFeature({});
        testFeature(fixAmendmentMajorityCalc);
    }
};

BEAST_DEFINE_TESTSUITE(AmendmentTable, app, ripple);

}  // namespace ripple
