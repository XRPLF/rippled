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

    static std::vector<AmendmentTable::FeatureInfo>
    makeFeatureInfo(
        std::vector<std::string> const& amendments,
        VoteBehavior voteBehavior)
    {
        std::vector<AmendmentTable::FeatureInfo> result;
        result.reserve(amendments.size());
        for (auto const& a : amendments)
        {
            result.emplace_back(a, amendmentId(a), voteBehavior);
        }
        return result;
    }

    static std::vector<AmendmentTable::FeatureInfo>
    makeDefaultYes(std::vector<std::string> const& amendments)
    {
        return makeFeatureInfo(amendments, VoteBehavior::DefaultYes);
    }

    static std::vector<AmendmentTable::FeatureInfo>
    makeDefaultYes(uint256 const amendment)
    {
        std::vector<AmendmentTable::FeatureInfo> result{
            {to_string(amendment), amendment, VoteBehavior::DefaultYes}};
        return result;
    }

    static std::vector<AmendmentTable::FeatureInfo>
    makeDefaultNo(std::vector<std::string> const& amendments)
    {
        return makeFeatureInfo(amendments, VoteBehavior::DefaultNo);
    }

    static std::vector<AmendmentTable::FeatureInfo>
    makeObsolete(std::vector<std::string> const& amendments)
    {
        return makeFeatureInfo(amendments, VoteBehavior::Obsolete);
    }

    template <class Arg, class... Args>
    static size_t
    totalsize(std::vector<Arg> const& src, Args const&... args)
    {
        if constexpr (sizeof...(args) > 0)
            return src.size() + totalsize(args...);
        return src.size();
    }

    template <class Arg, class... Args>
    static void
    combine_arg(
        std::vector<Arg>& dest,
        std::vector<Arg> const& src,
        Args const&... args)
    {
        assert(dest.capacity() >= dest.size() + src.size());
        std::copy(src.begin(), src.end(), std::back_inserter(dest));
        if constexpr (sizeof...(args) > 0)
            combine_arg(dest, args...);
    }

    template <class Arg, class... Args>
    static std::vector<Arg>
    combine(
        // Pass "left" by value. The values will need to be copied one way or
        // another, so just reuse it.
        std::vector<Arg> left,
        std::vector<Arg> const& right,
        Args const&... args)
    {
        left.reserve(totalsize(left, right, args...));

        combine_arg(left, right, args...);

        return left;
    }

    // All useful amendments are supported amendments.
    // Enabled amendments are typically a subset of supported amendments.
    // Vetoed amendments should be supported but not enabled.
    // Unsupported amendments may be added to the AmendmentTable.
    std::vector<std::string> const
        yes_{"g", "i", "k", "m", "o", "q", "r", "s", "t", "u"};
    std::vector<std::string> const
        enabled_{"b", "d", "f", "h", "j", "l", "n", "p"};
    std::vector<std::string> const vetoed_{"a", "c", "e"};
    std::vector<std::string> const obsolete_{"0", "1", "2"};
    std::vector<std::string> const allSupported_{
        combine(yes_, enabled_, vetoed_, obsolete_)};
    std::vector<std::string> const unsupported_{"v", "w", "x"};
    std::vector<std::string> const unsupportedMajority_{"y", "z"};

    Section const emptySection_;
    std::vector<AmendmentTable::FeatureInfo> const emptyYes_;

    test::SuiteJournal journal_;

