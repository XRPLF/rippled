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

#include <ripple_app/paths/Calculators.h>
#include <ripple_app/paths/RippleCalc.h>
#include <ripple_app/paths/Tuning.h>

namespace ripple {

// Compute how much might flow for the node for the pass. Does not actually
// adjust balances.
//
// uQualityIn -> uQualityOut
//   saPrvReq -> saCurReq
//   sqPrvAct -> saCurAct
//
// This is a minimizing routine: moving in reverse it propagates the send limit
// to the sender, moving forward it propagates the actual send toward the
// receiver.
//
// This routine works backwards:
// - cur is the driver: it calculates previous wants based on previous credit
//   limits and current wants.
//
// This routine works forwards:
// - prv is the driver: it calculates current deliver based on previous delivery
//   limits and current wants.
//
// This routine is called one or two times for a node in a pass. If called once,
// it will work and set a rate.  If called again, the new work must not worsen
// the previous rate.
void calcNodeRipple (
    RippleCalc& rippleCalc,
    const std::uint32_t uQualityIn,
    const std::uint32_t uQualityOut,
    const STAmount& saPrvReq,   // --> in limit including fees, <0 = unlimited
    const STAmount& saCurReq,   // --> out limit (driver)
    STAmount& saPrvAct,  // <-> in limit including achieved so far: <-- <= -->
    STAmount& saCurAct,  // <-> out limit including achieved : <-- <= -->
    std::uint64_t& uRateMax)
{
    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeRipple>"
        << " uQualityIn=" << uQualityIn
        << " uQualityOut=" << uQualityOut
        << " saPrvReq=" << saPrvReq
        << " saCurReq=" << saCurReq
        << " saPrvAct=" << saPrvAct
        << " saCurAct=" << saCurAct;

    assert (saCurReq > zero); // FIXME: saCurReq was zero
    assert (saPrvReq.getCurrency () == saCurReq.getCurrency ());
    assert (saPrvReq.getCurrency () == saPrvAct.getCurrency ());
    assert (saPrvReq.getIssuer () == saPrvAct.getIssuer ());

    const bool      bPrvUnlimited   = saPrvReq < zero;
    const STAmount  saPrv           = bPrvUnlimited ? STAmount (saPrvReq)
        : saPrvReq - saPrvAct;
    const STAmount  saCur           = saCurReq - saCurAct;

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeRipple: "
        << " bPrvUnlimited=" << bPrvUnlimited
        << " saPrv=" << saPrv
        << " saCur=" << saCur;

    if (uQualityIn >= uQualityOut)
    {
        // No fee.
        WriteLog (lsTRACE, RippleCalc) << "calcNodeRipple: No fees";

        // Only process if we are not worsing previously processed.
        if (!uRateMax || STAmount::uRateOne <= uRateMax)
        {
            // Limit amount to transfer if need.
            STAmount saTransfer = bPrvUnlimited ? saCur
                : std::min (saPrv, saCur);

            // In reverse, we want to propagate the limited cur to prv and set
            // actual cur.
            //
            // In forward, we want to propagate the limited prv to cur and set
            // actual prv.
            saPrvAct += saTransfer;
            saCurAct += saTransfer;

            // If no rate limit, set rate limit to avoid combining with
            // something with a worse rate.
            if (!uRateMax)
                uRateMax = STAmount::uRateOne;
        }
    }
    else
    {
        // Fee.
        WriteLog (lsTRACE, RippleCalc) << "calcNodeRipple: Fee";

        std::uint64_t uRate = STAmount::getRate (
            STAmount (uQualityOut), STAmount (uQualityIn));

        if (!uRateMax || uRate <= uRateMax)
        {
            const uint160   uCurrencyID     = saCur.getCurrency ();
            const uint160   uCurIssuerID    = saCur.getIssuer ();

            // TODO(tom): what's this?
            auto someFee = STAmount::mulRound (
                saCur, uQualityOut, uCurrencyID, uCurIssuerID, true);

            STAmount saCurIn = STAmount::divRound (
                someFee, uQualityIn, uCurrencyID, uCurIssuerID, true);

            WriteLog (lsTRACE, RippleCalc)
                << "calcNodeRipple:"
                << " bPrvUnlimited=" << bPrvUnlimited
                << " saPrv=" << saPrv
                << " saCurIn=" << saCurIn;

            if (bPrvUnlimited || saCurIn <= saPrv)
            {
                // All of cur. Some amount of prv.
                saCurAct += saCur;
                saPrvAct += saCurIn;
                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeRipple:3c:"
                    << " saCurReq=" << saCurReq
                    << " saPrvAct=" << saPrvAct;
            }
            else
            {
                // TODO(tom): what's this?
                auto someFee = STAmount::mulRound (
                    saPrv, uQualityIn, uCurrencyID, uCurIssuerID, true);
                // A part of cur. All of prv. (cur as driver)
                STAmount    saCurOut    = STAmount::divRound (
                    someFee, uQualityOut, uCurrencyID, uCurIssuerID, true);
                WriteLog (lsTRACE, RippleCalc)
                    << "calcNodeRipple:4: saCurReq=" << saCurReq;

                saCurAct    += saCurOut;
                saPrvAct    = saPrvReq;

            }
            if (!uRateMax)
                uRateMax    = uRate;
        }
    }

    WriteLog (lsTRACE, RippleCalc)
        << "calcNodeRipple<"
        << " uQualityIn=" << uQualityIn
        << " uQualityOut=" << uQualityOut
        << " saPrvReq=" << saPrvReq
        << " saCurReq=" << saCurReq
        << " saPrvAct=" << saPrvAct
        << " saCurAct=" << saCurAct;
}

} // ripple
