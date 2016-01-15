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

#ifndef RIPPLE_APP_PATHS_IMPL_DIRECTSTEP_H_INCLUDED
#define RIPPLE_APP_PATHS_IMPL_DIRECTSTEP_H_INCLUDED

#include <BeastConfig.h>
#include <ripple/app/paths/impl/Steps.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/IOUAmount.h>

namespace ripple {

class DirectStepI : public StepImp<IOUAmount, IOUAmount, DirectStepI>
{
  private:
    AccountID src_;
    AccountID dst_;
    Currency currency_;
    // Transfer fees are never charged when sending directly to or from an
    // issuing account
    bool noTransferFee_;
    Logs& l_;
    beast::Journal j_;

    struct Cache
    {
        IOUAmount in;
        IOUAmount srcToDst;
        IOUAmount out;

        Cache () = default;
        Cache (
            IOUAmount const& in_,
            IOUAmount const& srcToDst_,
            IOUAmount const& out_)
            : in(in_)
            , srcToDst(srcToDst_)
            , out(out_)
        {}
    };

    boost::optional<Cache> cache_;

    // Returns srcQOut, dstQIn
    std::pair <std::uint32_t, std::uint32_t>
    qualities (
        PaymentSandbox& sb,
        bool srcRedeems) const;
  public:
    DirectStepI (
        AccountID const& src,
        AccountID const& dst,
        Currency const& c,
        bool noTransferFee,
        Logs& l)
            :src_(src)
            , dst_(dst)
            , currency_ (c)
            , noTransferFee_ (noTransferFee)
            , l_ (l)
            , j_ (l.journal ("Flow")) {}

    AccountID const& src () const
    {
        return src_;
    };
    AccountID const& dst () const
    {
        return dst_;
    };
    Currency const& currency () const
    {
        return currency_;
    };

    boost::optional<EitherAmount> cachedIn () const override
    {
        if (!cache_)
            return boost::none;
        return EitherAmount (cache_->in);
    }

    boost::optional<EitherAmount>
    cachedOut () const override
    {
        if (!cache_)
            return boost::none;
        return EitherAmount (cache_->out);
    }

    std::pair<IOUAmount, IOUAmount>
    revImp (
        PaymentSandbox& sb,
        ApplyView& afView,
        std::vector<uint256>& ofrsToRm,
        IOUAmount const& out);

    std::pair<IOUAmount, IOUAmount>
    fwdImp (
        PaymentSandbox& sb,
        ApplyView& afView,
        std::vector<uint256>& ofrsToRm,
        IOUAmount const& in);

    std::pair<bool, EitherAmount>
    validFwd (
        PaymentSandbox& sb,
        ApplyView& afView,
        EitherAmount const& in) override;

    // Check for error, existing liquidity, and violations of auth/frozen
    // constraints.
    TER check (StrandContext const& ctx) const;

private:
    friend bool operator==(DirectStepI const& lhs, DirectStepI const& rhs);

    friend bool operator!=(DirectStepI const& lhs, DirectStepI const& rhs)
    {
        return ! (lhs == rhs);
    }

    bool equal (Step const& rhs) const override
    {
        if (auto ds = dynamic_cast<DirectStepI const*> (&rhs))
        {
            return *this == *ds;
        }
        return false;
    }

    std::string logString () const override
    {
        std::ostringstream ostr;
        ostr <<
            "DirectStepI: " <<
            "\nSrc: " << src_ <<
            "\nDst: " << dst_;
        return ostr.str ();
    }
};

inline bool operator==(DirectStepI const& lhs, DirectStepI const& rhs)
{
    return lhs.src_ == rhs.src_ &&
        lhs.dst_ == rhs.dst_ &&
        lhs.currency_ == rhs.currency_ &&
        lhs.noTransferFee_ == rhs.noTransferFee_;
}

}

#endif
