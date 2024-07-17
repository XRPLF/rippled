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

#ifndef RIPPLE_APP_PATHS_IMPL_PAYSTEPS_H_INCLUDED
#define RIPPLE_APP_PATHS_IMPL_PAYSTEPS_H_INCLUDED

#include <ripple/app/paths/impl/AmountSpec.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/QualityFunction.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/TER.h>

#include <boost/container/flat_set.hpp>
#include <optional>

namespace ripple {
class PaymentSandbox;
class ReadView;
class ApplyView;
class AMMContext;

enum class DebtDirection { issues, redeems };
enum class QualityDirection { in, out };
enum class StrandDirection { forward, reverse };
enum OfferCrossing { no = 0, yes = 1, sell = 2 };

inline bool
redeems(DebtDirection dir)
{
    return dir == DebtDirection::redeems;
}

inline bool
issues(DebtDirection dir)
{
    return dir == DebtDirection::issues;
}

/**
   A step in a payment path

   There are five concrete step classes:
     DirectStepI is an IOU step between accounts
     BookStepII is an IOU/IOU offer book
     BookStepIX is an IOU/XRP offer book
     BookStepXI is an XRP/IOU offer book
     XRPEndpointStep is the source or destination account for XRP

   Amounts may be transformed through a step in either the forward or the
   reverse direction. In the forward direction, the function `fwd` is used to
   find the amount the step would output given an input amount. In the reverse
   direction, the function `rev` is used to find the amount of input needed to
   produce the desired output.

   Amounts are always transformed using liquidity with the same quality (quality
   is the amount out/amount in). For example, a BookStep may use multiple offers
   when executing `fwd` or `rev`, but all those offers will be from the same
   quality directory.

   A step may not have enough liquidity to transform the entire requested
   amount. Both `fwd` and `rev` return a pair of amounts (one for input amount,
   one for output amount) that show how much of the requested amount the step
   was actually able to use.
 */
class Step
{
public:
    virtual ~Step() = default;

    /**
       Find the amount we need to put into the step to get the requested out
       subject to liquidity limits

       @param sb view with the strand's state of balances and offers
       @param afView view the state of balances before the strand runs
              this determines if an offer becomes unfunded or is found unfunded
       @param ofrsToRm offers found unfunded or in an error state are added to
       this collection
       @param out requested step output
       @return actual step input and output
    */
    virtual std::pair<EitherAmount, EitherAmount>
    rev(PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        EitherAmount const& out) = 0;

    /**
       Find the amount we get out of the step given the input
       subject to liquidity limits

       @param sb view with the strand's state of balances and offers
       @param afView view the state of balances before the strand runs
              this determines if an offer becomes unfunded or is found unfunded
       @param ofrsToRm offers found unfunded or in an error state are added to
       this collection
       @param in requested step input
       @return actual step input and output
    */
    virtual std::pair<EitherAmount, EitherAmount>
    fwd(PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        EitherAmount const& in) = 0;

    /**
       Amount of currency computed coming into the Step the last time the
       step ran in reverse.
    */
    virtual std::optional<EitherAmount>
    cachedIn() const = 0;

    /**
       Amount of currency computed coming out of the Step the last time the
       step ran in reverse.
    */
    virtual std::optional<EitherAmount>
    cachedOut() const = 0;

    /**
       If this step is DirectStepI (IOU->IOU direct step), return the src
       account. This is needed for checkNoRipple.
    */
    virtual std::optional<AccountID>
    directStepSrcAcct() const
    {
        return std::nullopt;
    }

    // for debugging. Return the src and dst accounts for a direct step
    // For XRP endpoints, one of src or dst will be the root account
    virtual std::optional<std::pair<AccountID, AccountID>>
    directStepAccts() const
    {
        return std::nullopt;
    }

    /**
       If this step is a DirectStepI and the src redeems to the dst, return
       true, otherwise return false. If this step is a BookStep, return false if
       the owner pays the transfer fee, otherwise return true.

       @param sb view with the strand's state of balances and offers
       @param dir reverse -> called from rev(); forward -> called from fwd().
    */
    virtual DebtDirection
    debtDirection(ReadView const& sb, StrandDirection dir) const = 0;

    /**
        If this step is a DirectStepI, return the quality in of the dst account.
     */
    virtual std::uint32_t
    lineQualityIn(ReadView const&) const
    {
        return QUALITY_ONE;
    }

