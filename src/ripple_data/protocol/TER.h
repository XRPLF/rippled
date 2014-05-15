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
    telBAD_DOMAIN = -398, // VFALCO TODO should read "telBAD_DOMAIN = -398," etc...
    telBAD_PATH_COUNT = -397,
    telBAD_PUBLIC_KEY = -396,
    telFAILED_PROCESSING = -395,
    telINSUF_FEE_P = -394,
    telNO_DST_PARTIAL = -393,

    // -299 .. -200: M Malformed (bad signature)
    // Causes:
    // - Transaction corrupt.
    // Implications:
    // - Not applied
    // - Not forwarded
    // - Reject
    // - Can not succeed in any imagined ledger.
    temMALFORMED    = -299,
    temBAD_AMOUNT = -298,
    temBAD_AUTH_MASTER = -297,
    temBAD_CURRENCY = -296,
    temBAD_FEE = -295,
    temBAD_EXPIRATION = -294,
    temBAD_ISSUER = -293,
    temBAD_LIMIT = -292,
    temBAD_OFFER = -291,
    temBAD_PATH = -290,
    temBAD_PATH_LOOP = -289,
    temBAD_PUBLISH = -288,
    temBAD_TRANSFER_RATE = -287,
    temBAD_SEND_XRP_LIMIT = -286,
    temBAD_SEND_XRP_MAX = -285,
    temBAD_SEND_XRP_NO_DIRECT = -284,
    temBAD_SEND_XRP_PARTIAL = -283,
    temBAD_SEND_XRP_PATHS = -282,
    temBAD_SIGNATURE = -281,
    temBAD_SRC_ACCOUNT = -280,
    temBAD_SEQUENCE = -279,
    temDST_IS_SRC = -278,
    temDST_NEEDED = -277,
    temINVALID = -276,
    temINVALID_FLAG = -275,
    temREDUNDANT = -274,
    temREDUNDANT_SEND_MAX = -273,
    temRIPPLE_EMPTY = -272,
    temUNCERTAIN = -271,       // An intermediate result used internally, should never be returned.
    temUNKNOWN = -270,

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
    tefALREADY = -198,
    tefBAD_ADD_AUTH = -197,
    tefBAD_AUTH = -196,
    tefBAD_CLAIM_ID = -195,
    tefBAD_GEN_AUTH = -194,
    tefBAD_LEDGER = -193,
    tefCLAIMED = -192,
    tefCREATED = -191,
    tefDST_TAG_NEEDED = -190,
    tefEXCEPTION = -189,
    tefGEN_IN_USE = -188,
    tefINTERNAL = -187,
    tefNO_AUTH_REQUIRED = -186,    // Can't set auth if auth is not required.
    tefPAST_SEQ = -185,
    tefWRONG_PRIOR = -184,
    tefMASTER_DISABLED = -183,
    tefMAX_LEDGER = -182,

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
    terFUNDS_SPENT = -98,         // This is a free transaction, therefore don't burden network.
    terINSUF_FEE_B = -97,         // Can't pay fee, therefore don't burden network.
    terNO_ACCOUNT = -96,          // Can't pay fee, therefore don't burden network.
    terNO_AUTH = -95,             // Not authorized to hold IOUs.
    terNO_LINE = -94,             // Internal flag.
    terOWNERS = -93,              // Can't succeed with non-zero owner count.
    terPRE_SEQ = -92,             // Can't pay fee, no point in forwarding, therefore don't burden network.
    terLAST = -91,                // Process after all other transactions
    terNO_RIPPLE = -90,           // Rippling not allowed

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
