#include <xrpl/protocol/Feature.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/digest.h>

#include <boost/container_hash/hash.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/indexed_by.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/tag.hpp>
#include <boost/multi_index_container.hpp>

#include <atomic>
#include <cstddef>
#include <map>
#include <optional>
#include <string>

namespace xrpl {

inline std::size_t
// NOLINTNEXTLINE(readability-identifier-naming)
hash_value(xrpl::uint256 const& feature)
{
    std::size_t seed = 0;
    using namespace boost;
    for (auto const& n : feature)
        hash_combine(seed, n);
    return seed;
}

namespace {

enum class Supported : bool { No = false, Yes };

// *NOTE*
//
// Features, or Amendments as they are called elsewhere, are enabled on the
// network at some specific time based on Validator voting.  Features are
// enabled using run-time conditionals based on the state of the amendment.
// There is value in retaining that conditional code for some time after
// the amendment is enabled to make it simple to replay old transactions.
// However, once an amendment has been enabled for, say, more than two years
// then retaining that conditional code has less value since it is
// uncommon to replay such old transactions.
//
// Starting in January of 2020 Amendment conditionals from before January
// 2018 are being removed.  So replaying any ledger from before January
// 2018 needs to happen on an older version of the server code.  There's
// a log message in Application.cpp that warns about replaying old ledgers.
//
// At some point in the future someone may wish to remove amendment
// conditional code for amendments that were enabled after January 2018.
// When that happens then the log message in Application.cpp should be
// updated.
//
// Generally, amendments which introduce new features should be set as
// "VoteBehavior::DefaultNo" whereas in rare cases, amendments that fix
// critical bugs should be set as "VoteBehavior::DefaultYes", if off-chain
// consensus is reached amongst reviewers, validator operators, and other
// participants.

class FeatureCollections
{
    struct Feature
    {
        std::string name;
        uint256 feature;

        Feature() = delete;
        explicit Feature(std::string name, uint256 const& feature)
            : name(std::move(name)), feature(feature)
        {
        }

        // These structs are used by the `features` multi_index_container to
        // provide access to the features collection by size_t index, string
        // name, and uint256 feature identifier
        struct ByIndex
        {
        };
        struct ByName
        {
        };
        struct ByFeature
        {
        };
    };

    // Intermediate types to help with readability
    template <class Tag, typename Type, Type Feature::* PtrToMember>
    using feature_hashed_unique = boost::multi_index::hashed_unique<
        boost::multi_index::tag<Tag>,
        boost::multi_index::member<Feature, Type, PtrToMember>>;

    // Intermediate types to help with readability
    using feature_indexing = boost::multi_index::indexed_by<
        boost::multi_index::random_access<boost::multi_index::tag<Feature::ByIndex>>,
        feature_hashed_unique<Feature::ByFeature, uint256, &Feature::feature>,
        feature_hashed_unique<Feature::ByName, std::string, &Feature::name>>;

    // This multi_index_container provides access to the features collection by
    // name, index, and uint256 feature identifier
    boost::multi_index::multi_index_container<Feature, feature_indexing> features_;
    std::map<std::string, AmendmentSupport> all_;
    std::map<std::string, VoteBehavior> supported_;
    std::size_t upVotes_ = 0;
    std::size_t downVotes_ = 0;
    mutable std::atomic<bool> readOnly_ = false;

    // These helper functions provide access to the features collection by name,
    // index, and uint256 feature identifier, so the details of
    // multi_index_container can be hidden
    Feature const&
    getByIndex(size_t i) const
    {
        if (i >= features_.size())
            logicError("Invalid FeatureBitset index");
        auto const& sequence = features_.get<Feature::ByIndex>();
        return sequence[i];
    }
    size_t
    getIndex(Feature const& feature) const
    {
        auto const& sequence = features_.get<Feature::ByIndex>();
        auto const itTo = sequence.iterator_to(feature);
        return itTo - sequence.begin();
    }
    Feature const*
    getByFeature(uint256 const& feature) const
    {
        auto const& featureIndex = features_.get<Feature::ByFeature>();
        auto const featureIt = featureIndex.find(feature);
        return featureIt == featureIndex.end() ? nullptr : &*featureIt;
    }
    Feature const*
    getByName(std::string const& name) const
    {
        auto const& nameIndex = features_.get<Feature::ByName>();
        auto const nameIt = nameIndex.find(name);
        return nameIt == nameIndex.end() ? nullptr : &*nameIt;
    }

public:
    FeatureCollections();

