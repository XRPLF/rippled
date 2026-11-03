#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <boost/container/flat_map.hpp>

#include <bitset>
#include <cstddef>
#include <map>
#include <optional>
#include <string>

/**
 * @page Feature How to add new features
 *
 * Steps required to add new features to the code:
 *
 * 1) Add the appropriate XRPL_FEATURE or XRPL_FIX macro definition for the
 *    feature to features.macro with the feature's name, `Supported::no`, and
 *    `VoteBehavior::DefaultNo`.
 *
 * 2) Use the generated variable name as the parameter to `view.rules.enabled()`
 *    to control flow into new code that this feature limits. (featureName or
 *    fixName)
 *
 * 3) If the feature development is COMPLETE, and the feature is ready to be
 *    SUPPORTED, change the macro parameter in features.macro to Supported::yes.
 *
 * 4) In general, any newly supported amendments (`Supported::yes`) should have
 *    a `VoteBehavior::DefaultNo` indefinitely so that external governance can
 *    make the decision on when to activate it. High priority bug fixes can be
 *    an exception to this rule. In such cases, ensure the fix has been
 *    clearly communicated to the community using appropriate channels,
 *    then change the macro parameter in features.macro to
 *    `VoteBehavior::DefaultYes`. The communication process is beyond
 *    the scope of these instructions.

 * 5) If a supported feature (`Supported::yes`) was _ever_ in a released
 *     version, it can never be changed back to `Supported::no`, because
 *     it _may_ still become enabled at any time. This would cause newer
 *     versions of `xrpld` to become amendment blocked.
 *     Instead, to prevent newer versions from voting on the feature, use
 *     `VoteBehavior::Obsolete`. Obsolete features can not be voted for
 *     by any versions of `xrpld` built with that setting, but will still
 *     work correctly if they get enabled. If a feature remains obsolete
 *     for long enough that _all_ clients that could vote for it are
 *     amendment blocked, the feature can be removed from the code
 *     as if it was unsupported.
 *
 *
 * When a feature has been enabled for several years, the conditional code
 * may be removed, and the feature "retired". To retire a feature:
 *
 * 1) MOVE the macro definition in features.macro to the "retired features"
 *    section at the end of the file, and change the macro to XRPL_RETIRE.
 *
 * The feature must remain registered and supported indefinitely because it
 * may exist in the Amendments object on ledger. There is no need to vote
 * for it because there's nothing to vote for. If the feature definition is
 * removed completely from the code, any instances running that code will get
 * amendment blocked. Removing the feature from the ledger is beyond the scope
 * of these instructions.
 *
 */