public:
    AmendmentTable_test() : journal_("AmendmentTable_test", *this)
    {
    }

    std::unique_ptr<AmendmentTable>
    makeTable(
        Application& app,
        std::chrono::seconds majorityTime,
        std::vector<AmendmentTable::FeatureInfo> const& supported,
        Section const& enabled,
        Section const& vetoed)
    {
        return make_AmendmentTable(
            app, majorityTime, supported, enabled, vetoed, journal_);
    }

    std::unique_ptr<AmendmentTable>
    makeTable(
        test::jtx::Env& env,
        std::chrono::seconds majorityTime,
        std::vector<AmendmentTable::FeatureInfo> const& supported,
        Section const& enabled,
        Section const& vetoed)
    {
        return makeTable(env.app(), majorityTime, supported, enabled, vetoed);
    }

    std::unique_ptr<AmendmentTable>
    makeTable(test::jtx::Env& env, std::chrono::seconds majorityTime)
    {
        static std::vector<AmendmentTable::FeatureInfo> const supported =
            combine(
                makeDefaultYes(yes_),
                // Use non-intuitive default votes for "enabled_" and "vetoed_"
                // so that when the tests later explicitly enable or veto them,
                // we can be certain that they are not simply going by their
                // default vote setting.
                makeDefaultNo(enabled_),
                makeDefaultYes(vetoed_),
                makeObsolete(obsolete_));
        return makeTable(
            env.app(),
            majorityTime,
            supported,
            makeSection(enabled_),
            makeSection(vetoed_));
    }

    void
    testConstruct()
    {
        testcase("Construction");
        test::jtx::Env env{*this, makeConfig()};
        auto table = makeTable(env, weeks(1));

        for (auto const& a : allSupported_)
            BEAST_EXPECT(table->isSupported(amendmentId(a)));

        for (auto const& a : yes_)
            BEAST_EXPECT(table->isSupported(amendmentId(a)));

        for (auto const& a : enabled_)
            BEAST_EXPECT(table->isSupported(amendmentId(a)));

        for (auto const& a : vetoed_)
        {
            BEAST_EXPECT(table->isSupported(amendmentId(a)));
            BEAST_EXPECT(!table->isEnabled(amendmentId(a)));
        }

        for (auto const& a : obsolete_)
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

        for (auto const& a : yes_)
            BEAST_EXPECT(table->find(a) == amendmentId(a));
        for (auto const& a : enabled_)
            BEAST_EXPECT(table->find(a) == amendmentId(a));
        for (auto const& a : vetoed_)
            BEAST_EXPECT(table->find(a) == amendmentId(a));
        for (auto const& a : obsolete_)
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
                table->getJson(unsupportedID, true)[to_string(unsupportedID)];
            BEAST_EXPECT(unsupp.size() == 0);
        }

        // After vetoing unsupportedID verify that it is in table.
        table->veto(unsupportedID);
        {
            Json::Value const unsupp =
                table->getJson(unsupportedID, true)[to_string(unsupportedID)];
            BEAST_EXPECT(unsupp[jss::vetoed].asBool());
        }
    }

    void
    testBadConfig()
    {
        auto const yesVotes = makeDefaultYes(yes_);
        auto const section = makeSection(vetoed_);
        auto const id = to_string(amendmentId(enabled_[0]));

        testcase("Bad Config");

        {  // Two arguments are required - we pass one
            Section test = section;
            test.append(id);

            try
            {
                test::jtx::Env env{*this, makeConfig()};
                if (makeTable(env, weeks(2), yesVotes, test, emptySection_))
                    fail("Accepted only amendment ID");
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(
                    e.what() == "Invalid entry '" + id + "' in [Test]");
            }
        }

        {  // Two arguments are required - we pass three
            Section test = section;
            test.append(id + " Test Name");

            try
            {
                test::jtx::Env env{*this, makeConfig()};
                if (makeTable(env, weeks(2), yesVotes, test, emptySection_))
                    fail("Accepted extra arguments");
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(
                    e.what() ==
                    "Invalid entry '" + id + " Test Name' in [Test]");
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
                if (makeTable(env, weeks(2), yesVotes, test, emptySection_))
                    fail("Accepted short amendment ID");
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(
                    e.what() == "Invalid entry '" + sid + " Name' in [Test]");
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
                if (makeTable(env, weeks(2), yesVotes, test, emptySection_))
                    fail("Accepted long amendment ID");
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(
                    e.what() == "Invalid entry '" + sid + " Name' in [Test]");
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
                if (makeTable(env, weeks(2), yesVotes, test, emptySection_))
                    fail("Accepted non-hex amendment ID");
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(
                    e.what() == "Invalid entry '" + sid + " Name' in [Test]");
            }
        }
    }

    void
    testEnableVeto()
    {
        testcase("enable and veto");

        test::jtx::Env env{*this, makeConfig()};
        std::unique_ptr<AmendmentTable> table = makeTable(env, weeks(2));

        // Note which entries are enabled (convert the amendment names to IDs)
        std::set<uint256> allEnabled;
        for (auto const& a : enabled_)
            allEnabled.insert(amendmentId(a));

        for (uint256 const& a : allEnabled)
            BEAST_EXPECT(table->enable(a));

        // So far all enabled amendments are supported.
        BEAST_EXPECT(!table->hasUnsupportedEnabled());

        // Verify all enables are enabled and nothing else.
        for (std::string const& a : yes_)
        {
            uint256 const supportedID = amendmentId(a);
            bool const enabled = table->isEnabled(supportedID);
            bool const found = allEnabled.find(supportedID) != allEnabled.end();
            BEAST_EXPECTS(
                enabled == found,
                a + (enabled ? " enabled " : " disabled ") +
                    (found ? " found" : " not found"));
        }

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
            BEAST_EXPECT(!table->unVeto(amendmentId(yes_[1])));
            BEAST_EXPECT(desired == table->getDesired());
        }

        // UnVeto one of the vetoed amendments.  It should now be desired.
        {
            uint256 const unvetoedID = amendmentId(vetoed_[0]);
            BEAST_EXPECT(table->unVeto(unvetoedID));

            std::vector<uint256> const desired = table->getDesired();
            BEAST_EXPECT(
                std::find(desired.begin(), desired.end(), unvetoedID) !=
                desired.end());
        }

        // Veto all supported amendments.  Now desired should be empty.
        for (std::string const& a : allSupported_)
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

    // Make a list of trusted validators.
    // Register the validators with AmendmentTable and return the list.
    std::vector<std::pair<PublicKey, SecretKey>>
    makeValidators(int num, std::unique_ptr<AmendmentTable> const& table)
    {
        std::vector<std::pair<PublicKey, SecretKey>> ret;
        ret.reserve(num);
        hash_set<PublicKey> trustedValidators;
        trustedValidators.reserve(num);
        for (int i = 0; i < num; ++i)
        {
            auto const& back =
                ret.emplace_back(randomKeyPair(KeyType::secp256k1));
            trustedValidators.insert(back.first);
        }
        table->trustChanged(trustedValidators);
        return ret;
    }

    static NetClock::time_point
    hourTime(std::chrono::hours h)
    {
        return NetClock::time_point{h};
    }

    // Execute a pretend consensus round for a flag ledger
    void
    doRound(
        Rules const& rules,
        AmendmentTable& table,
        std::chrono::hours hour,
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

        auto const roundTime = hourTime(hour);

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
                if (rules.enabled(fixAmendmentMajorityCalc) ? nVotes >= i
                                                            : nVotes > i)
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

        auto actions =
            table.doVoting(rules, roundTime, enabled, majority, validations);
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
    testNoOnUnknown(FeatureBitset const& feat)
    {
        testcase("Vote NO on unknown");

        auto const testAmendment = amendmentId("TestAmendment");

        test::jtx::Env env{*this, feat};
        auto table =
            makeTable(env, weeks(2), emptyYes_, emptySection_, emptySection_);

        auto const validators = makeValidators(10, table);

        std::vector<std::pair<uint256, int>> votes;
        std::vector<uint256> ourVotes;
        std::set<uint256> enabled;
        majorityAmendments_t majority;

        doRound(
            env.current()->rules(),
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

        uint256 const unsupportedID = amendmentId(unsupported_[0]);
        {
            Json::Value const unsupp =
                table->getJson(unsupportedID, false)[to_string(unsupportedID)];
            BEAST_EXPECT(unsupp.size() == 0);
        }

        table->veto(unsupportedID);
        {
            Json::Value const unsupp =
                table->getJson(unsupportedID, false)[to_string(unsupportedID)];
            BEAST_EXPECT(!unsupp[jss::vetoed].asBool());
        }

        votes.emplace_back(testAmendment, validators.size());

        votes.emplace_back(testAmendment, validators.size());

        doRound(
            env.current()->rules(),
            *table,
            weeks{2},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(ourVotes.empty());
        BEAST_EXPECT(enabled.empty());

        majority[testAmendment] = hourTime(weeks{1});

        // Note that the simulation code assumes others behave as we do,
        // so the amendment won't get enabled
        doRound(
            env.current()->rules(),
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
    testNoOnVetoed(FeatureBitset const& feat)
    {
        testcase("Vote NO on vetoed");

        auto const testAmendment = amendmentId("vetoedAmendment");

        test::jtx::Env env{*this, feat};
        auto table = makeTable(
            env,
            weeks(2),
            emptyYes_,
            emptySection_,
            makeSection(testAmendment));

        auto const validators = makeValidators(10, table);

        std::vector<std::pair<uint256, int>> votes;
        std::vector<uint256> ourVotes;
        std::set<uint256> enabled;
        majorityAmendments_t majority;

        doRound(
            env.current()->rules(),
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
            env.current()->rules(),
            *table,
            weeks{2},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(ourVotes.empty());
        BEAST_EXPECT(enabled.empty());

        majority[testAmendment] = hourTime(weeks{1});

        doRound(
            env.current()->rules(),
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
    testVoteEnable(FeatureBitset const& feat)
    {
        testcase("voteEnable");

        test::jtx::Env env{*this, feat};
        auto table = makeTable(
            env, weeks(2), makeDefaultYes(yes_), emptySection_, emptySection_);

        auto const validators = makeValidators(10, table);

        std::vector<std::pair<uint256, int>> votes;
        std::vector<uint256> ourVotes;
        std::set<uint256> enabled;
        majorityAmendments_t majority;

        // Week 1: We should vote for all known amendments not enabled
        doRound(
            env.current()->rules(),
            *table,
            weeks{1},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(ourVotes.size() == yes_.size());
        BEAST_EXPECT(enabled.empty());
        for (auto const& i : yes_)
            BEAST_EXPECT(majority.find(amendmentId(i)) == majority.end());

        // Now, everyone votes for this feature
        for (auto const& i : yes_)
            votes.emplace_back(amendmentId(i), validators.size());

        // Week 2: We should recognize a majority
        doRound(
            env.current()->rules(),
            *table,
            weeks{2},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(ourVotes.size() == yes_.size());
        BEAST_EXPECT(enabled.empty());

        for (auto const& i : yes_)
            BEAST_EXPECT(majority[amendmentId(i)] == hourTime(weeks{2}));

        // Week 5: We should enable the amendment
        doRound(
            env.current()->rules(),
            *table,
            weeks{5},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(enabled.size() == yes_.size());

        // Week 6: We should remove it from our votes and from having a majority
        doRound(
            env.current()->rules(),
            *table,
            weeks{6},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(enabled.size() == yes_.size());
        BEAST_EXPECT(ourVotes.empty());
        for (auto const& i : yes_)
            BEAST_EXPECT(majority.find(amendmentId(i)) == majority.end());
    }

    // Detect majority at 80%, enable later
    void
    testDetectMajority(FeatureBitset const& feat)
    {
        testcase("detectMajority");

        auto const testAmendment = amendmentId("detectMajority");
        test::jtx::Env env{*this, feat};
        auto table = makeTable(
            env,
            weeks(2),
            makeDefaultYes(testAmendment),
            emptySection_,
            emptySection_);

        auto const validators = makeValidators(16, table);

        std::set<uint256> enabled;
        majorityAmendments_t majority;

        for (int i = 0; i <= 17; ++i)
        {
            std::vector<std::pair<uint256, int>> votes;
            std::vector<uint256> ourVotes;

            if ((i > 0) && (i < 17))
                votes.emplace_back(testAmendment, i);

            doRound(
                env.current()->rules(),
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
    testLostMajority(FeatureBitset const& feat)
    {
        testcase("lostMajority");

        auto const testAmendment = amendmentId("lostMajority");

        test::jtx::Env env{*this, feat};
        auto table = makeTable(
            env,
            weeks(8),
            makeDefaultYes(testAmendment),
            emptySection_,
            emptySection_);

        auto const validators = makeValidators(16, table);

        std::set<uint256> enabled;
        majorityAmendments_t majority;

        {
            // establish majority
            std::vector<std::pair<uint256, int>> votes;
            std::vector<uint256> ourVotes;

            votes.emplace_back(testAmendment, validators.size());

            doRound(
                env.current()->rules(),
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
                env.current()->rules(),
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

    // Exercise the UNL changing while voting is in progress.
    void
    testChangedUNL(FeatureBitset const& feat)
    {
        // This test doesn't work without fixAmendmentMajorityCalc enabled.
        if (!feat[fixAmendmentMajorityCalc])
            return;

        testcase("changedUNL");

        auto const testAmendment = amendmentId("changedUNL");
        test::jtx::Env env{*this, feat};
        auto table = makeTable(
            env,
            weeks(8),
            makeDefaultYes(testAmendment),
            emptySection_,
            emptySection_);

        std::vector<std::pair<PublicKey, SecretKey>> validators =
            makeValidators(10, table);

        std::set<uint256> enabled;
        majorityAmendments_t majority;

        {
            // 10 validators with 2 voting against won't get majority.
            std::vector<std::pair<uint256, int>> votes;
            std::vector<uint256> ourVotes;

            votes.emplace_back(testAmendment, validators.size() - 2);

            doRound(
                env.current()->rules(),
                *table,
                weeks{1},
                validators,
                votes,
                ourVotes,
                enabled,
                majority);

            BEAST_EXPECT(enabled.empty());
            BEAST_EXPECT(majority.empty());
        }

        // Add one new validator to the UNL.
        validators.emplace_back(randomKeyPair(KeyType::secp256k1));

        // A lambda that updates the AmendmentTable with the latest
        // trusted validators.
        auto callTrustChanged =
            [](std::vector<std::pair<PublicKey, SecretKey>> const& validators,
               std::unique_ptr<AmendmentTable> const& table) {
                // We need a hash_set to pass to trustChanged.
                hash_set<PublicKey> trustedValidators;
                trustedValidators.reserve(validators.size());
                std::for_each(
                    validators.begin(),
                    validators.end(),
                    [&trustedValidators](auto const& val) {
                        trustedValidators.insert(val.first);
                    });

                // Tell the AmendmentTable that the UNL changed.
                table->trustChanged(trustedValidators);
            };

        // Tell the table that there's been a change in trusted validators.
        callTrustChanged(validators, table);

        {
            // 11 validators with 2 voting against gains majority.
            std::vector<std::pair<uint256, int>> votes;
            std::vector<uint256> ourVotes;

            votes.emplace_back(testAmendment, validators.size() - 2);

            doRound(
                env.current()->rules(),
                *table,
                weeks{2},
                validators,
                votes,
                ourVotes,
                enabled,
                majority);

            BEAST_EXPECT(enabled.empty());
            BEAST_EXPECT(!majority.empty());
        }
        {
            // One of the validators goes flaky and doesn't send validations
            // (without the UNL changing) so the amendment loses majority.
            std::pair<PublicKey, SecretKey> const savedValidator =
                validators.front();
            validators.erase(validators.begin());

            std::vector<std::pair<uint256, int>> votes;
            std::vector<uint256> ourVotes;

            votes.emplace_back(testAmendment, validators.size() - 2);

            doRound(
                env.current()->rules(),
                *table,
                weeks{3},
                validators,
                votes,
                ourVotes,
                enabled,
                majority);

            BEAST_EXPECT(enabled.empty());
            BEAST_EXPECT(majority.empty());

            // Simulate the validator re-syncing to the network by adding it
            // back to the validators vector
            validators.insert(validators.begin(), savedValidator);

            votes.front().second = validators.size() - 2;

            doRound(
                env.current()->rules(),
                *table,
                weeks{4},
                validators,
                votes,
                ourVotes,
                enabled,
                majority);

            BEAST_EXPECT(enabled.empty());
            BEAST_EXPECT(!majority.empty());

            // Finally, remove one validator from the UNL and see that majority
            // is lost.
            validators.erase(validators.begin());

            // Tell the table that there's been a change in trusted validators.
            callTrustChanged(validators, table);

            votes.front().second = validators.size() - 2;

            doRound(
                env.current()->rules(),
                *table,
                weeks{5},
                validators,
                votes,
                ourVotes,
                enabled,
                majority);

            BEAST_EXPECT(enabled.empty());
            BEAST_EXPECT(majority.empty());
        }
    }

    // Exercise a validator losing connectivity and then regaining it after
    // extended delays.  Depending on how long that delay is an amendment
    // either will or will not go live.
    void
    testValidatorFlapping(FeatureBitset const& feat)
    {
        // This test doesn't work without fixAmendmentMajorityCalc enabled.
        if (!feat[fixAmendmentMajorityCalc])
            return;

        testcase("validatorFlapping");

        // We run a test where a validator flaps on and off every 23 hours
        // and another one one where it flaps on and off every 25 hours.
        //
        // Since the local validator vote record expires after 24 hours,
        // with 23 hour flapping the amendment will go live.  But with 25
        // hour flapping the amendment will not go live.
        for (int flapRateHours : {23, 25})
        {
            test::jtx::Env env{*this, feat};
            auto const testAmendment = amendmentId("validatorFlapping");
            auto table = makeTable(
                env,
                weeks(1),
                makeDefaultYes(testAmendment),
                emptySection_,
                emptySection_);

            // Make two lists of validators, one with a missing validator, to
            // make it easy to simulate validator flapping.
            auto const allValidators = makeValidators(11, table);
            decltype(allValidators) const mostValidators(
                allValidators.begin() + 1, allValidators.end());
            BEAST_EXPECT(allValidators.size() == mostValidators.size() + 1);

            std::set<uint256> enabled;
            majorityAmendments_t majority;

            std::vector<std::pair<uint256, int>> votes;
            std::vector<uint256> ourVotes;

            votes.emplace_back(testAmendment, allValidators.size() - 2);

            int delay = flapRateHours;
            // Loop for 1 week plus a day.
            for (int hour = 1; hour < (24 * 8); ++hour)
            {
                decltype(allValidators) const& thisHoursValidators =
                    (delay < flapRateHours) ? mostValidators : allValidators;
                delay = delay == flapRateHours ? 0 : delay + 1;

                votes.front().second = thisHoursValidators.size() - 2;

                using namespace std::chrono;
                doRound(
                    env.current()->rules(),
                    *table,
                    hours(hour),
                    thisHoursValidators,
                    votes,
                    ourVotes,
                    enabled,
                    majority);

                if (hour <= (24 * 7) || flapRateHours > 24)
                {
                    // The amendment should not be enabled under any
                    // circumstance until one week has elapsed.
                    BEAST_EXPECT(enabled.empty());

                    // If flapping is less than 24 hours, there should be
                    // no flapping.  Otherwise we should only have majority
                    // if allValidators vote -- which means there are no
                    // missing validators.
                    bool const expectMajority = (delay <= 24)
                        ? true
                        : &thisHoursValidators == &allValidators;
                    BEAST_EXPECT(majority.empty() != expectMajority);
                }
                else
                {
                    // We're...
                    //  o Past one week, and
                    //  o AmendmentFlapping was less than 24 hours.
                    // The amendment should be enabled.
                    BEAST_EXPECT(!enabled.empty());
                    BEAST_EXPECT(majority.empty());
                }
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
    testFeature(FeatureBitset const& feat)
    {
        testNoOnUnknown(feat);
        testNoOnVetoed(feat);
        testVoteEnable(feat);
        testDetectMajority(feat);
        testLostMajority(feat);
        testChangedUNL(feat);
        testValidatorFlapping(feat);
    }

    void
    run() override
    {
        FeatureBitset const all{test::jtx::supported_amendments()};
        FeatureBitset const fixMajorityCalc{fixAmendmentMajorityCalc};

        testConstruct();
        testGet();
        testBadConfig();
        testEnableVeto();
        testHasUnsupported();
        testFeature(all - fixMajorityCalc);
        testFeature(all);
    }
};

BEAST_DEFINE_TESTSUITE(AmendmentTable, app, ripple);

}  // namespace ripple
