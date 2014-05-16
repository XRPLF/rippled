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

#ifndef RIPPLE_TER_H
#define RIPPLE_TER_H

namespace ripple {

// See https://ripple.com/wiki/Transaction_errors

// VFALCO TODO do not use auto-incrementing. Explicitly assign each
//              constant so there is no possibility of someone coming in
//              and screwing it up.
//
// VFALCO TODO consider renaming TER to TxErr or TxResult for clarity.
//
enum TER    // aka TransactionEngineResult
{
    // Note: Range is stable.  Exact numbers are currently unstable.  Use tokens.

    // -399 .. -300: L Local error (transaction fee inadequate, exceeds local limit)
    // Only valid during non-consensus processing.
    // Implications:
    // - Not forwarded
    // - No fee check
    telLOCAL_ERROR  = -399,
    telBAD_DOMAIN, // VFALCO TODO should read "telBAD_DOMAIN = -398," etc...
    telBAD_PATH_COUNT,
    telBAD_PUBLIC_KEY,
    telFAILED_PROCESSING,
    telINSUF_FEE_P,
    telNO_DST_PARTIAL,

    // -299 .. -200: M Malformed (bad signature)
    // Causes:
    // - Transaction corrupt.
    // Implications:
    // - Not applied
    // - Not forwarded
    // - Reject
    // - Can not succeed in any imagined ledger.
    temMALFORMED    = -299,
    temBAD_AMOUNT,
    temBAD_AUTH_MASTER,
    temBAD_CURRENCY,
    temBAD_FEE,
    temBAD_EXPIRATION,
    temBAD_ISSUER,
    temBAD_LIMIT,
    temBAD_OFFER,
    temBAD_PATH,
    temBAD_PATH_LOOP,
    temBAD_PUBLISH,
    temBAD_TRANSFER_RATE,
    temBAD_SEND_XRP_LIMIT,
    temBAD_SEND_XRP_MAX,
    temBAD_SEND_XRP_NO_DIRECT,
    temBAD_SEND_XRP_PARTIAL,
    temBAD_SEND_XRP_PATHS,
    temBAD_SIGNATURE,
    temBAD_SRC_ACCOUNT,
    temBAD_SEQUENCE,
    temDST_IS_SRC,
    temDST_NEEDED,
    temINVALID,
    temINVALID_FLAG,
    temREDUNDANT,
    temREDUNDANT_SEND_MAX,
    temRIPPLE_EMPTY,
    temUNCERTAIN,       // An intermediate result used internally, should never be returned.
    temUNKNOWN,

    // -199 .. -100: F Failure (sequence number previously used)
    // Causes:
    // - Transaction cannot succeed because of ledger state.
    // - Unexpected ledger state.
    // - C++ exception.
    // Implications:
    // - Not applied
    // - Not forwarded
    // - Could succeed in an imagined ledger.
    tefFAILURE      = -199,
    tefALREADY,
    tefBAD_ADD_AUTH,
    tefBAD_AUTH,
    tefBAD_CLAIM_ID,
    tefBAD_GEN_AUTH,
    tefBAD_LEDGER,
    tefCLAIMED,
    tefCREATED,
    tefDST_TAG_NEEDED,
    tefEXCEPTION,
    tefGEN_IN_USE,
    tefINTERNAL,
    tefNO_AUTH_REQUIRED,    // Can't set auth if auth is not required.
    tefPAST_SEQ,
    tefWRONG_PRIOR,
    tefMASTER_DISABLED,
    tefMAX_LEDGER,

    // -99 .. -1: R Retry (sequence too high, no funds for txn fee, originating account non-existent)
    // Causes:
    // - Prior application of another, possibly non-existant, another transaction could allow this transaction to succeed.
    // Implications:
    // - Not applied
    // - Not forwarded
    // - Might succeed later
    // - Hold
    // - Makes hole in sequence which jams transactions.
    terRETRY        = -99,
    terFUNDS_SPENT,         // This is a free transaction, therefore don't burden network.
    terINSUF_FEE_B,         // Can't pay fee, therefore don't burden network.
    terNO_ACCOUNT,          // Can't pay fee, therefore don't burden network.
    terNO_AUTH,             // Not authorized to hold IOUs.
    terNO_LINE,             // Internal flag.
    terOWNERS,              // Can't succeed with non-zero owner count.
    terPRE_SEQ,             // Can't pay fee, no point in forwarding, therefore don't burden network.
    terLAST,                // Process after all other transactions
    terNO_RIPPLE,           // Rippling not allowed

    // 0: S Success (success)
    // Causes:
    // - Success.
    // Implications:
    // - Applied
    // - Forwarded
    tesSUCCESS      = 0,

    // 100 .. 159 C Claim fee only (ripple transaction with no good paths, pay to non-existent account, no path)
    // Causes:
    // - Success, but does not achieve optimal result.
    // - Invalid transaction or no effect, but claim fee to use the sequence number.
    // Implications:
    // - Applied
    // - Forwarded
    // Only allowed as a return code of appliedTransaction when !tapRetry. Otherwise, treated as terRETRY.
    //
    // DO NOT CHANGE THESE NUMBERS: They appear in ledger meta data.
    tecCLAIM                    = 100,
    tecPATH_PARTIAL             = 101,
    tecUNFUNDED_ADD             = 102,
    tecUNFUNDED_OFFER           = 103,
    tecUNFUNDED_PAYMENT         = 104,
    tecFAILED_PROCESSING        = 105,
    tecDIR_FULL                 = 121,
    tecINSUF_RESERVE_LINE       = 122,
    tecINSUF_RESERVE_OFFER      = 123,
    tecNO_DST                   = 124,
    tecNO_DST_INSUF_XRP         = 125,
    tecNO_LINE_INSUF_RESERVE    = 126,
    tecNO_LINE_REDUNDANT        = 127,
    tecPATH_DRY                 = 128,
    tecUNFUNDED                 = 129,  // Deprecated, old ambiguous unfunded.
    tecMASTER_DISABLED          = 130,
    tecNO_REGULAR_KEY           = 131,
    tecOWNERS                   = 132,
    tecNO_ISSUER                = 133,
    tecNO_AUTH                  = 134,
    tecNO_LINE                  = 135,
};

// VFALCO TODO change these to normal functions.
#define isTelLocal(x)       ((x) >= telLOCAL_ERROR && (x) < temMALFORMED)
#define isTemMalformed(x)   ((x) >= temMALFORMED && (x) < tefFAILURE)
#define isTefFailure(x)     ((x) >= tefFAILURE && (x) < terRETRY)
#define isTerRetry(x)       ((x) >= terRETRY && (x) < tesSUCCESS)
#define isTesSuccess(x)     ((x) == tesSUCCESS)
#define isTecClaim(x)       ((x) >= tecCLAIM)

// VFALCO TODO group these into a shell class along with the defines above.
extern bool transResultInfo (TER terCode, std::string& strToken, std::string& strHuman);
extern std::string transToken (TER terCode);
extern std::string transHuman (TER terCode);

} // ripple

#endif