    // clang-format off
    /**
       Find an upper bound of quality for the step

       @param v view to query the ledger state from
       @param prevStepDir Set to DebtDirection::redeems if the previous step redeems.
       @return A pair. The first element is the upper bound of quality for the step, or std::nullopt if the
       step is dry. The second element will be set to DebtDirection::redeems if this steps redeems,
       DebtDirection:issues if this step issues.
       @note it is an upper bound because offers on the books may be unfunded.
       If there is always a funded offer at the tip of the book, then we could
       rename this `theoreticalQuality` rather than `qualityUpperBound`. It
       could still differ from the actual quality, but except for "dust" amounts,
       it should be a good estimate for the actual quality.
     */
    // clang-format on
    virtual std::pair<std::optional<Quality>, DebtDirection>
    qualityUpperBound(ReadView const& v, DebtDirection prevStepDir) const = 0;

    /** Get QualityFunction. Used in one path optimization where
     * the quality function is non-constant (has AMM) and there is
     * limitQuality. QualityFunction allows calculation of
     * required path output given requested limitQuality.
     * All steps, except for BookStep have the default
     * implementation.
     */
    virtual std::pair<std::optional<QualityFunction>, DebtDirection>
    getQualityFunc(ReadView const& v, DebtDirection prevStepDir) const;

    /** Return the number of offers consumed or partially consumed the last time
        the step ran, including expired and unfunded offers.

        N.B. This this not the total number offers consumed by this step for the
        entire payment, it is only the number the last time it ran. Offers may
        be partially consumed multiple times during a payment.
     */
    virtual std::uint32_t
    offersUsed() const
    {
        return 0;
    }

    /**
       If this step is a BookStep, return the book.
    */
    virtual std::optional<Book>
    bookStepBook() const
    {
        return std::nullopt;
    }

    /**
       Check if amount is zero
    */
    virtual bool
    isZero(EitherAmount const& out) const = 0;

    /**
       Return true if the step should be considered inactive.
       A strand that has additional liquidity may be marked inactive if a step
       has consumed too many offers.
     */
    virtual bool
    inactive() const
    {
        return false;
    }

    /**
       Return true if Out of lhs == Out of rhs.
    */
    virtual bool
    equalOut(EitherAmount const& lhs, EitherAmount const& rhs) const = 0;

    /**
       Return true if In of lhs == In of rhs.
    */
    virtual bool
    equalIn(EitherAmount const& lhs, EitherAmount const& rhs) const = 0;

    /**
       Check that the step can correctly execute in the forward direction

       @param sb view with the strands state of balances and offers
       @param afView view the state of balances before the strand runs
       this determines if an offer becomes unfunded or is found unfunded
       @param in requested step input
       @return first element is true if step is valid, second element is out
       amount
    */
    virtual std::pair<bool, EitherAmount>
    validFwd(PaymentSandbox& sb, ApplyView& afView, EitherAmount const& in) = 0;

    /** Return true if lhs == rhs.

        @param lhs Step to compare.
        @param rhs Step to compare.
        @return true if lhs == rhs.
    */
    friend bool
    operator==(Step const& lhs, Step const& rhs)
    {
        return lhs.equal(rhs);
    }

    /** Return true if lhs != rhs.

        @param lhs Step to compare.
        @param rhs Step to compare.
        @return true if lhs != rhs.
    */
    friend bool
    operator!=(Step const& lhs, Step const& rhs)
    {
        return !(lhs == rhs);
    }

    /** Streaming operator for a Step. */
    friend std::ostream&
    operator<<(std::ostream& stream, Step const& step)
    {
        stream << step.logString();
        return stream;
    }

private:
    virtual std::string
    logString() const = 0;

