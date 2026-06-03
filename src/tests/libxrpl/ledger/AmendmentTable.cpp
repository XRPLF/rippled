#include <xrpl/ledger/AmendmentTable.h>

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <helpers/TestServiceRegistry.h>
#include <helpers/TxTest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace xrpl::test {

using ::testing::ThrowsMessage;

/**
 * @brief Test fixture for the AmendmentTable.
 *
 * Provides a TestServiceRegistry (with an in-memory wallet DB) and a collection
 * of helpers for building amendment sections, feature lists and validators.
 */
struct AmendmentTableTest : ::testing::Test
{
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
    makeSection(std::string const& name, std::vector<std::string> const& amendments)
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

    static std::vector<AmendmentTable::FeatureInfo>
    makeFeatureInfo(std::vector<std::string> const& amendments, VoteBehavior voteBehavior)
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
    static std::size_t
    totalSize(std::vector<Arg> const& src, Args const&... args)
    {
        if constexpr (sizeof...(args) > 0)
            return src.size() + totalSize(args...);
        return src.size();
    }

    template <class Arg, class... Args>
    static void
    combineArg(std::vector<Arg>& dest, std::vector<Arg> const& src, Args const&... args)
    {
        std::copy(src.begin(), src.end(), std::back_inserter(dest));
        if constexpr (sizeof...(args) > 0)
            combineArg(dest, args...);
    }

    template <class Arg, class... Args>
    static std::vector<Arg>
    combine(std::vector<Arg> left, std::vector<Arg> const& right, Args const&... args)
    {
        left.reserve(totalSize(left, right, args...));
        combineArg(left, right, args...);
        return left;
    }

    // All useful amendments are supported amendments.
    // Enabled amendments are typically a subset of supported amendments.
    // Vetoed amendments should be supported but not enabled.
    // Unsupported amendments may be added to the AmendmentTable.
    std::vector<std::string> const yes_{"g", "i", "k", "m", "o", "q", "r", "s", "t", "u"};
    std::vector<std::string> const enabled_{"b", "d", "f", "h", "j", "l", "n", "p"};
    std::vector<std::string> const vetoed_{"a", "c", "e"};
    std::vector<std::string> const obsolete_{"0", "1", "2"};
    std::vector<std::string> const allSupported_{combine(yes_, enabled_, vetoed_, obsolete_)};
    std::vector<std::string> const unsupported_{"v", "w", "x"};
    std::vector<std::string> const unsupportedMajority_{"y", "z"};

    Section const emptySection_;
    std::vector<AmendmentTable::FeatureInfo> const emptyYes_;

    TestServiceRegistry registry_;
    beast::Journal journal_{registry_.getJournal("AmendmentTableTest")};

    std::unique_ptr<AmendmentTable>
    makeTable(
        std::chrono::seconds majorityTime,
        std::vector<AmendmentTable::FeatureInfo> const& supported,
        Section const& enabled,
        Section const& vetoed)
    {
        return makeAmendmentTable(registry_, majorityTime, supported, enabled, vetoed, journal_);
    }

    std::unique_ptr<AmendmentTable>
    makeTable(std::chrono::seconds majorityTime)
    {
        static std::vector<AmendmentTable::FeatureInfo> const kSupported = combine(
            makeDefaultYes(yes_),
            // Use non-intuitive default votes for "enabled_" and "vetoed_"
            // so that when the tests later explicitly enable or veto them,
            // we can be certain that they are not simply going by their
            // default vote setting.
            makeDefaultNo(enabled_),
            makeDefaultYes(vetoed_),
            makeObsolete(obsolete_));
        return makeTable(majorityTime, kSupported, makeSection(enabled_), makeSection(vetoed_));
    }

    // Build a Rules object that has all testable amendments enabled.
    static Rules const&
    allRules()
    {
        static Rules const kRules = [] {
            std::unordered_set<uint256, beast::Uhash<>> featureSet;
            foreachFeature(allFeatures(), [&](uint256 const& f) { featureSet.insert(f); });
            return Rules{featureSet};
        }();
        return kRules;
    }