namespace xrpl {

// Feature names must not exceed this length (in characters, excluding the null terminator).
static constexpr std::size_t kMaxFeatureNameSize = 63;
// Reserve this exact feature-name length (in characters/bytes, excluding the null terminator)
// so that a 32-byte UInt256 (for example, in WASM or other interop contexts) can be used
// as a compact, fixed-size feature selector without conflicting with human-readable names.
static constexpr std::size_t kReservedFeatureNameSize = 32;

// Both validFeatureNameSize and validFeatureName are consteval functions that can be used in
// static_asserts to validate feature names at compile time. They are only used inside
// enforceValidFeatureName in Feature.cpp, but are exposed here for testing. The expected
// parameter `auto fn` is a constexpr lambda which returns a const char*, making it available
// for compile-time evaluation. Read more in https://accu.org/journals/overload/30/172/wu/
consteval auto
validFeatureNameSize(auto fn) -> bool
{
    constexpr char const* kN = fn();
    // Note, std::strlen is not constexpr, we need to implement our own here.
    constexpr std::size_t kLen = [](auto n) {
        std::size_t ret = 0;
        for (auto ptr = n; *ptr != '\0'; ret++, ++ptr)
            ;
        return ret;
    }(kN);
    return kLen != kReservedFeatureNameSize &&  //
        kLen <= kMaxFeatureNameSize;
}

consteval auto
validFeatureName(auto fn) -> bool
{
    constexpr char const* kN = fn();
    // Prevent the use of visually confusable characters and enforce that feature names
    // are always valid ASCII. This is needed because C++ allows Unicode identifiers.
    // Characters below 0x20 are nonprintable control characters, and characters with the 0x80 bit
    // set are non-ASCII (e.g. UTF-8 encoding of Unicode), so both are disallowed.
    for (auto ptr = kN; *ptr != '\0'; ++ptr)
    {
        if (*ptr & 0x80 || *ptr < 0x20)
            return false;
    }
    return true;
}

enum class VoteBehavior : int { Obsolete = -1, DefaultNo = 0, DefaultYes = 1 };
enum class AmendmentSupport : int { Retired = -1, Supported = 0, Unsupported = 1 };

/**
 * All amendments libxrpl knows about.
 */
std::map<std::string, AmendmentSupport> const&
allAmendments();

namespace detail {

#pragma push_macro("XRPL_FEATURE")
#undef XRPL_FEATURE
#pragma push_macro("XRPL_FIX")
#undef XRPL_FIX
#pragma push_macro("XRPL_RETIRE_FEATURE")
#undef XRPL_RETIRE_FEATURE
#pragma push_macro("XRPL_RETIRE_FIX")
#undef XRPL_RETIRE_FIX

// NOLINTBEGIN(bugprone-macro-parentheses)
#define XRPL_FEATURE(name, supported, vote) +1
#define XRPL_FIX(name, supported, vote) +1
#define XRPL_RETIRE_FEATURE(name) +1
#define XRPL_RETIRE_FIX(name) +1
// NOLINTEND(bugprone-macro-parentheses)

// This value SHOULD be equal to the number of amendments registered in
// Feature.cpp. Because it's only used to reserve storage, and determine how
// large to make the FeatureBitset, it MAY be larger. It MUST NOT be less than
// the actual number of amendments. A LogicError on startup will verify this.
static constexpr std::size_t kNumFeatures =
    (0 +
#include <xrpl/protocol/detail/features.macro>
    );

#undef XRPL_RETIRE_FEATURE
#pragma pop_macro("XRPL_RETIRE_FEATURE")
#undef XRPL_RETIRE_FIX
#pragma pop_macro("XRPL_RETIRE_FIX")
#undef XRPL_FIX
#pragma pop_macro("XRPL_FIX")
#undef XRPL_FEATURE
#pragma pop_macro("XRPL_FEATURE")

/**
 * Amendments that this server supports and the default voting behavior.
 * Whether they are enabled depends on the Rules defined in the validated
 * ledger
 */
std::map<std::string, VoteBehavior> const&
supportedAmendments();

/**
 * Amendments that this server won't vote for by default.
 *
 * This function is only used in unit tests.
 */
std::size_t
numDownVotedAmendments();

/**
 * Amendments that this server will vote for by default.
 *
 * This function is only used in unit tests.
 */
std::size_t
numUpVotedAmendments();

}  // namespace detail

std::optional<UInt256>
getRegisteredFeature(std::string const& name);

size_t
featureToBitsetIndex(UInt256 const& f);

UInt256
bitsetIndexToFeature(size_t i);

std::string
featureToName(UInt256 const& f);

class FeatureBitset : private std::bitset<detail::kNumFeatures>
{
    using Base = std::bitset<detail::kNumFeatures>;

    template <class... Fs>
    void
    initFromFeatures(UInt256 const& f, Fs&&... fs)
    {
        set(f);
        if constexpr (sizeof...(fs) > 0)
            initFromFeatures(std::forward<Fs>(fs)...);
    }

public:
    using Base::bitset;
    using Base::operator==;

    using Base::all;
    using Base::any;
    using Base::count;
    using Base::flip;
    using Base::none;
    using Base::reset;
    using Base::set;
    using Base::size;
    using Base::test;
    using Base::operator[];
    using Base::to_string;
    using Base::to_ullong;
    using Base::to_ulong;

    FeatureBitset() = default;

    explicit FeatureBitset(Base const& b) : Base(b)
    {
        XRPL_ASSERT(b.count() == count(), "xrpl::FeatureBitset::FeatureBitset(base) : count match");
    }

    template <class... Fs>
    explicit FeatureBitset(UInt256 const& f, Fs&&... fs)
    {
        initFromFeatures(f, std::forward<Fs>(fs)...);
        XRPL_ASSERT(
            count() == (sizeof...(fs) + 1),
            "xrpl::FeatureBitset::FeatureBitset(UInt256) : count and "
            "sizeof... do match");
    }

