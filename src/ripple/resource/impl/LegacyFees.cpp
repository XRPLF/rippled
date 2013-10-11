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

namespace ripple {
namespace Resource {

Charge legacyFee (LoadType t)
{
    switch (t)
    {
    case LT_InvalidRequest:     return feeInvalidRequest;
    case LT_RequestNoReply:     return feeRequestNoReply;
    case LT_InvalidSignature:   return feeInvalidSignature;
    case LT_UnwantedData:       return feeUnwantedData;
    case LT_BadPoW:             return feeBadProofOfWork;
    case LT_BadData:            return feeBadData;

    case LT_RPCInvalid:         return feeInvalidRPC;
    case LT_RPCReference:       return feeReferenceRPC;
    case LT_RPCException:       return feeExceptionRPC;

#if 0
    case LT_RPCLight:           return feeLightRPC;
    case LT_RPCBurdenLow:       return feeLowBurdenRPC;
    case LT_RPCBurdenMedium:    return feeMediumBurdenRPC;
    case LT_RPCBurdenHigh:      return feeHighBurdenRPC;
#endif

    case LT_NewTrusted:         return feeNewTrustedNote;
    case LT_NewTransaction:     return feeNewValidTx;
    case LT_NeededData:         return feeSatisfiedRequest;

    case LT_RequestData:        return feeRequestedData;
    
    default:
        bassertfalse;
    case LT_CheapQuery:         return feeCheapQuery;
    };

    return feeInvalidRequest;
};

}
}
