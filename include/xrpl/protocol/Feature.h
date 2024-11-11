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

#ifndef RIPPLE_PROTOCOL_FEATURE_H_INCLUDED
#define RIPPLE_PROTOCOL_FEATURE_H_INCLUDED

#include <xrpl/basics/base_uint.h>
#include <boost/container/flat_map.hpp>
#include <array>
#include <bitset>
#include <map>
#include <optional>
#include <string>

/**
 * @page Feature How to add new features
 *
 * Steps required to add new features to the code:
 *
 * 1) In this file, increment `numFeatures` and add a uint256 declaration
 *    for the feature at the bottom
 * 2) Add a uint256 definition for the feature to the corresponding source
 *    file (Feature.cpp). Use `registerFeature` to create the feature with
 *    the feature's name, `Supported::no`, and `VoteBehavior::DefaultNo`. This
 *    should be the only place the feature's name appears in code as a string.
 * 3) Use the uint256 as the parameter to `view.rules.enabled()` to
 *    control flow into new code that this feature limits.
 * 4) If the feature development is COMPLETE, and the feature is ready to be
 *    SUPPORTED, change the `registerFeature` parameter to Supported::yes.
 * 5) When the feature is ready to be ENABLED, change the `registerFeature`
 *    parameter to `VoteBehavior::DefaultYes`.
 * In general, any newly supported amendments (`Supported::yes`) should have
 * a `VoteBehavior::DefaultNo` for at least one full release cycle. High
 * priority bug fixes can be an exception to this rule of thumb.
 *
 * When a feature has been enabled for several years, the conditional code
 * may be removed, and the feature "retired". To retire a feature:
 * 1) Remove the uint256 declaration from this file.
 * 2) MOVE the uint256 definition in Feature.cpp to the "retired features"
 *    section at the end of the file.
 * 3) CHANGE the name of the variable to start with "retired".
 * 4) CHANGE the parameters of the `registerFeature` call to `Supported::yes`
 *    and `VoteBehavior::DefaultNo`.
 * The feature must remain registered and supported indefinitely because it
 * still exists in the ledger, but there is no need to vote for it because
 * there's nothing to vote for. If it is removed completely from the code, any
 * instances running that code will get amendment blocked. Removing the
 * feature from the ledger is beyond the scope of these instructions.
 *
 */

namespace ripple {

enum class VoteBehavior : int { Obsolete = -1, DefaultNo = 0, DefaultYes };
enum class AmendmentSupport : int { Retired = -1, Supported = 0, Unsupported };

/** All amendments libxrpl knows about. */
std::map<std::string, AmendmentSupport> const&
allAmendments();

namespace detail {

// This value SHOULD be equal to the number of amendments registered in
// Feature.cpp. Because it's only used to reserve storage, and determine how
// large to make the FeatureBitset, it MAY be larger. It MUST NOT be less than
// the actual number of amendments. A LogicError on startup will verify this.
static constexpr std::size_t numFeatures = 83;

/** Amendments that this server supports and the default voting behavior.
   Whether they are enabled depends on the Rules defined in the validated
   ledger */
std::map<std::string, VoteBehavior> const&
supportedAmendments();

/** Amendments that this server won't vote for by default.

    This function is only used in unit tests.
*/
std::size_t
numDownVotedAmendments();

/** Amendments that this server will vote for by default.

    This function is only used in unit tests.
*/
std::size_t
numUpVotedAmendments();

}  // namespace detail

std::optional<uint256>
getRegisteredFeature(std::string const& name);

size_t
featureToBitsetIndex(uint256 const& f);

uint256
bitsetIndexToFeature(size_t i);

std::string
featureToName(uint256 const& f);

class FeatureBitset : private std::bitset<detail::numFeatures>
{
    using base = std::bitset<detail::numFeatures>;

    template <class... Fs>
    void
    initFromFeatures(uint256 const& f, Fs&&... fs)
    {
        set(f);
        if constexpr (sizeof...(fs) > 0)
            initFromFeatures(std::forward<Fs>(fs)...);
    }

public:
    using base::bitset;
    using base::operator==;

