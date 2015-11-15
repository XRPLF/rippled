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

#include <ripple/basics/Log.h>
#include <ripple/protocol/AmountSpec.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/TER.h>

#include <boost/container/flat_set.hpp>
#include <boost/optional.hpp>

namespace ripple {
class PaymentSandbox;
class ReadView;
class ApplyView;

/**
   A step in a payment path

   There are five concrete step classes:
   DirectStepI is an IOU step between accounts
   BookStepII is an IOU/IOU offer book
   BookStepIX is an IOU/XRP offer book
   BookStepXI is an XRP/IOU offer book
   XRPEndpointStep is the source or destination account for XRP
 */
class Step
{
  public:
    virtual ~Step () = default;

    /**
       Find the amount we need to put into the step to get the requested out
       subject to liquidity limits

       @param sb view with the strands state of balances and offers
       @param afView view the the state of balances before the strand runs
              this determines if an offer becomes unfunded or is found unfunded
       @param ofrsToRm offers found unfunded or in an error state are added to this collection
       @param out requested step output
       @return actual step input and output
     */
    virtual
    std::pair<EitherAmount, EitherAmount>
    rev (
        PaymentSandbox& sb,
        ApplyView& afView,
        std::vector<uint256>& ofrsToRm,
        EitherAmount const& out) = 0;

    /**
       Find the amount we get out of the step given the input
       subject to liquidity limits

       @param sb view with the strands state of balances and offers
       @param afView view the the state of balances before the strand runs
              this determines if an offer becomes unfunded or is found unfunded
       @param ofrsToRm offers found unfunded or in an error state are added to this collection
       @param in requested step input
       @return actual step input and output
    */
    virtual
    std::pair<EitherAmount, EitherAmount>
    fwd (
        PaymentSandbox&,
        ApplyView& afView,
        std::vector<uint256>& ofrsToRm,
        EitherAmount const& in) = 0;

    virtual
    boost::optional<EitherAmount>
    cachedIn () const = 0;

    virtual
    boost::optional<EitherAmount>
    cachedOut () const = 0;

    /**
       Check if amount is zero
    */
    virtual
    bool
    dry (EitherAmount const& out) const = 0;

    virtual
    bool
    equalOut (
        EitherAmount const& lhs,
        EitherAmount const& rhs) const = 0;

    /**
       Check that the step can correctly execute in the forward direction

       @param sb view with the strands state of balances and offers
       @param afView view the the state of balances before the strand runs
       this determines if an offer becomes unfunded or is found unfunded
       @param in requested step input
       @return first element is true is step is valid, second element is out amount
    */
    virtual
    std::pair<bool, EitherAmount>
    validFwd (
        PaymentSandbox& sb,
        ApplyView& afView,
        EitherAmount const& in) = 0;

private:
    virtual
    std::string
    logString () const = 0;

    friend
    std::ostream&
    operator << (
        std::ostream& stream,
        Step const& step)
    {
        stream << step.logString ();
        return stream;
    }

    virtual bool equal (Step const& rhs) const = 0;

    friend bool operator==(Step const& lhs, Step const& rhs)
    {
        return lhs.equal (rhs);
    }

    friend bool operator!=(Step const& lhs, Step const& rhs)
    {
        return ! (lhs == rhs);
    }
};

template <class TIn, class TOut, class TDerived>
struct StepImp : public Step
{
    std::pair<EitherAmount, EitherAmount>
    rev (
        PaymentSandbox& sb,
        ApplyView& afView,
        std::vector<uint256>& ofrsToRm,
        EitherAmount const& out) override
    {
        auto const r =
            static_cast<TDerived*> (this)->revImp (sb, afView, ofrsToRm, get<TOut>(out));
        return {EitherAmount (r.first), EitherAmount (r.second)};
    }

    // Given the requested amount to consume, compute the amount produced.
    // Return the consumed/produced
    std::pair<EitherAmount, EitherAmount>
    fwd (
        PaymentSandbox& sb,
        ApplyView& afView,
        std::vector<uint256>& ofrsToRm,
        EitherAmount const& in) override
    {
        auto const r =
            static_cast<TDerived*> (this)->fwdImp (sb, afView, ofrsToRm, get<TIn>(in));
        return {EitherAmount (r.first), EitherAmount (r.second)};
    }

    bool
    dry (EitherAmount const& out) const override
    {
        return get<TOut>(out) == beast::zero;
    }

    bool
    equalOut (
        EitherAmount const& lhs,
        EitherAmount const& rhs) const override
    {
        return get<TOut> (lhs) == get <TOut> (rhs);
    }
};

class StepError : public std::runtime_error
{
  public:
    TER ter;
    std::string msg;

    StepError (TER t, std::string const& msg)
        : std::runtime_error (msg)
        , ter (t)
    {
    }

    explicit
    StepError (TER t)
            : std::runtime_error (transHuman (t))
            , ter (t)
    {
    }
};

// Check equal with tolerance
bool checkEqual (IOUAmount const& expected, IOUAmount const& actual);
bool checkEqual (XRPAmount const& expected, XRPAmount const& actual);

/**
   Context needed for error checking
 */
struct StrandContext
{
    ReadView const& view;
    AccountID const strandSrc;
    AccountID const strandDst;
    bool const isFirst;
    bool const isLast = false;
    size_t const strandSize;
    // If the previous step is a direct step, we need the src account to check
    // the no ripple constraint
    boost::optional<AccountID> const prevDSSrc;
    // A strand may not include the same account node more than once
    // in the same currency. In a direct step, an account will show up
    // at most twice: once as a src and once as a dst (hence the two element array).
    // The strandSrc and strandDst will only show up once each.
    std::array<boost::container::flat_set<Issue>, 2>& seenDirectIssues;
    // A strand may not include the same offer book more than once
    boost::container::flat_set<Book>& seenBooks;
    Logs& logs;

    StrandContext (ReadView const& view_,
        std::vector<std::unique_ptr<Step>> const& strand_,
        // A strand may not include an inner node that
        // replicates the source or destination.
        AccountID strandSrc_,
        AccountID strandDst_,
        bool isLast_,
        std::array<boost::container::flat_set<Issue>, 2>& seenDirectIssues_,
        boost::container::flat_set<Book>& seenBooks_,
        Logs& logs_);
};

std::pair<TER, std::unique_ptr<Step>>
make_DirectStepI (
    StrandContext const& ctx,
    AccountID const& src,
    AccountID const& dst,
    Currency const& c);

std::pair<TER, std::unique_ptr<Step>>
make_BookStepII (
    StrandContext const& ctx,
    Issue const& in,
    Issue const& out);

std::pair<TER, std::unique_ptr<Step>>
make_BookStepIX (
    StrandContext const& ctx,
    Issue const& in);

std::pair<TER, std::unique_ptr<Step>>
make_BookStepXI (
    StrandContext const& ctx,
    Issue const& out);

std::pair<TER, std::unique_ptr<Step>>
make_XRPEndpointStep (
    StrandContext const& ctx,
    AccountID const& acc);

} // ripple

#endif
