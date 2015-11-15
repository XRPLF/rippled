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

#ifndef RIPPLE_APP_PATHS_IMPL_XRPENDPOINTSTEP_H_INCLUDED
#define RIPPLE_APP_PATHS_IMPL_XRPENDPOINTSTEP_H_INCLUDED

#include <BeastConfig.h>
#include <ripple/app/paths/impl/Steps.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/AmountSpec.h>
#include <ripple/protocol/XRPAmount.h>

namespace ripple {

class XRPEndpointStep : public StepImp<XRPAmount, XRPAmount, XRPEndpointStep>
{
  private:
    AccountID acc_;
    bool isLast_;
    beast::Journal j_;

    boost::optional<XRPAmount> cache_;

  public:
    XRPEndpointStep (
        AccountID const& acc,
        bool isLast,
        Logs& l)
            :acc_(acc)
            , isLast_(isLast)
            , j_ (l.journal ("Flow")) {}

    AccountID const& acc () const
    {
        return acc_;
    };

    boost::optional<EitherAmount>
    cachedIn () const override
    {
        if (!cache_)
            return boost::none;
        return EitherAmount (*cache_);
    }

    boost::optional<EitherAmount>
    cachedOut () const override
    {
        if (!cache_)
            return boost::none;
        return EitherAmount (*cache_);
    }

    std::pair<XRPAmount, XRPAmount>
    revImp (
        PaymentSandbox& sb,
        ApplyView& afView,
        std::vector<uint256>& ofrsToRm,
        XRPAmount const& out);

    std::pair<XRPAmount, XRPAmount>
    fwdImp (
        PaymentSandbox& sb,
        ApplyView& afView,
        std::vector<uint256>& ofrsToRm,
        XRPAmount const& in);

    std::pair<bool, EitherAmount>
    validFwd (
        PaymentSandbox& sb,
        ApplyView& afView,
        EitherAmount const& in) override;

    // Check for errors and violations of frozen constraints.
    TER check (StrandContext const& ctx) const;

private:
    friend bool operator==(XRPEndpointStep const& lhs, XRPEndpointStep const& rhs);

    friend bool operator!=(XRPEndpointStep const& lhs, XRPEndpointStep const& rhs)
    {
        return ! (lhs == rhs);
    }

    bool equal (Step const& rhs) const override
    {
        if (auto ds = dynamic_cast<XRPEndpointStep const*> (&rhs))
        {
            return *this == *ds;
        }
        return false;
    }

    std::string logString () const override
    {
        std::ostringstream ostr;
        ostr <<
            "XRPEndpointStep: " <<
            "\nAcc: " << acc_;
        return ostr.str ();
    }
};

inline bool operator==(XRPEndpointStep const& lhs, XRPEndpointStep const& rhs)
{
    return lhs.acc_ == rhs.acc_ && lhs.isLast_ == rhs.isLast_;
}

} // ripple

#endif
