//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/ledger/CashDiff.h>
#include <ripple/ledger/detail/ApplyStateTable.h>
#include <ripple/protocol/st.h>
#include <boost/container/static_vector.hpp>
#include <cassert>
#include <cstdlib>                                  // std::abs()

namespace ripple {
namespace detail {

// Data structure that summarize cash changes in a single ApplyStateTable.
struct CashSummary
{
    explicit CashSummary() = default;

    // Sorted vectors.  All of the vectors fill in for std::maps.
    std::vector<std::pair<
        AccountID, XRPAmount>> xrpChanges;

    std::vector<std::pair<
        std::tuple<AccountID, AccountID, Currency>, STAmount>> trustChanges;

    std::vector<std::pair<
        std::tuple<AccountID, AccountID, Currency>, bool>> trustDeletions;

    std::vector<std::pair<
        std::tuple<AccountID, std::uint32_t>,
        CashDiff::OfferAmounts>> offerChanges;

    // Note that the OfferAmounts hold the amount *prior* to deletion.
    std::vector<std::pair<
        std::tuple<AccountID, std::uint32_t>,
        CashDiff::OfferAmounts>> offerDeletions;

    bool hasDiff () const
    {
        return !xrpChanges.empty()
            || !trustChanges.empty()
            || !trustDeletions.empty()
            || !offerChanges.empty()
            || !offerDeletions.empty();
    }

    void reserve (size_t newCap)
    {
        xrpChanges.reserve (newCap);
        trustChanges.reserve (newCap);
        trustDeletions.reserve (newCap);
        offerChanges.reserve (newCap);
        offerDeletions.reserve (newCap);
    }

    void shrink_to_fit()
    {
        xrpChanges.shrink_to_fit();
        trustChanges.shrink_to_fit();
        trustDeletions.shrink_to_fit();
        offerChanges.shrink_to_fit();
        offerDeletions.shrink_to_fit();
    }