    // Make a list of trusted validators.
    // Register the validators with AmendmentTable and return the list.
    static std::vector<std::pair<PublicKey, SecretKey>>
    makeValidators(int num, std::unique_ptr<AmendmentTable> const& table)
    {
        std::vector<std::pair<PublicKey, SecretKey>> ret;
        ret.reserve(num);
        hash_set<PublicKey> trustedValidators;
        trustedValidators.reserve(num);
        for (int i = 0; i < num; ++i)
        {
            auto const& back = ret.emplace_back(randomKeyPair(KeyType::Secp256k1));
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

    // State threaded through successive consensus rounds.
    // votes:      Amendments and the number of validators who vote for them
    // enabled:    In/out enabled amendments
    // majority:   In/out majority amendments (and when they got a majority)
    struct VotingState
    {
        std::vector<std::pair<uint256, int>> votes;
        std::set<uint256> enabled;
        majorityAmendments_t majority;
    };

    // Execute a pretend consensus round for a flag ledger, returning the
    // amendments we voted for.
    static std::vector<uint256>
    doRound(
        Rules const& rules,
        AmendmentTable& table,
        std::chrono::hours hour,
        std::vector<std::pair<PublicKey, SecretKey>> const& validators,
        VotingState& votingState)
    {
        // Do a round at the specified time

        // Parameters:
        // table:       Our table of known and vetoed amendments
        // validators:  The addresses of validators we trust
        // votingState: The voting state carried across rounds (see VotingState)

        auto const roundTime = hourTime(hour);

        // Build validations
        std::vector<std::shared_ptr<STValidation>> validations;
        validations.reserve(validators.size());

        int i = 0;
        for (auto const& [pub, sec] : validators)
        {
            ++i;
            std::vector<uint256> field;

            for (auto const& [hash, nVotes] : votingState.votes)
            {
                if (nVotes >= i)
                {
                    // We vote yes on this amendment
                    field.push_back(hash);
                }
            }

            auto v = std::make_shared<STValidation>(
                xrpl::NetClock::time_point{}, pub, sec, calcNodeID(pub), [&field](STValidation& v) {
                    if (!field.empty())
                        v.setFieldV256(sfAmendments, STVector256(sfAmendments, field));
                    v.setFieldU32(sfLedgerSequence, 6180339);
                });

            validations.emplace_back(v);
        }

        std::vector<uint256> ourVotes = table.doValidation(votingState.enabled);

        auto actions = table.doVoting(
            rules, roundTime, votingState.enabled, votingState.majority, validations);
        for (auto const& [hash, action] : actions)
        {
            // This code assumes other validators do as we do

            switch (action)
            {
                case 0:
                    // amendment goes from majority to enabled
                    if (votingState.enabled.contains(hash))
                        Throw<std::runtime_error>("enabling already enabled");
                    if (!votingState.majority.contains(hash))
                        Throw<std::runtime_error>("enabling without majority");
                    votingState.enabled.insert(hash);
                    votingState.majority.erase(hash);
                    break;

                case tfGotMajority:
                    if (votingState.majority.contains(hash))
                        Throw<std::runtime_error>("got majority while having majority");
                    votingState.majority[hash] = roundTime;
                    break;

                case tfLostMajority:
                    if (!votingState.majority.contains(hash))
                        Throw<std::runtime_error>("lost majority without majority");
                    votingState.majority.erase(hash);
                    break;

                default:
                    Throw<std::runtime_error>("unknown action");
            }
        }

        return ourVotes;
    }
};

TEST_F(AmendmentTableTest, construction)
{
    auto table = makeTable(weeks(1));

    for (auto const& a : allSupported_)
        EXPECT_TRUE(table->isSupported(amendmentId(a)));

    for (auto const& a : yes_)
        EXPECT_TRUE(table->isSupported(amendmentId(a)));

    for (auto const& a : enabled_)
        EXPECT_TRUE(table->isSupported(amendmentId(a)));

    for (auto const& a : vetoed_)
    {
        EXPECT_TRUE(table->isSupported(amendmentId(a)));
        EXPECT_FALSE(table->isEnabled(amendmentId(a)));
    }

    for (auto const& a : obsolete_)
    {
        EXPECT_TRUE(table->isSupported(amendmentId(a)));
        EXPECT_FALSE(table->isEnabled(amendmentId(a)));
    }
}

TEST_F(AmendmentTableTest, name_to_id_mapping)
{
    auto table = makeTable(weeks(1));

    for (auto const& a : yes_)
        EXPECT_EQ(table->find(a), amendmentId(a));
    for (auto const& a : enabled_)
        EXPECT_EQ(table->find(a), amendmentId(a));
    for (auto const& a : vetoed_)
        EXPECT_EQ(table->find(a), amendmentId(a));
    for (auto const& a : obsolete_)
        EXPECT_EQ(table->find(a), amendmentId(a));
    for (auto const& a : unsupported_)
        EXPECT_FALSE(table->find(a));
    for (auto const& a : unsupportedMajority_)
        EXPECT_FALSE(table->find(a));
}

// Vetoing an unsupported amendment adds it to the table.  The veto is only
// visible through getJson when admin information is requested.
TEST_F(AmendmentTableTest, getjson_veto_unsupported)
{
    auto table = makeTable(weeks(1));

    uint256 const unsupportedID = amendmentId(unsupported_[0]);

    // Before vetoing, the amendment is not in the table.
    {
        json::Value const unsupp = table->getJson(unsupportedID, true)[to_string(unsupportedID)];
        EXPECT_EQ(unsupp.size(), 0u);
    }
    {
        json::Value const unsupp = table->getJson(unsupportedID, false)[to_string(unsupportedID)];
        EXPECT_EQ(unsupp.size(), 0u);
    }

    table->veto(unsupportedID);

    // After vetoing, the veto is reported with admin, but not without.
    {
        json::Value const unsupp = table->getJson(unsupportedID, true)[to_string(unsupportedID)];
        EXPECT_TRUE(unsupp[jss::vetoed].asBool());
    }
    {
        json::Value const unsupp = table->getJson(unsupportedID, false)[to_string(unsupportedID)];
        EXPECT_FALSE(unsupp[jss::vetoed].asBool());
    }
}

// Each case provides a function that builds a malformed amendment entry from a
// valid amendment id.  Every malformed entry must be rejected with the same
// "Invalid entry" error.
struct BadConfigParam
{
    std::string name;
    std::string (*makeEntry)(std::string const& id);
};

struct AmendmentTableBadConfigTest : AmendmentTableTest,
                                     ::testing::WithParamInterface<BadConfigParam>
{
};

TEST_P(AmendmentTableBadConfigTest, rejects_invalid_entry)
{
    auto const yesVotes = makeDefaultYes(yes_);
    auto const section = makeSection(vetoed_);
    auto const id = to_string(amendmentId(enabled_[0]));

    std::string const entry = GetParam().makeEntry(id);

    Section test = section;
    test.append(entry);

    EXPECT_THAT(
        [&] { makeTable(weeks(2), yesVotes, test, emptySection_); },
        ThrowsMessage<std::runtime_error>("Invalid entry '" + entry + "' in [Test]"));
}

INSTANTIATE_TEST_SUITE_P(
    AmendmentTableBadConfig,
    AmendmentTableBadConfigTest,
    ::testing::Values(
        // Two arguments are required - we pass one
        BadConfigParam{
            "too_few_arguments",
            [](std::string const& id) -> std::string { return id; }},
        // Two arguments are required - we pass three
        BadConfigParam{
            "too_many_arguments",
            [](std::string const& id) -> std::string { return id + " Test Name"; }},
        BadConfigParam{
            "short_id",
            [](std::string const& id) -> std::string {
                std::string sid = id;
                sid.resize(sid.length() - 1);
                return sid + " Name";
            }},
        BadConfigParam{
            "long_id",
            [](std::string const& id) -> std::string {
                std::string sid = id;
                sid.resize(sid.length() + 1, '0');
                return sid + " Name";
            }},
        BadConfigParam{
            "non_hex_id",
            [](std::string const& id) -> std::string {
                std::string sid = id;
                sid.resize(sid.length() - 1);
                sid.push_back('Q');
                return sid + " Name";
            }}),
    [](::testing::TestParamInfo<BadConfigParam> const& info) { return info.param.name; });

TEST_F(AmendmentTableTest, enable)
{
    std::unique_ptr<AmendmentTable> table = makeTable(weeks(2));

    // Note which entries are enabled (convert the amendment names to IDs)
    std::set<uint256> allEnabled;
    for (auto const& a : enabled_)
        allEnabled.insert(amendmentId(a));

    for (uint256 const& a : allEnabled)
        EXPECT_TRUE(table->enable(a));

    // So far all enabled amendments are supported.
    EXPECT_FALSE(table->hasUnsupportedEnabled());

    // Verify all enables are enabled and nothing else.
    for (std::string const& a : yes_)
    {
        uint256 const supportedID = amendmentId(a);
        bool const enabled = table->isEnabled(supportedID);
        bool const found = allEnabled.contains(supportedID);
        EXPECT_EQ(enabled, found) << a << (enabled ? " enabled " : " disabled ")
                                  << (found ? " found" : " not found");
    }
}

// All supported and unVetoed amendments should be returned as desired.
TEST_F(AmendmentTableTest, desired_excludes_vetoed)
{
    std::unique_ptr<AmendmentTable> table = makeTable(weeks(2));

    for (std::string const& a : enabled_)
        table->enable(amendmentId(a));

    std::set<uint256> vetoed;
    for (std::string const& a : vetoed_)
        vetoed.insert(amendmentId(a));

    std::vector<uint256> const desired = table->getDesired();
    for (uint256 const& a : desired)
        EXPECT_TRUE(not vetoed.contains(a));

    // Unveto an amendment that is already not vetoed.  Shouldn't
    // hurt anything, but the values returned by getDesired()
    // shouldn't change.
    EXPECT_FALSE(table->unVeto(amendmentId(yes_[1])));
    EXPECT_EQ(desired, table->getDesired());
}

// UnVeto one of the vetoed amendments.  It should now be desired.
TEST_F(AmendmentTableTest, unveto_vetoed)
{
    std::unique_ptr<AmendmentTable> table = makeTable(weeks(2));

    for (std::string const& a : enabled_)
        table->enable(amendmentId(a));

    uint256 const unvetoedID = amendmentId(vetoed_[0]);
    EXPECT_TRUE(table->unVeto(unvetoedID));

    std::vector<uint256> const desired = table->getDesired();
    EXPECT_TRUE(std::ranges::find(desired, unvetoedID) != desired.end());
}

// Veto all supported amendments.  Now desired should be empty.
TEST_F(AmendmentTableTest, veto_all)
{
    std::unique_ptr<AmendmentTable> table = makeTable(weeks(2));

    for (std::string const& a : enabled_)
        table->enable(amendmentId(a));

    for (std::string const& a : allSupported_)
        table->veto(amendmentId(a));

    EXPECT_TRUE(table->getDesired().empty());
}

// Enable an unsupported amendment.
TEST_F(AmendmentTableTest, enable_unsupported)
{
    std::unique_ptr<AmendmentTable> table = makeTable(weeks(2));

    EXPECT_FALSE(table->hasUnsupportedEnabled());
    table->enable(amendmentId(unsupported_[0]));
    EXPECT_TRUE(table->hasUnsupportedEnabled());
}

TEST_F(AmendmentTableTest, has_unsupported_enabled)
{
    using namespace std::chrono_literals;
    constexpr weeks kW(1);
    auto table = makeTable(kW);
    EXPECT_FALSE(table->hasUnsupportedEnabled());
    EXPECT_FALSE(table->firstUnsupportedExpected());
    EXPECT_TRUE(table->needValidatedLedger(1));

    std::set<uint256> enabled;
    std::ranges::for_each(
        unsupported_, [&enabled](auto const& s) { enabled.insert(amendmentId(s)); });

    majorityAmendments_t majority;
    table->doValidatedLedger(1, enabled, majority);
    EXPECT_TRUE(table->hasUnsupportedEnabled());
    EXPECT_FALSE(table->firstUnsupportedExpected());

    NetClock::duration t{1000s};
    std::ranges::for_each(unsupportedMajority_, [&majority, &t](auto const& s) {
        majority[amendmentId(s)] = NetClock::time_point{--t};
    });

    table->doValidatedLedger(1, enabled, majority);
    EXPECT_TRUE(table->hasUnsupportedEnabled());
    ASSERT_TRUE(table->firstUnsupportedExpected());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) checked in ASSERT_TRUE above
    EXPECT_EQ(*table->firstUnsupportedExpected(), NetClock::time_point{t} + kW);

    // Make sure the table knows when it needs an update.
    EXPECT_FALSE(table->needValidatedLedger(256));
    EXPECT_TRUE(table->needValidatedLedger(257));
}

// No vote on unknown amendment
TEST_F(AmendmentTableTest, no_on_unknown)
{
    auto const testAmendment = amendmentId("TestAmendment");

    auto table = makeTable(weeks(2), emptyYes_, emptySection_, emptySection_);

    auto const validators = makeValidators(10, table);

    VotingState votingState;

    std::vector<uint256> ourVotes = doRound(allRules(), *table, weeks{1}, validators, votingState);
    EXPECT_TRUE(ourVotes.empty());
    EXPECT_TRUE(votingState.enabled.empty());
    EXPECT_TRUE(votingState.majority.empty());

    votingState.votes.emplace_back(testAmendment, validators.size());

    votingState.votes.emplace_back(testAmendment, validators.size());

    ourVotes = doRound(allRules(), *table, weeks{2}, validators, votingState);
    EXPECT_TRUE(ourVotes.empty());
    EXPECT_TRUE(votingState.enabled.empty());

    votingState.majority[testAmendment] = hourTime(weeks{1});

    // Note that the simulation code assumes others behave as we do,
    // so the amendment won't get enabled
    ourVotes = doRound(allRules(), *table, weeks{5}, validators, votingState);
    EXPECT_TRUE(ourVotes.empty());
    EXPECT_TRUE(votingState.enabled.empty());
}

// No vote on vetoed amendment
TEST_F(AmendmentTableTest, no_on_vetoed)
{
    auto const testAmendment = amendmentId("vetoedAmendment");

    auto table = makeTable(weeks(2), emptyYes_, emptySection_, makeSection(testAmendment));

    auto const validators = makeValidators(10, table);

    VotingState votingState;

    std::vector<uint256> ourVotes = doRound(allRules(), *table, weeks{1}, validators, votingState);
    EXPECT_TRUE(ourVotes.empty());
    EXPECT_TRUE(votingState.enabled.empty());
    EXPECT_TRUE(votingState.majority.empty());

    votingState.votes.emplace_back(testAmendment, validators.size());

    ourVotes = doRound(allRules(), *table, weeks{2}, validators, votingState);
    EXPECT_TRUE(ourVotes.empty());
    EXPECT_TRUE(votingState.enabled.empty());

    votingState.majority[testAmendment] = hourTime(weeks{1});

    ourVotes = doRound(allRules(), *table, weeks{5}, validators, votingState);
    EXPECT_TRUE(ourVotes.empty());
    EXPECT_TRUE(votingState.enabled.empty());
}

// Vote on and enable known, not-enabled amendment
TEST_F(AmendmentTableTest, vote_enable)
{
    auto table = makeTable(weeks(2), makeDefaultYes(yes_), emptySection_, emptySection_);

    auto const validators = makeValidators(10, table);

    VotingState votingState;

    // Week 1: We should vote for all known amendments not enabled
    std::vector<uint256> ourVotes = doRound(allRules(), *table, weeks{1}, validators, votingState);
    EXPECT_EQ(ourVotes.size(), yes_.size());
    EXPECT_TRUE(votingState.enabled.empty());
    for (auto const& i : yes_)
        EXPECT_TRUE(!votingState.majority.contains(amendmentId(i)));

    // Now, everyone votes for this feature
    for (auto const& i : yes_)
        votingState.votes.emplace_back(amendmentId(i), validators.size());

    // Week 2: We should recognize a majority
    ourVotes = doRound(allRules(), *table, weeks{2}, validators, votingState);
    EXPECT_EQ(ourVotes.size(), yes_.size());
    EXPECT_TRUE(votingState.enabled.empty());

    for (auto const& i : yes_)
        EXPECT_EQ(votingState.majority[amendmentId(i)], hourTime(weeks{2}));

    // Week 5: We should enable the amendment
    ourVotes = doRound(allRules(), *table, weeks{5}, validators, votingState);
    EXPECT_EQ(votingState.enabled.size(), yes_.size());

    // Week 6: We should remove it from our votes and from having a majority
    ourVotes = doRound(allRules(), *table, weeks{6}, validators, votingState);
    EXPECT_EQ(votingState.enabled.size(), yes_.size());
    EXPECT_TRUE(ourVotes.empty());
    for (auto const& i : yes_)
        EXPECT_TRUE(!votingState.majority.contains(amendmentId(i)));
}

// Detect majority at 80%, enable later
TEST_F(AmendmentTableTest, detect_majority)
{
    auto const testAmendment = amendmentId("detectMajority");
    auto table = makeTable(weeks(2), makeDefaultYes(testAmendment), emptySection_, emptySection_);

    auto const validators = makeValidators(16, table);

    VotingState votingState;

    for (int i = 0; i <= 17; ++i)
    {
        votingState.votes.clear();

        if ((i > 0) && (i < 17))
            votingState.votes.emplace_back(testAmendment, i);

        std::vector<uint256> const ourVotes =
            doRound(allRules(), *table, weeks{i}, validators, votingState);

        if (i < 13)  // 13 => 13/16 = 0.8125 => > 80%
        {
            // We are voting yes, not enabled, no majority
            EXPECT_FALSE(ourVotes.empty());
            EXPECT_TRUE(votingState.enabled.empty());
            EXPECT_TRUE(votingState.majority.empty());
        }
        else if (i < 15)
        {
            // We have a majority, not enabled, keep voting
            EXPECT_FALSE(ourVotes.empty());
            EXPECT_FALSE(votingState.majority.empty());
            EXPECT_TRUE(votingState.enabled.empty());
        }
        else if (i == 15)
        {
            // enable, keep voting, remove from majority
            EXPECT_FALSE(ourVotes.empty());
            EXPECT_TRUE(votingState.majority.empty());
            EXPECT_FALSE(votingState.enabled.empty());
        }
        else
        {
            // Done, we should be enabled and not voting
            EXPECT_TRUE(ourVotes.empty());
            EXPECT_TRUE(votingState.majority.empty());
            EXPECT_FALSE(votingState.enabled.empty());
        }
    }
}

// Detect loss of majority
TEST_F(AmendmentTableTest, lost_majority)
{
    auto const testAmendment = amendmentId("lostMajority");

    auto table = makeTable(weeks(8), makeDefaultYes(testAmendment), emptySection_, emptySection_);

    auto const validators = makeValidators(16, table);

    VotingState votingState;

    {
        // establish majority
        votingState.votes.emplace_back(testAmendment, validators.size());

        doRound(allRules(), *table, weeks{1}, validators, votingState);

        EXPECT_TRUE(votingState.enabled.empty());
        EXPECT_FALSE(votingState.majority.empty());
    }

    for (int i = 1; i < 8; ++i)
    {
        // Gradually reduce support
        votingState.votes.clear();
        votingState.votes.emplace_back(testAmendment, validators.size() - i);

        std::vector<uint256> const ourVotes =
            doRound(allRules(), *table, weeks{i + 1}, validators, votingState);

        if (i < 4)  // 16 - 3 = 13 => 13/16 = 0.8125 => > 80%
        {           // 16 - 4 = 12 => 12/16 = 0.75 => < 80%
            // We are voting yes, not enabled, majority
            EXPECT_FALSE(ourVotes.empty());
            EXPECT_TRUE(votingState.enabled.empty());
            EXPECT_FALSE(votingState.majority.empty());
        }
        else
        {
            // No majority, not enabled, keep voting
            EXPECT_FALSE(ourVotes.empty());
            EXPECT_TRUE(votingState.majority.empty());
            EXPECT_TRUE(votingState.enabled.empty());
        }
    }
}

// Exercise the UNL changing while voting is in progress.
TEST_F(AmendmentTableTest, changed_unl)
{
    auto const testAmendment = amendmentId("changedUNL");
    auto table = makeTable(weeks(8), makeDefaultYes(testAmendment), emptySection_, emptySection_);

    std::vector<std::pair<PublicKey, SecretKey>> validators = makeValidators(10, table);

    VotingState votingState;

    {
        // 10 validators with 2 voting against won't get majority.
        votingState.votes.emplace_back(testAmendment, validators.size() - 2);

        doRound(allRules(), *table, weeks{1}, validators, votingState);

        EXPECT_TRUE(votingState.enabled.empty());
        EXPECT_TRUE(votingState.majority.empty());
    }

    // Add one new validator to the UNL.
    validators.emplace_back(randomKeyPair(KeyType::Secp256k1));

    // A lambda that updates the AmendmentTable with the latest
    // trusted validators.
    auto callTrustChanged = [](std::vector<std::pair<PublicKey, SecretKey>> const& validators,
                               std::unique_ptr<AmendmentTable> const& table) {
        // We need a hash_set to pass to trustChanged.
        hash_set<PublicKey> trustedValidators;
        trustedValidators.reserve(validators.size());
        std::ranges::for_each(validators, [&trustedValidators](auto const& val) {
            trustedValidators.insert(val.first);
        });

        // Tell the AmendmentTable that the UNL changed.
        table->trustChanged(trustedValidators);
    };

    // Tell the table that there's been a change in trusted validators.
    callTrustChanged(validators, table);

    {
        // 11 validators with 2 voting against gains majority.
        votingState.votes.clear();
        votingState.votes.emplace_back(testAmendment, validators.size() - 2);

        doRound(allRules(), *table, weeks{2}, validators, votingState);

        EXPECT_TRUE(votingState.enabled.empty());
        EXPECT_FALSE(votingState.majority.empty());
    }
    {
        // One of the validators goes flaky and doesn't send validations
        // (without the UNL changing) so the amendment loses majority.
        std::pair<PublicKey, SecretKey> const savedValidator = validators.front();
        validators.erase(validators.begin());

        votingState.votes.clear();
        votingState.votes.emplace_back(testAmendment, validators.size() - 2);

        doRound(allRules(), *table, weeks{3}, validators, votingState);

        EXPECT_TRUE(votingState.enabled.empty());
        EXPECT_TRUE(votingState.majority.empty());

        // Simulate the validator re-syncing to the network by adding it
        // back to the validators vector
        validators.insert(validators.begin(), savedValidator);

        votingState.votes.front().second = validators.size() - 2;

        doRound(allRules(), *table, weeks{4}, validators, votingState);

        EXPECT_TRUE(votingState.enabled.empty());
        EXPECT_FALSE(votingState.majority.empty());

        // Finally, remove one validator from the UNL and see that majority
        // is lost.
        validators.erase(validators.begin());

        // Tell the table that there's been a change in trusted validators.
        callTrustChanged(validators, table);

        votingState.votes.front().second = validators.size() - 2;

        doRound(allRules(), *table, weeks{5}, validators, votingState);

        EXPECT_TRUE(votingState.enabled.empty());
        EXPECT_TRUE(votingState.majority.empty());
    }
}

// Exercise a validator losing connectivity and then regaining it after
// extended delays.  Depending on how long that delay is an amendment
// either will or will not go live.
TEST_F(AmendmentTableTest, validator_flapping)
{
    // We run a test where a validator flaps on and off every 23 hours
    // and another one one where it flaps on and off every 25 hours.
    //
    // Since the local validator vote record expires after 24 hours,
    // with 23 hour flapping the amendment will go live.  But with 25
    // hour flapping the amendment will not go live.
    for (int const flapRateHours : {23, 25})
    {
        auto const testAmendment = amendmentId("validatorFlapping");
        auto table =
            makeTable(weeks(1), makeDefaultYes(testAmendment), emptySection_, emptySection_);

        // Make two lists of validators, one with a missing validator, to
        // make it easy to simulate validator flapping.
        auto const allValidators = makeValidators(11, table);
        decltype(allValidators)
            const mostValidators(allValidators.begin() + 1, allValidators.end());
        EXPECT_EQ(allValidators.size(), mostValidators.size() + 1);

        VotingState votingState;

        votingState.votes.emplace_back(testAmendment, allValidators.size() - 2);

        int delay = flapRateHours;
        // Loop for 1 week plus a day.
        for (int hour = 1; hour < (24 * 8); ++hour)
        {
            decltype(allValidators) const& thisHoursValidators =
                (delay < flapRateHours) ? mostValidators : allValidators;
            delay = delay == flapRateHours ? 0 : delay + 1;

            votingState.votes.front().second = thisHoursValidators.size() - 2;

            using namespace std::chrono;
            doRound(allRules(), *table, hours(hour), thisHoursValidators, votingState);

            if (hour <= (24 * 7) || flapRateHours > 24)
            {
                // The amendment should not be enabled under any
                // circumstance until one week has elapsed.
                EXPECT_TRUE(votingState.enabled.empty());

                // If flapping is less than 24 hours, there should be
                // no flapping.  Otherwise we should only have majority
                // if allValidators vote -- which means there are no
                // missing validators.
                bool const expectMajority =
                    (delay <= 24) ? true : &thisHoursValidators == &allValidators;
                EXPECT_NE(votingState.majority.empty(), expectMajority);
            }
            else
            {
                // We're...
                //  o Past one week, and
                //  o AmendmentFlapping was less than 24 hours.
                // The amendment should be enabled.
                EXPECT_FALSE(votingState.enabled.empty());
                EXPECT_TRUE(votingState.majority.empty());
            }
        }
    }
}

}  // namespace xrpl::test