    template <class Col>
    explicit FeatureBitset(Col const& fs)
    {
        for (auto const& f : fs)
            set(featureToBitsetIndex(f));
        XRPL_ASSERT(
            fs.size() == count(),
            "xrpl::FeatureBitset::FeatureBitset(Container auto) : count and "
            "size do match");
    }

    auto
    operator[](UInt256 const& f)
    {
        return Base::operator[](featureToBitsetIndex(f));
    }

    auto
    operator[](UInt256 const& f) const
    {
        return Base::operator[](featureToBitsetIndex(f));
    }

    FeatureBitset&
    set(UInt256 const& f, bool value = true)
    {
        Base::set(featureToBitsetIndex(f), value);
        return *this;
    }

    FeatureBitset&
    reset(UInt256 const& f)
    {
        Base::reset(featureToBitsetIndex(f));
        return *this;
    }

    FeatureBitset&
    flip(UInt256 const& f)
    {
        Base::flip(featureToBitsetIndex(f));
        return *this;
    }

    FeatureBitset&
    operator&=(FeatureBitset const& rhs)
    {
        Base::operator&=(rhs);
        return *this;
    }

    FeatureBitset&
    operator|=(FeatureBitset const& rhs)
    {
        Base::operator|=(rhs);
        return *this;
    }

    FeatureBitset
    operator~() const
    {
        return FeatureBitset{Base::operator~()};
    }

    friend FeatureBitset
    operator&(FeatureBitset const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{static_cast<Base const&>(lhs) & static_cast<Base const&>(rhs)};
    }

    friend FeatureBitset
    operator&(FeatureBitset const& lhs, UInt256 const& rhs)
    {
        return lhs & FeatureBitset{rhs};
    }

    friend FeatureBitset
    operator&(UInt256 const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} & rhs;
    }

    friend FeatureBitset
    operator|(FeatureBitset const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{static_cast<Base const&>(lhs) | static_cast<Base const&>(rhs)};
    }

    friend FeatureBitset
    operator|(FeatureBitset const& lhs, UInt256 const& rhs)
    {
        return lhs | FeatureBitset{rhs};
    }

    friend FeatureBitset
    operator|(UInt256 const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} | rhs;
    }

    friend FeatureBitset
    operator^(FeatureBitset const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{static_cast<Base const&>(lhs) ^ static_cast<Base const&>(rhs)};
    }

    friend FeatureBitset
    operator^(FeatureBitset const& lhs, UInt256 const& rhs)
    {
        return lhs ^ FeatureBitset{rhs};
    }

    friend FeatureBitset
    operator^(UInt256 const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} ^ rhs;
    }

    // set difference
    friend FeatureBitset
    operator-(FeatureBitset const& lhs, FeatureBitset const& rhs)
    {
        return lhs & ~rhs;
    }

    friend FeatureBitset
    operator-(FeatureBitset const& lhs, UInt256 const& rhs)
    {
        return lhs - FeatureBitset{rhs};
    }

    friend FeatureBitset
    operator-(UInt256 const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} - rhs;
    }
};

template <class F>
void
foreachFeature(FeatureBitset bs, F&& f)
{
    for (size_t i = 0; i < bs.size(); ++i)
    {
        if (bs[i])
            f(bitsetIndexToFeature(i));
    }
}

#pragma push_macro("XRPL_FEATURE")
#undef XRPL_FEATURE
#pragma push_macro("XRPL_FIX")
#undef XRPL_FIX
#pragma push_macro("XRPL_RETIRE_FEATURE")
#undef XRPL_RETIRE_FEATURE
#pragma push_macro("XRPL_RETIRE_FIX")
#undef XRPL_RETIRE_FIX

#define XRPL_FEATURE(name, supported, vote) extern UInt256 const feature##name;
#define XRPL_FIX(name, supported, vote) extern UInt256 const fix##name;
#define XRPL_RETIRE_FEATURE(name)
#define XRPL_RETIRE_FIX(name)

#include <xrpl/protocol/detail/features.macro>

#undef XRPL_RETIRE_FEATURE
#pragma pop_macro("XRPL_RETIRE_FEATURE")
#undef XRPL_RETIRE_FIX
#pragma pop_macro("XRPL_RETIRE_FIX")
#undef XRPL_FIX
#pragma pop_macro("XRPL_FIX")
#undef XRPL_FEATURE
#pragma pop_macro("XRPL_FEATURE")

}  // namespace xrpl