    void sort()
    {
        std::sort (xrpChanges.begin(), xrpChanges.end());
        std::sort (trustChanges.begin(), trustChanges.end());
        std::sort (trustDeletions.begin(), trustDeletions.end());
        std::sort (offerChanges.begin(), offerChanges.end());
        std::sort (offerDeletions.begin(), offerDeletions.end());
    }
};

// treatZeroOfferAsDeletion()
//
// Older payment code might set an Offer's TakerPays and TakerGets to
// zero and let the offer be cleaned up later.  A more recent version
// may be more proactive about removing offers.  We attempt to paper
// over that difference here.
//
// Two conditions are checked:
//
//  o A modified Offer with both TakerPays and TakerGets set to zero is
//    added to offerDeletions (not offerChanges).
//
//  o Any deleted offer that was zero before deletion is ignored.  It will
//    have been treated as deleted when the offer was first set to zero.
//
// The returned bool indicates whether the passed in data was handled.
// This allows the caller to avoid further handling.
static bool treatZeroOfferAsDeletion (CashSummary& result, bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (!before)
        return false;

    auto const& prev = *before;

    if (isDelete)
    {
        if (prev.getType() == ltOFFER &&
            prev[sfTakerPays] == zero && prev[sfTakerGets] == zero)
        {
            // Offer was previously treated as deleted when it was zeroed.
            return true;
        }
    }
    else
    {
        // modify
        if (!after)
            return false;

        auto const& cur = *after;
        if (cur.getType() == ltOFFER &&
            cur[sfTakerPays] == zero && cur[sfTakerGets] == zero)
        {
            // Either ignore or treat as delete.
            auto const oldTakerPays = prev[sfTakerPays];
            auto const oldTakerGets = prev[sfTakerGets];
            if (oldTakerPays != zero && oldTakerGets != zero)
            {
                result.offerDeletions.push_back (
                    std::make_pair (
                        std::make_tuple (prev[sfAccount], prev[sfSequence]),
                    CashDiff::OfferAmounts {{oldTakerPays, oldTakerGets}}));
                return true;
            }
        }
    }
    return false;
}

static bool getBasicCashFlow (CashSummary& result, bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (isDelete)
    {
        if (!before)
            return false;

        auto const& prev = *before;
        switch(prev.getType())
        {
        case ltACCOUNT_ROOT:
            result.xrpChanges.push_back (
                std::make_pair (prev[sfAccount], XRPAmount {0}));
            return true;

        case ltRIPPLE_STATE:
            result.trustDeletions.push_back(
                std::make_pair (
                    std::make_tuple (
                        prev[sfLowLimit].getIssuer(),
                        prev[sfHighLimit].getIssuer(),
                        prev[sfBalance].getCurrency()), false));
            return true;

        case ltOFFER:
            result.offerDeletions.push_back (
                std::make_pair (
                    std::make_tuple (prev[sfAccount], prev[sfSequence]),
                CashDiff::OfferAmounts {{
                    prev[sfTakerPays],
                    prev[sfTakerGets]}}));
            return true;

        default:
            return false;
        }
    }
    else
    {
        // insert or modify
        if (!after)
        {
            assert (after);
            return false;
        }

        auto const& cur = *after;
        switch(cur.getType())
        {
        case ltACCOUNT_ROOT:
        {
            auto const curXrp = cur[sfBalance].xrp();
            if (!before || (*before)[sfBalance].xrp() != curXrp)
                result.xrpChanges.push_back (
                    std::make_pair (cur[sfAccount], curXrp));
            return true;
        }
        case ltRIPPLE_STATE:
        {
            auto const curBalance = cur[sfBalance];
            if (!before || (*before)[sfBalance] != curBalance)
                result.trustChanges.push_back (
                    std::make_pair (
                        std::make_tuple (
                            cur[sfLowLimit].getIssuer(),
                            cur[sfHighLimit].getIssuer(),
                            curBalance.getCurrency()),
                        curBalance));
            return true;
        }
        case ltOFFER:
        {
            auto const curTakerPays = cur[sfTakerPays];
            auto const curTakerGets = cur[sfTakerGets];
            if (!before || (*before)[sfTakerGets] != curTakerGets ||
                (*before)[sfTakerPays] != curTakerPays)
            {
                result.offerChanges.push_back (
                    std::make_pair (
                        std::make_tuple (cur[sfAccount], cur[sfSequence]),
                    CashDiff::OfferAmounts {{curTakerPays, curTakerGets}}));
            }
            return true;
        }
        default:
            break;
        }
    }
    return false;
}

// Extract the final cash state from an ApplyStateTable.
static CashSummary
getCashFlow (ReadView const& view, CashFilter f, ApplyStateTable const& table)
{
    CashSummary result;
    result.reserve (table.size());

    // Make a container of filters based on the passed in filter flags.
    using FuncType = decltype (&getBasicCashFlow);
    boost::container::static_vector<FuncType, 2> filters;

    if ((f & CashFilter::treatZeroOfferAsDeletion) != CashFilter::none)
        filters.push_back (treatZeroOfferAsDeletion);

    filters.push_back (&getBasicCashFlow);

    auto each = [&result, &filters](uint256 const& key, bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) {

        std::find_if (filters.begin(), filters.end(),
            [&result, isDelete, &before, &after] (FuncType func) {
                return func (result, isDelete, before, after);
        });
    };

    table.visit (view, each);
    result.sort();
    result.shrink_to_fit();
    return result;
}

} // detail

//------------------------------------------------------------------------------

// Holds all of the CashDiff-related data.
class CashDiff::Impl
{
private:
    // Note differences in destroyed XRP between two ApplyStateTables.
    struct DropsGone
    {
        XRPAmount lhs;
        XRPAmount rhs;
    };

    ReadView const& view_;

    std::size_t commonKeys_ = 0;  // Number of keys common to both rhs and lhs.
    std::size_t lhsKeys_ = 0;     // Number of keys in lhs but not rhs.
    std::size_t rhsKeys_ = 0;     // Number of keys in rhs but not lhs.
    boost::optional<DropsGone> dropsGone_;
    detail::CashSummary lhsDiffs_;
    detail::CashSummary rhsDiffs_;

public:
    // Constructor.
    Impl (ReadView const& view,
          CashFilter lhsFilter, detail::ApplyStateTable const& lhs,
          CashFilter rhsFilter, detail::ApplyStateTable const& rhs)
    : view_ (view)
    {
        findDiffs (lhsFilter, lhs, rhsFilter, rhs);
    }

    std::size_t commonCount() const
    {
        return commonKeys_;
    }

    std::size_t lhsOnlyCount() const
    {
        return lhsKeys_;
    }

    std::size_t rhsOnlyCount () const
    {
        return rhsKeys_;
    }

    bool hasDiff () const
    {
        return dropsGone_ != boost::none
            || lhsDiffs_.hasDiff()
            || rhsDiffs_.hasDiff();
    }