    std::optional<uint256>
    getRegisteredFeature(std::string const& name) const;

    uint256
    registerFeature(std::string const& name, Supported support, VoteBehavior vote);

    /** Tell FeatureCollections when registration is complete. */
    bool
    registrationIsDone();

    std::size_t
    featureToBitsetIndex(uint256 const& f) const;

    uint256 const&
    bitsetIndexToFeature(size_t i) const;

    std::string
    featureToName(uint256 const& f) const;

    /** All amendments that are registered within the table. */
    std::map<std::string, AmendmentSupport> const&
    allAmendments() const
    {
        return all_;
    }

    /** Amendments that this server supports.
    Whether they are enabled depends on the Rules defined in the validated
    ledger */
    std::map<std::string, VoteBehavior> const&
    supportedAmendments() const
    {
        return supported_;
    }

    /** Amendments that this server WON'T vote for by default. */
    std::size_t
    numDownVotedAmendments() const
    {
        return downVotes_;
    }

    /** Amendments that this server WILL vote for by default. */
    std::size_t
    numUpVotedAmendments() const
    {
        return upVotes_;
    }
};

//------------------------------------------------------------------------------

FeatureCollections::FeatureCollections()
{
    features_.reserve(xrpl::detail::kNumFeatures);
}

std::optional<uint256>
FeatureCollections::getRegisteredFeature(std::string const& name) const
{
    XRPL_ASSERT(
        readOnly_.load(), "xrpl::FeatureCollections::getRegisteredFeature : startup completed");
    Feature const* feature = getByName(name);
    if (feature != nullptr)
        return feature->feature;
    return std::nullopt;
}

void
check(bool condition, char const* logicErrorMessage)
{
    if (!condition)
        logicError(logicErrorMessage);
}

uint256
FeatureCollections::registerFeature(std::string const& name, Supported support, VoteBehavior vote)
{
    check(!readOnly_, "Attempting to register a feature after startup.");
    check(
        support == Supported::Yes || vote == VoteBehavior::DefaultNo,
        "Invalid feature parameters. Must be supported to be up-voted.");
    Feature const* i = getByName(name);
    if (i == nullptr)
    {
        check(features_.size() < detail::kNumFeatures, "More features defined than allocated.");

        auto const f = sha512Half(Slice(name.data(), name.size()));

        features_.emplace_back(name, f);

        auto const getAmendmentSupport = [=]() {
            if (vote == VoteBehavior::Obsolete)
                return AmendmentSupport::Retired;
            return support == Supported::Yes ? AmendmentSupport::Supported
                                             : AmendmentSupport::Unsupported;
        };
        all_.emplace(name, getAmendmentSupport());

        if (support == Supported::Yes)
        {
            supported_.emplace(name, vote);

            if (vote == VoteBehavior::DefaultYes)
            {
                ++upVotes_;
            }
            else
            {
                ++downVotes_;
            }
        }
        check(upVotes_ + downVotes_ == supported_.size(), "Feature counting logic broke");
        check(
            supported_.size() <= features_.size(), "More supported features than defined features");
        check(features_.size() == all_.size(), "The 'all' features list is populated incorrectly");
        return f;
    }

    // Each feature should only be registered once
    logicError("Duplicate feature registration");
}

/** Tell FeatureCollections when registration is complete. */
bool
FeatureCollections::registrationIsDone()
{
    readOnly_ = true;
    return true;
}

size_t
FeatureCollections::featureToBitsetIndex(uint256 const& f) const
{
    XRPL_ASSERT(
        readOnly_.load(), "xrpl::FeatureCollections::featureToBitsetIndex : startup completed");

    Feature const* feature = getByFeature(f);
    if (feature == nullptr)
        logicError("Invalid Feature ID");

    return getIndex(*feature);
}

uint256 const&
FeatureCollections::bitsetIndexToFeature(size_t i) const
{
    XRPL_ASSERT(
        readOnly_.load(), "xrpl::FeatureCollections::bitsetIndexToFeature : startup completed");
    Feature const& feature = getByIndex(i);
    return feature.feature;
}

std::string
FeatureCollections::featureToName(uint256 const& f) const
{
    XRPL_ASSERT(readOnly_.load(), "xrpl::FeatureCollections::featureToName : startup completed");
    Feature const* feature = getByFeature(f);
    return (feature != nullptr) ? feature->name : to_string(f);
}

FeatureCollections gFeatureCollections;

}  // namespace

/** All amendments libxrpl knows of. */
std::map<std::string, AmendmentSupport> const&
allAmendments()
{
    return gFeatureCollections.allAmendments();
}

/** Amendments that this server supports.
   Whether they are enabled depends on the Rules defined in the validated
   ledger */
std::map<std::string, VoteBehavior> const&
detail::supportedAmendments()
{
    return gFeatureCollections.supportedAmendments();
}

/** Amendments that this server won't vote for by default. */
std::size_t
detail::numDownVotedAmendments()
{
    return gFeatureCollections.numDownVotedAmendments();
}

/** Amendments that this server will vote for by default. */
std::size_t
detail::numUpVotedAmendments()
{
    return gFeatureCollections.numUpVotedAmendments();
}

//------------------------------------------------------------------------------

std::optional<uint256>
getRegisteredFeature(std::string const& name)
{
    return gFeatureCollections.getRegisteredFeature(name);
}

uint256
registerFeature(std::string const& name, Supported support, VoteBehavior vote)
{
    return gFeatureCollections.registerFeature(name, support, vote);
}

// Retired features are in the ledger and have no code controlled by the
// feature. They need to be supported, but do not need to be voted on.
uint256
retireFeature(std::string const& name)
{
    return registerFeature(name, Supported::Yes, VoteBehavior::Obsolete);
}

/** Tell FeatureCollections when registration is complete. */
bool
registrationIsDone()
{
    return gFeatureCollections.registrationIsDone();
}

size_t
featureToBitsetIndex(uint256 const& f)
{
    return gFeatureCollections.featureToBitsetIndex(f);
}

uint256
bitsetIndexToFeature(size_t i)
{
    return gFeatureCollections.bitsetIndexToFeature(i);
}

std::string
featureToName(uint256 const& f)
{
    return gFeatureCollections.featureToName(f);
}

// All known amendments must be registered either here or below with the
// "retired" amendments

#pragma push_macro("XRPL_FEATURE")
#undef XRPL_FEATURE
#pragma push_macro("XRPL_FIX")
#undef XRPL_FIX
#pragma push_macro("XRPL_RETIRE_FEATURE")
#undef XRPL_RETIRE_FEATURE
#pragma push_macro("XRPL_RETIRE_FIX")
#undef XRPL_RETIRE_FIX

consteval auto
enforceValidFeatureName(auto fn) -> char const*
{
    static_assert(validFeatureName(fn), "Invalid feature name");
    static_assert(validFeatureNameSize(fn), "Invalid feature name size");
    return fn();
}

#define XRPL_FEATURE(name, supported, vote) \
    uint256 const feature##name =           \
        registerFeature(enforceValidFeatureName([] { return #name; }), supported, vote);
#define XRPL_FIX(name, supported, vote) \
    uint256 const fix##name =           \
        registerFeature(enforceValidFeatureName([] { return "fix" #name; }), supported, vote);

// clang-format off
#define XRPL_RETIRE_FEATURE(name)                                       \
    [[deprecated("The referenced feature amendment has been retired")]] \
    [[maybe_unused]]                                                    \
    uint256 const retiredFeature##name = retireFeature(#name);

#define XRPL_RETIRE_FIX(name)                                           \
    [[deprecated("The referenced fix amendment has been retired")]]     \
    [[maybe_unused]]                                                    \
    uint256 const retiredFix##name = retireFeature("fix" #name);
// clang-format on

#include <xrpl/protocol/detail/features.macro>

#include <utility>

#undef XRPL_RETIRE_FEATURE
#pragma pop_macro("XRPL_RETIRE_FEATURE")
#undef XRPL_RETIRE_FIX
#pragma pop_macro("XRPL_RETIRE_FIX")
#undef XRPL_FIX
#pragma pop_macro("XRPL_FIX")
#undef XRPL_FEATURE
#pragma pop_macro("XRPL_FEATURE")

// All of the features should now be registered, since variables in a cpp file
// are initialized from top to bottom.
//
// Use initialization of one final static variable to set featureCollections::readOnly_.
[[maybe_unused]] static bool const kReadOnlySet = gFeatureCollections.registrationIsDone();

}  // namespace xrpl
