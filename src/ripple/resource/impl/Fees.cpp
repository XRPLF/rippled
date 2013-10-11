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

Charge const feeInvalidRequest    (  10, "malformed request"      );
Charge const feeRequestNoReply    (   1, "unsatisfiable request"  );
Charge const feeInvalidSignature  ( 100, "invalid signature"      );
Charge const feeUnwantedData      (   5, "useless data"           );
Charge const feeBadProofOfWork    ( 250, "incorrect proof of work"); // DAVID: Check the cost
Charge const feeBadData           (  20, "invalid data"           );

Charge const feeInvalidRPC        (  10, "malformed RPC"          );
Charge const feeReferenceRPC      (   2, "reference RPC"          );
Charge const feeExceptionRPC      (  10, "exceptioned RPC"        );
Charge const feeLightRPC          (   5, "light RPC"              ); // DAVID: Check the cost
Charge const feeLowBurdenRPC      (  10, "low RPC"                );
Charge const feeMediumBurdenRPC   (  20, "medium RPC"             );
Charge const feeHighBurdenRPC     (  50, "heavy RPC"              );

Charge const feeNewTrustedNote    (  10, "trusted note"           );
Charge const feeNewValidTx        (   2, "valid tx"               );
Charge const feeSatisfiedRequest  (  10, "needed data"            );

Charge const feeRequestedData     (   5, "costly request"         );
Charge const feeCheapQuery        (   1, "trivial query"          );

Charge const feeWarning           (  10, "received warning"       ); // DAVID: Check the cost
Charge const feeDrop              ( 100, "dropped"                ); // DAVID: Check the cost

}
}