    virtual bool
    equal(Step const& rhs) const = 0;
};

inline std::pair<std::optional<QualityFunction>, DebtDirection>
Step::getQualityFunc(ReadView const& v, DebtDirection prevStepDir) const
{
    if (auto const res = qualityUpperBound(v, prevStepDir); res.first)
        return {
            QualityFunction{*res.first, QualityFunction::CLOBLikeTag{}},
            res.second};
    else
        return {std::nullopt, res.second};
}

/// @cond INTERNAL
using Strand = std::vector<std::unique_ptr<Step>>;

inline std::uint32_t
offersUsed(Strand const& strand)
{
    std::uint32_t r = 0;
    for (auto const& step : strand)
    {
        if (step)
            r += step->offersUsed();
    }
    return r;
}
/// @endcond

/// @cond INTERNAL
inline bool
operator==(Strand const& lhs, Strand const& rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    for (size_t i = 0, e = lhs.size(); i != e; ++i)
        if (*lhs[i] != *rhs[i])
            return false;
    return true;
}
/// @endcond

/*
   Normalize a path by inserting implied accounts and offers

   @param src Account that is sending assets
   @param dst Account that is receiving assets
   @param deliver Asset the dst account will receive
   (if issuer of deliver == dst, then accept any issuer)
   @param sendMax Optional asset to send.
   @param path Liquidity sources to use for this strand of the payment. The path
               contains an ordered collection of the offer books to use and
               accounts to ripple through.
   @return error code and normalized path
*/
std::pair<TER, STPath>
normalizePath(
    AccountID const& src,
    AccountID const& dst,
    Issue const& deliver,
    std::optional<Issue> const& sendMaxIssue,
    STPath const& path);

/**
   Create a Strand for the specified path

   @param sb view for trust lines, balances, and attributes like auth and freeze
   @param src Account that is sending assets
   @param dst Account that is receiving assets
   @param deliver Asset the dst account will receive
                  (if issuer of deliver == dst, then accept any issuer)
   @param limitQuality Offer crossing BookSteps use this value in an
                       optimization.  If, during direct offer crossing, the
                       quality of the tip of the book drops below this value,
                       then evaluating the strand can stop.
   @param sendMaxIssue Optional asset to send.
   @param path Liquidity sources to use for this strand of the payment. The path
               contains an ordered collection of the offer books to use and
               accounts to ripple through.
   @param ownerPaysTransferFee false -> charge sender; true -> charge offer
   owner
   @param offerCrossing false -> payment; true -> offer crossing
   @param ammContext counts iterations with AMM offers
   @param j Journal for logging messages
   @return Error code and constructed Strand
*/
std::pair<TER, Strand>
toStrand(
    ReadView const& sb,
    AccountID const& src,
    AccountID const& dst,
    Issue const& deliver,
    std::optional<Quality> const& limitQuality,
    std::optional<Issue> const& sendMaxIssue,
    STPath const& path,
    bool ownerPaysTransferFee,
    OfferCrossing offerCrossing,
    AMMContext& ammContext,
    beast::Journal j);

/**
   Create a Strand for each specified path (including the default path, if
   indicated)

   @param sb View for trust lines, balances, and attributes like auth and freeze
   @param src Account that is sending assets
   @param dst Account that is receiving assets
   @param deliver Asset the dst account will receive
                  (if issuer of deliver == dst, then accept any issuer)
   @param limitQuality Offer crossing BookSteps use this value in an
                       optimization.  If, during direct offer crossing, the
                       quality of the tip of the book drops below this value,
                       then evaluating the strand can stop.
   @param sendMax Optional asset to send.
   @param paths Paths to use to fulfill the payment. Each path in the pathset
                contains an ordered collection of the offer books to use and
                accounts to ripple through.
   @param addDefaultPath Determines if the default path should be included
   @param ownerPaysTransferFee false -> charge sender; true -> charge offer
   owner
   @param offerCrossing false -> payment; true -> offer crossing
   @param ammContext counts iterations with AMM offers
   @param j Journal for logging messages
   @return error code and collection of strands
*/
std::pair<TER, std::vector<Strand>>
toStrands(
    ReadView const& sb,
    AccountID const& src,
    AccountID const& dst,
    Issue const& deliver,
    std::optional<Quality> const& limitQuality,
    std::optional<Issue> const& sendMax,
    STPathSet const& paths,
    bool addDefaultPath,
    bool ownerPaysTransferFee,
    OfferCrossing offerCrossing,
    AMMContext& ammContext,
    beast::Journal j);

/// @cond INTERNAL
template <class TIn, class TOut, class TDerived>
struct StepImp : public Step
{
    explicit StepImp() = default;

    std::pair<EitherAmount, EitherAmount>
    rev(PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        EitherAmount const& out) override
    {
        auto const r = static_cast<TDerived*>(this)->revImp(
            sb, afView, ofrsToRm, get<TOut>(out));
        return {EitherAmount(r.first), EitherAmount(r.second)};
    }

    // Given the requested amount to consume, compute the amount produced.
    // Return the consumed/produced
    std::pair<EitherAmount, EitherAmount>
    fwd(PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        EitherAmount const& in) override
    {
        auto const r = static_cast<TDerived*>(this)->fwdImp(
            sb, afView, ofrsToRm, get<TIn>(in));
        return {EitherAmount(r.first), EitherAmount(r.second)};
    }

    bool
    isZero(EitherAmount const& out) const override
    {
        return get<TOut>(out) == beast::zero;
    }