    using base::all;
    using base::any;
    using base::count;
    using base::flip;
    using base::none;
    using base::reset;
    using base::set;
    using base::size;
    using base::test;
    using base::operator[];
    using base::to_string;
    using base::to_ullong;
    using base::to_ulong;

    FeatureBitset() = default;

    explicit FeatureBitset(base const& b) : base(b)
    {
        assert(b.count() == count());
    }

    template <class... Fs>
    explicit FeatureBitset(uint256 const& f, Fs&&... fs)
    {
        initFromFeatures(f, std::forward<Fs>(fs)...);
        assert(count() == (sizeof...(fs) + 1));
    }

    template <class Col>
    explicit FeatureBitset(Col const& fs)
    {
        for (auto const& f : fs)
            set(featureToBitsetIndex(f));
        assert(fs.size() == count());
    }

    auto
    operator[](uint256 const& f)
    {
        return base::operator[](featureToBitsetIndex(f));
    }

    auto
    operator[](uint256 const& f) const
    {
        return base::operator[](featureToBitsetIndex(f));
    }

    FeatureBitset&
    set(uint256 const& f, bool value = true)
    {
        base::set(featureToBitsetIndex(f), value);
        return *this;
    }

    FeatureBitset&
    reset(uint256 const& f)
    {
        base::reset(featureToBitsetIndex(f));
        return *this;
    }

    FeatureBitset&
    flip(uint256 const& f)
    {
        base::flip(featureToBitsetIndex(f));
        return *this;
    }

    FeatureBitset&
    operator&=(FeatureBitset const& rhs)
    {
        base::operator&=(rhs);
        return *this;
    }

    FeatureBitset&
    operator|=(FeatureBitset const& rhs)
    {
        base::operator|=(rhs);
        return *this;
    }

    FeatureBitset
    operator~() const
    {
        return FeatureBitset{base::operator~()};
    }

    friend FeatureBitset
    operator&(FeatureBitset const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{
            static_cast<base const&>(lhs) & static_cast<base const&>(rhs)};
    }

    friend FeatureBitset
    operator&(FeatureBitset const& lhs, uint256 const& rhs)
    {
        return lhs & FeatureBitset{rhs};
    }

    friend FeatureBitset
    operator&(uint256 const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} & rhs;
    }

    friend FeatureBitset
    operator|(FeatureBitset const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{
            static_cast<base const&>(lhs) | static_cast<base const&>(rhs)};
    }

    friend FeatureBitset
    operator|(FeatureBitset const& lhs, uint256 const& rhs)
    {
        return lhs | FeatureBitset{rhs};
    }

    friend FeatureBitset
    operator|(uint256 const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} | rhs;
    }

    friend FeatureBitset
    operator^(FeatureBitset const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{
            static_cast<base const&>(lhs) ^ static_cast<base const&>(rhs)};
    }

    friend FeatureBitset
    operator^(FeatureBitset const& lhs, uint256 const& rhs)
    {
        return lhs ^ FeatureBitset { rhs };
    }

    friend FeatureBitset
    operator^(uint256 const& lhs, FeatureBitset const& rhs)
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
    operator-(FeatureBitset const& lhs, uint256 const& rhs)
    {
        return lhs - FeatureBitset{rhs};
    }

    friend FeatureBitset
    operator-(uint256 const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} - rhs;
    }
};

template <class F>
void
foreachFeature(FeatureBitset bs, F&& f)
{
    for (size_t i = 0; i < bs.size(); ++i)
        if (bs[i])
            f(bitsetIndexToFeature(i));
}

#pragma push_macro("XRPL_FEATURE")
#undef XRPL_FEATURE
#pragma push_macro("XRPL_FIX")
#undef XRPL_FIX

#define XRPL_FEATURE(name, supported, vote) extern uint256 const feature##name;
#define XRPL_FIX(name, supported, vote) extern uint256 const fix##name;

#include <xrpl/protocol/detail/features.macro>

#undef XRPL_FIX
#pragma pop_macro("XRPL_FIX")
#undef XRPL_FEATURE
#pragma pop_macro("XRPL_FEATURE")

}  // namespace ripple

#endif