    // Filter out differences that are small enough to be in the floating
    // point noise.
    bool rmDust ();

    // Remove offer deletion differences from a given side
    bool rmLhsDeletedOffers();
    bool rmRhsDeletedOffers();

private:
    void findDiffs (
        CashFilter lhsFilter, detail::ApplyStateTable const& lhs,
        CashFilter rhsFilter, detail::ApplyStateTable const& rhs);
};

// Template function to count difference types in individual CashDiff vectors.
// Assumes those vectors are sorted.
//
// Returned array:
//  [0] count of keys present in both vectors.
//  [1] count of keys present in lhs only.
//  [2] count of keys present in rhs only.
template <typename T, typename U>
static std::array<std::size_t, 3> countKeys (
    std::vector<std::pair<T, U>> const& lhs,
    std::vector<std::pair<T, U>> const& rhs)
{
    std::array<std::size_t, 3> ret {};  // Zero initialize;

    auto lhsItr = lhs.cbegin();
    auto rhsItr = rhs.cbegin();

    while (lhsItr != lhs.cend() || rhsItr != rhs.cend())
    {
        if (lhsItr == lhs.cend())
        {
            // rhs has an entry that is not in lhs.
            ret[2] += 1;
            ++rhsItr;
        }
        else if (rhsItr == rhs.cend())
        {
            // lhs has an entry that is not in rhs.
            ret[1] += 1;
            ++lhsItr;
        }
        else if (lhsItr->first < rhsItr->first)
        {
            // This key is only in lhs.
            ret[1] += 1;
            ++lhsItr;
        }
        else if (rhsItr->first < lhsItr->first)
        {
            // This key is only in rhs.
            ret[2] += 1;
            ++rhsItr;
        }
        else
        {
            // The equivalent key is present in both vectors.
            ret[0] += 1;
            ++lhsItr;
            ++rhsItr;
        }
    }
    return ret;
}

// Given two CashSummary instances, count the keys.  Assumes both
// CashSummaries have sorted entries.
//
// Returned array:
//  [0] count of keys present in both vectors.
//  [1] count of keys present in lhs only.
//  [2] count of keys present in rhs only.
static std::array<std::size_t, 3>
countKeys (detail::CashSummary const& lhs, detail::CashSummary const& rhs)
{
    std::array<std::size_t, 3> ret {};  // Zero initialize;

    // Lambda to add in new results.
    auto addIn = [&ret] (std::array<std::size_t, 3> const& a)
    {
        std::transform (a.cbegin(), a.cend(),
            ret.cbegin(), ret.begin(), std::plus<std::size_t>());
    };
    addIn (countKeys(lhs.xrpChanges,     rhs.xrpChanges));
    addIn (countKeys(lhs.trustChanges,   rhs.trustChanges));
    addIn (countKeys(lhs.trustDeletions, rhs.trustDeletions));
    addIn (countKeys(lhs.offerChanges,   rhs.offerChanges));
    addIn (countKeys(lhs.offerDeletions, rhs.offerDeletions));
    return ret;
}

// Function that compares two CashDiff::OfferAmounts and returns true if
// the difference is dust-sized.
static bool diffIsDust (
    CashDiff::OfferAmounts const& lhs, CashDiff::OfferAmounts const& rhs)
{
    for (auto i = 0; i < lhs.count(); ++i)
    {
        if (!diffIsDust (lhs[i], rhs[i]))
            return false;
    }
    return true;
}

// Template function to remove dust from individual CashDiff vectors.
template <typename T, typename U, typename L>
static bool
rmVecDust (
    std::vector<std::pair<T, U>>& lhs,
    std::vector<std::pair<T, U>>& rhs,
    L&& justDust)
{
    static_assert (
        std::is_same<bool,
        decltype (justDust (lhs[0].second, rhs[0].second))>::value,
        "Invalid lambda passed to rmVecDust");

    bool dustWasRemoved = false;
    auto lhsItr = lhs.begin();
    while (lhsItr != lhs.end())
    {
        using value_t = std::pair<T, U>;
        auto const found = std::equal_range (rhs.begin(), rhs.end(), *lhsItr,
            [] (value_t const& a, value_t const& b)
            {
                return a.first < b.first;
            });

        if (found.first != found.second)
        {
            // The same entry changed for both lhs and rhs.  Check whether
            // the differences are small enough to be removed.
            if (justDust (lhsItr->second, found.first->second))
            {
                dustWasRemoved = true;
                rhs.erase (found.first);
                // Dodge an invalid iterator by using erase's return value.
                lhsItr = lhs.erase (lhsItr);
                continue;
            }
        }
        ++lhsItr;
    }
    return dustWasRemoved;
}

bool CashDiff::Impl::rmDust ()
{
    bool removedDust = false;

    // Four of the containers can have small (floating point style)
    // amount differences: xrpChanges, trustChanges, offerChanges, and
    // offerDeletions.  Rifle through those containers and remove any
    // entries that are _almost_ the same between lhs and rhs.

    // xrpChanges.  We call a difference of 2 drops or less dust.
    removedDust |= rmVecDust (lhsDiffs_.xrpChanges, rhsDiffs_.xrpChanges,
        [](XRPAmount const& lhs, XRPAmount const& rhs)
        {
            return diffIsDust (lhs, rhs);
        });

    // trustChanges.
    removedDust |= rmVecDust (lhsDiffs_.trustChanges, rhsDiffs_.trustChanges,
        [](STAmount const& lhs, STAmount const& rhs)
        {
            return diffIsDust (lhs, rhs);
        });

    // offerChanges.
    removedDust |= rmVecDust (lhsDiffs_.offerChanges, rhsDiffs_.offerChanges,
        [](CashDiff::OfferAmounts const& lhs, CashDiff::OfferAmounts const& rhs)
        {
            return diffIsDust (lhs, rhs);
        });

    // offerDeletions.
    removedDust |= rmVecDust (
        lhsDiffs_.offerDeletions, rhsDiffs_.offerDeletions,
        [](CashDiff::OfferAmounts const& lhs, CashDiff::OfferAmounts const& rhs)
        {
            return diffIsDust (lhs, rhs);
        });

    return removedDust;
}

bool CashDiff::Impl::rmLhsDeletedOffers()
{
    bool const ret = !lhsDiffs_.offerDeletions.empty();
    if (ret)
        lhsDiffs_.offerDeletions.clear();
    return ret;
}

bool CashDiff::Impl::rmRhsDeletedOffers()
{
    bool const ret = !rhsDiffs_.offerDeletions.empty();
    if (ret)
        rhsDiffs_.offerDeletions.clear();
    return ret;
}

// Deposits differences between two sorted vectors into a destination.
template <typename T>
static void setDiff (
    std::vector<T> const& a, std::vector<T> const& b, std::vector<T>& dest)
{
    dest.clear();
    std::set_difference (a.cbegin(), a.cend(),
        b.cbegin(), b.cend(), std::inserter (dest, dest.end()));
}

void CashDiff::Impl::findDiffs (
        CashFilter lhsFilter, detail::ApplyStateTable const& lhs,
        CashFilter rhsFilter, detail::ApplyStateTable const& rhs)
{
    // If dropsDestroyed_ is different, note that.
    if (lhs.dropsDestroyed() != rhs.dropsDestroyed())
    {
        dropsGone_ = DropsGone{lhs.dropsDestroyed(), rhs.dropsDestroyed()};
    }

    // Extract cash flow changes from the state tables
    auto lhsDiffs = getCashFlow (view_, lhsFilter, lhs);
    auto rhsDiffs = getCashFlow (view_, rhsFilter, rhs);

    // Get statistics on keys.
    auto const counts = countKeys (lhsDiffs, rhsDiffs);
    commonKeys_ = counts[0];
    lhsKeys_    = counts[1];
    rhsKeys_    = counts[2];

    // Save only the differences between the results.
    // xrpChanges:
    setDiff (lhsDiffs.xrpChanges, rhsDiffs.xrpChanges, lhsDiffs_.xrpChanges);
    setDiff (rhsDiffs.xrpChanges, lhsDiffs.xrpChanges, rhsDiffs_.xrpChanges);

    // trustChanges:
    setDiff (lhsDiffs.trustChanges, rhsDiffs.trustChanges, lhsDiffs_.trustChanges);
    setDiff (rhsDiffs.trustChanges, lhsDiffs.trustChanges, rhsDiffs_.trustChanges);

    // trustDeletions:
    setDiff (lhsDiffs.trustDeletions, rhsDiffs.trustDeletions, lhsDiffs_.trustDeletions);
    setDiff (rhsDiffs.trustDeletions, lhsDiffs.trustDeletions, rhsDiffs_.trustDeletions);

    // offerChanges:
    setDiff (lhsDiffs.offerChanges, rhsDiffs.offerChanges, lhsDiffs_.offerChanges);
    setDiff (rhsDiffs.offerChanges, lhsDiffs.offerChanges, rhsDiffs_.offerChanges);

    // offerDeletions:
    setDiff (lhsDiffs.offerDeletions, rhsDiffs.offerDeletions, lhsDiffs_.offerDeletions);
    setDiff (rhsDiffs.offerDeletions, lhsDiffs.offerDeletions, rhsDiffs_.offerDeletions);
}

//------------------------------------------------------------------------------

// Locates differences between two ApplyStateTables.
CashDiff::CashDiff (CashDiff&& other)
: impl_ (std::move (other.impl_))
{
}

CashDiff::~CashDiff()
{
}

CashDiff::CashDiff (ReadView const& view,
    CashFilter lhsFilter, detail::ApplyStateTable const& lhs,
    CashFilter rhsFilter, detail::ApplyStateTable const& rhs)
: impl_ (new Impl (view, lhsFilter, lhs, rhsFilter, rhs))
{
}

std::size_t CashDiff::commonCount () const
{
    return impl_->commonCount();
}

std::size_t CashDiff::rhsOnlyCount () const
{
    return impl_->rhsOnlyCount();
}

std::size_t CashDiff::lhsOnlyCount () const
{
    return impl_->lhsOnlyCount();
}

bool CashDiff::hasDiff() const
{
    return impl_->hasDiff();
}

bool CashDiff::rmDust()
{
    return impl_->rmDust();
}

bool CashDiff::rmLhsDeletedOffers()
{
    return impl_->rmLhsDeletedOffers();
}

bool CashDiff::rmRhsDeletedOffers()
{
    return impl_->rmRhsDeletedOffers();
}

//------------------------------------------------------------------------------

// Function that compares two STAmounts and returns true if the difference
// is dust-sized.
bool diffIsDust (STAmount const& v1, STAmount const& v2, std::uint8_t e10)
{
    // If one value is positive and the other negative then there's something
    // odd afoot.
    if (v1 != zero && v2 != zero && (v1.negative() != v2.negative()))
        return false;

    // v1 and v2 must be the same Issue for their difference to make sense.
    if (v1.native() != v2.native())
        return false;

    if (!v1.native() && (v1.issue() != v2.issue()))
        return false;

    // If v1 == v2 then the dust is vanishingly small.
    if (v1 == v2)
        return true;

    STAmount const& small = v1 < v2 ? v1 : v2;
    STAmount const& large = v1 < v2 ? v2 : v1;

    // Handling XRP is different from IOU.
    if (v1.native())
    {
        std::uint64_t const s = small.mantissa();
        std::uint64_t const l = large.mantissa();

        // Always allow a couple of drops of noise.
        if (l - s <= 2)
            return true;

        static_assert (sizeof (1ULL) == sizeof (std::uint64_t), "");
        std::uint64_t ratio = s / (l - s);
        static constexpr std::uint64_t e10Lookup[]
        {
                                     1ULL,
                                    10ULL,
                                   100ULL,
                                 1'000ULL,
                                10'000ULL,
                               100'000ULL,
                             1'000'000ULL,
                            10'000'000ULL,
                           100'000'000ULL,
                         1'000'000'000ULL,
                        10'000'000'000ULL,
                       100'000'000'000ULL,
                     1'000'000'000'000ULL,
                    10'000'000'000'000ULL,
                   100'000'000'000'000ULL,
                 1'000'000'000'000'000ULL,
                10'000'000'000'000'000ULL,
               100'000'000'000'000'000ULL,
             1'000'000'000'000'000'000ULL,
            10'000'000'000'000'000'000ULL,
        };
        static std::size_t constexpr maxIndex =
            sizeof (e10Lookup) / sizeof e10Lookup[0];

        // Make sure the table is big enough.
        static_assert (
            std::numeric_limits<std::uint64_t>::max() /
            e10Lookup[maxIndex - 1] < 10, "Table too small");

        if (e10 >= maxIndex)
            return false;

        return ratio >= e10Lookup[e10];
    }

    // Non-native.  Note that even though large and small may not be equal,
    // their difference may be zero.  One way that can happen is if two
    // values are different, but their difference results in an STAmount
    // with an exponent less than -96.
    STAmount const diff = large - small;
    if (diff == zero)
        return true;

    STAmount const ratio = divide (small, diff, v1.issue());
    STAmount const one (v1.issue(), 1);
    int const ratioExp = ratio.exponent() - one.exponent();

    return ratioExp >= e10;
};

} // ripple