    bool
    equalOut(EitherAmount const& lhs, EitherAmount const& rhs) const override
    {
        return get<TOut>(lhs) == get<TOut>(rhs);
    }

    bool
    equalIn(EitherAmount const& lhs, EitherAmount const& rhs) const override
    {
        return get<TIn>(lhs) == get<TIn>(rhs);
    }
};
/// @endcond

/// @cond INTERNAL
// Thrown when unexpected errors occur
class FlowException : public std::runtime_error
{
public:
    TER ter;

    FlowException(TER t, std::string const& msg)
        : std::runtime_error(msg), ter(t)
    {
    }

    explicit FlowException(TER t) : std::runtime_error(transHuman(t)), ter(t)
    {
    }
};
/// @endcond

/// @cond INTERNAL
// Check equal with tolerance
bool
checkNear(IOUAmount const& expected, IOUAmount const& actual);
bool
checkNear(XRPAmount const& expected, XRPAmount const& actual);
/// @endcond

/**
   Context needed to build Strand Steps and for error checking
 */
struct StrandContext
{
    ReadView const& view;                       ///< Current ReadView
    AccountID const strandSrc;                  ///< Strand source account
    AccountID const strandDst;                  ///< Strand destination account
    Issue const strandDeliver;                  ///< Issue strand delivers
    std::optional<Quality> const limitQuality;  ///< Worst accepted quality
    bool const isFirst;               ///< true if Step is first in Strand
    bool const isLast = false;        ///< true if Step is last in Strand
    bool const ownerPaysTransferFee;  ///< true if owner, not sender, pays fee
    OfferCrossing const
        offerCrossing;         ///< Yes/Sell if offer crossing, not payment
    bool const isDefaultPath;  ///< true if Strand is default path
    size_t const strandSize;   ///< Length of Strand
    /** The previous step in the strand. Needed to check the no ripple
        constraint
     */
    Step const* const prevStep = nullptr;
    /** A strand may not include the same account node more than once
        in the same currency. In a direct step, an account will show up
        at most twice: once as a src and once as a dst (hence the two element
       array). The strandSrc and strandDst will only show up once each.
    */
    std::array<boost::container::flat_set<Issue>, 2>& seenDirectIssues;
    /** A strand may not include an offer that output the same issue more
        than once
    */
    boost::container::flat_set<Issue>& seenBookOuts;
    AMMContext& ammContext;
    beast::Journal const j;

    /** StrandContext constructor. */
    StrandContext(
        ReadView const& view_,
        std::vector<std::unique_ptr<Step>> const& strand_,
        // A strand may not include an inner node that
        // replicates the source or destination.
        AccountID const& strandSrc_,
        AccountID const& strandDst_,
        Issue const& strandDeliver_,
        std::optional<Quality> const& limitQuality_,
        bool isLast_,
        bool ownerPaysTransferFee_,
        OfferCrossing offerCrossing_,
        bool isDefaultPath_,
        std::array<boost::container::flat_set<Issue>, 2>&
            seenDirectIssues_,  ///< For detecting currency loops
        boost::container::flat_set<Issue>&
            seenBookOuts_,  ///< For detecting book loops
        AMMContext& ammContext_,
        beast::Journal j_);  ///< Journal for logging
};

/// @cond INTERNAL
namespace test {
// Needed for testing
bool
directStepEqual(
    Step const& step,
    AccountID const& src,
    AccountID const& dst,
    Currency const& currency);

bool
xrpEndpointStepEqual(Step const& step, AccountID const& acc);

bool
bookStepEqual(Step const& step, ripple::Book const& book);
}  // namespace test

std::pair<TER, std::unique_ptr<Step>>
make_DirectStepI(
    StrandContext const& ctx,
    AccountID const& src,
    AccountID const& dst,
    Currency const& c);

std::pair<TER, std::unique_ptr<Step>>
make_BookStepII(StrandContext const& ctx, Issue const& in, Issue const& out);

std::pair<TER, std::unique_ptr<Step>>
make_BookStepIX(StrandContext const& ctx, Issue const& in);

std::pair<TER, std::unique_ptr<Step>>
make_BookStepXI(StrandContext const& ctx, Issue const& out);

std::pair<TER, std::unique_ptr<Step>>
make_XRPEndpointStep(StrandContext const& ctx, AccountID const& acc);

template <class InAmt, class OutAmt>
bool
isDirectXrpToXrp(Strand const& strand);
/// @endcond

}  // namespace ripple

#endif
