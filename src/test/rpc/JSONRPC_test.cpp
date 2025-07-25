//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <test/jtx.h>
#include <test/jtx/envconfig.h>

#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/core/ConfigSections.h>
#include <xrpld/rpc/detail/TransactionSign.h>

#include <xrpl/basics/contract.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/protocol/ErrorCodes.h>

namespace ripple {

namespace RPC {

struct TxnTestData
{
    char const* const description;
    int const line;
    char const* const json;
    // The JSON is applied to four different interfaces:
    //   1. sign,
    //   2. submit,
    //   3. sign_for, and
    //   4. submit_multisigned.
    // The JSON is not valid for all of these interfaces, but it should
    // crash none of them, and should provide reliable error messages.
    //
    // The expMsg array contains the expected error string for the above cases.
    std::array<char const* const, 4> const expMsg;

    constexpr TxnTestData(
        char const* description_,
        int line_,
        char const* json_,
        std::array<char const* const, 4> const& expMsg_)
        : description(description_), line(line_), json(json_), expMsg{expMsg_}
    {
    }

    TxnTestData() = delete;
    TxnTestData(TxnTestData const&) = delete;
    TxnTestData(TxnTestData&&) = delete;
    TxnTestData&
    operator=(TxnTestData const&) = delete;
    TxnTestData&
    operator=(TxnTestData&&) = delete;
};

static constexpr TxnTestData txnTestArray[] = {

    {"Minimal payment, no Amount only DeliverMax",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "DeliverMax": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"",
       "",
       "Missing field 'account'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Pass in Fee with minimal payment, both Amount and DeliverMax.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Fee": 10,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "DeliverMax": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"",
       "",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Pass in Sequence, no Amount only DeliverMax",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "DeliverMax": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"",
       "",
       "Missing field 'tx_json.Fee'.",
       "Missing field 'tx_json.SigningPubKey'."}}},

    {"Pass in Sequence and Fee with minimal payment, both Amount and "
     "DeliverMax.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Sequence": 0,
        "Fee": 10,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "DeliverMax": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"",
       "",
       "A Signer may not be the transaction's Account "
       "(rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh).",
       "Missing field 'tx_json.SigningPubKey'."}}},

    {"Add 'fee_mult_max' field.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "fee_mult_max": 7,
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"",
       "",
       "Missing field 'tx_json.Fee'.",
       "Missing field 'tx_json.SigningPubKey'."}}},

    {"Add 'fee_mult_max' and 'fee_div_max' field.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "fee_mult_max": 7,
    "fee_div_max": 4,
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"",
       "",
       "Missing field 'tx_json.Fee'.",
       "Missing field 'tx_json.SigningPubKey'."}}},

    {"fee_mult_max is ignored if 'Fee' is present.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "fee_mult_max": 0,
    "tx_json": {
        "Sequence": 0,
        "Fee": 10,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"",
       "",
       "A Signer may not be the transaction's Account "
       "(rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh).",
       "Missing field 'tx_json.SigningPubKey'."}}},

    {"fee_div_max is ignored if 'Fee' is present.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "fee_mult_max": 100,
    "fee_div_max": 1000,
    "tx_json": {
        "Sequence": 0,
        "Fee": 10,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"",
       "",
       "A Signer may not be the transaction's Account "
       "(rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh).",
       "Missing field 'tx_json.SigningPubKey'."}}},

    {"Invalid 'fee_mult_max' field.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "fee_mult_max": "NotAFeeMultiplier",
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Invalid field 'fee_mult_max', not a positive integer.",
       "Invalid field 'fee_mult_max', not a positive integer.",
       "Missing field 'tx_json.Fee'.",
       "Missing field 'tx_json.SigningPubKey'."}}},

    {"Invalid 'fee_div_max' field.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "fee_mult_max": 5,
    "fee_div_max": "NotAFeeMultiplier",
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Invalid field 'fee_div_max', not a positive integer.",
       "Invalid field 'fee_div_max', not a positive integer.",
       "Missing field 'tx_json.Fee'.",
       "Missing field 'tx_json.SigningPubKey'."}}},

    {"Invalid value for 'fee_mult_max' field.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "fee_mult_max": 0,
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Fee of 10 exceeds the requested tx limit of 0",
       "Fee of 10 exceeds the requested tx limit of 0",
       "Missing field 'tx_json.Fee'.",
       "Missing field 'tx_json.SigningPubKey'."}}},

    {"Invalid value for 'fee_div_max' field.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "fee_mult_max": 4,
    "fee_div_max": 7,
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Fee of 10 exceeds the requested tx limit of 5",
       "Fee of 10 exceeds the requested tx limit of 5",
       "Missing field 'tx_json.Fee'.",
       "Missing field 'tx_json.SigningPubKey'."}}},

    {"Invalid zero value for 'fee_div_max' field.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "fee_mult_max": 4,
    "fee_div_max": 0,
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Invalid field 'fee_div_max', not a positive integer.",
       "Invalid field 'fee_div_max', not a positive integer.",
       "Missing field 'tx_json.Fee'.",
       "Missing field 'tx_json.SigningPubKey'."}}},

    {"Missing 'Amount'.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'tx_json.Amount'.",
       "Missing field 'tx_json.Amount'.",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Invalid 'Amount'.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "NotAnAmount",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Invalid field 'tx_json.Amount'.",
       "Invalid field 'tx_json.Amount'.",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Missing 'Destination'.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'tx_json.Destination'.",
       "Missing field 'tx_json.Destination'.",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Invalid 'Destination'.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "NotADestination",
        "TransactionType": "Payment"
    }
})",
     {{"Invalid field 'tx_json.Destination'.",
       "Invalid field 'tx_json.Destination'.",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Cannot create XRP to XRP paths.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "build_path": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Cannot build XRP to XRP paths.",
       "Cannot build XRP to XRP paths.",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Successful 'build_path'.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "build_path": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": {
            "value": "10",
            "currency": "USD",
            "issuer": "rLPwWB1itaUGMV8kbMLLysjGkEpTM2Soy4"
        },
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"",
       "",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Not valid to include both 'Paths' and 'build_path'.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "build_path": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": {
            "value": "10",
            "currency": "USD",
            "issuer": "rLPwWB1itaUGMV8kbMLLysjGkEpTM2Soy4"
        },
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Paths": "",
        "TransactionType": "Payment"
    }
})",
     {{"Cannot specify both 'tx_json.Paths' and 'build_path'",
       "Cannot specify both 'tx_json.Paths' and 'build_path'",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Successful 'SendMax'.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "build_path": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": {
            "value": "10",
            "currency": "USD",
            "issuer": "rLPwWB1itaUGMV8kbMLLysjGkEpTM2Soy4"
        },
        "SendMax": {
            "value": "5",
            "currency": "USD",
            "issuer": "rLPwWB1itaUGMV8kbMLLysjGkEpTM2Soy4"
        },
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"",
       "",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"'Amount' may not be XRP for pathfinding, but 'SendMax' may be XRP.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "build_path": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": {
            "value": "10",
            "currency": "USD",
            "issuer": "rLPwWB1itaUGMV8kbMLLysjGkEpTM2Soy4"
        },
        "SendMax": 10000,
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"",
       "",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"'secret' must be present.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"'secret' must be non-empty.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Invalid field 'secret'.",
       "Invalid field 'secret'.",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Use 'seed' instead of 'secret'.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rJrxi4Wxev4bnAGVNP9YCdKPdAoKfAmcsi",
    "key_type": "ed25519",
    "seed": "sh1yJfwoi98zCygwijUzuHmJDeVKd",
    "tx_json": {
        "Account": "rJrxi4Wxev4bnAGVNP9YCdKPdAoKfAmcsi",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"",
       "",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Malformed 'seed'.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rJrxi4Wxev4bnAGVNP9YCdKPdAoKfAmcsi",
    "key_type": "ed25519",
    "seed": "not a seed",
    "tx_json": {
        "Account": "rJrxi4Wxev4bnAGVNP9YCdKPdAoKfAmcsi",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Disallowed seed.",
       "Disallowed seed.",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"'tx_json' must be present.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "rx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'tx_json'.",
       "Missing field 'tx_json'.",
       "Missing field 'tx_json'.",
       "Missing field 'tx_json'."}}},

    {"'TransactionType' must be present.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
    }
})",
     {{"Missing field 'tx_json.TransactionType'.",
       "Missing field 'tx_json.TransactionType'.",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"The 'TransactionType' must be a pre-established transaction type.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "tt"
    }
})",
     {{"Field 'tx_json.TransactionType' has invalid data.",
       "Field 'tx_json.TransactionType' has invalid data.",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"The 'TransactionType' may be represented with an integer.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": 0
    }
})",
     {{"",
       "",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"'Account' must be present.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'tx_json.Account'.",
       "Missing field 'tx_json.Account'.",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"'Account' must be well formed.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "NotAnAccount",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Invalid field 'tx_json.Account'.",
       "Invalid field 'tx_json.Account'.",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"The 'offline' tag may be added to the transaction.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "offline": 0,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"",
       "",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"If 'offline' is true then a 'Sequence' field must be supplied.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "offline": 1,
    "tx_json": {
        "Fee": 10,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"If 'offline' is true then a 'Fee' field must be supplied.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "offline": 1,
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'tx_json.Fee'.",
       "Missing field 'tx_json.Fee'.",
       "Missing field 'tx_json.Fee'.",
       "Missing field 'tx_json.SigningPubKey'."}}},

    {"Valid transaction if 'offline' is true.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "offline": 1,
    "tx_json": {
        "Sequence": 0,
        "Fee": 10,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"",
       "",
       "A Signer may not be the transaction's Account "
       "(rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh).",
       "Missing field 'tx_json.SigningPubKey'."}}},

    {"'offline' and 'build_path' are mutually exclusive.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "offline": 1,
    "build_path": 1,
    "tx_json": {
        "Sequence": 0,
        "Fee": 10,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Field 'build_path' not allowed in this context.",
       "Field 'build_path' not allowed in this context.",
       "Field 'build_path' not allowed in this context.",
       "Missing field 'tx_json.SigningPubKey'."}}},

    {"A 'Flags' field may be specified.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Flags": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"",
       "",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"The 'Flags' field must be numeric.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Flags": "NotGoodFlags",
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Field 'tx_json.Flags' has invalid data.",
       "Field 'tx_json.Flags' has invalid data.",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"It's okay to add a 'debug_signing' field.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "debug_signing": 0,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"",
       "",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Single-sign a multisigned transaction.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
    "secret": "a",
    "tx_json": {
        "Account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Amount" : "1000000000",
        "Destination" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Fee" : "50",
        "Sequence" : 0,
        "Signers" : [
            {
                "Signer" : {
                    "Account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
                    "SigningPubKey" : "0330E7FC9D56BB25D6893BA3F317AE5BCF33B3291BD63DB32654A313222F7FD020",
                    "TxnSignature" : "304502210080EB23E78A841DDC5E3A4F10DE6EAF052207D6B519BF8954467ADB221B3F349002202CA458E8D4E4DE7176D27A91628545E7B295A5DFC8ADF0B5CD3E279B6FA02998"
                }
            }
        ],
        "SigningPubKey" : "",
        "TransactionType" : "Payment"
    }
})",
     {{"Already multisigned.",
       "Already multisigned.",
       "Secret does not match account.",
       ""}}},

    {"Minimal sign_for.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Amount": "1000000000",
        "Destination": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Secret does not match account.",
       "Secret does not match account.",
       "",
       "Missing field 'tx_json.Signers'."}}},

    {"Minimal offline sign_for.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "offline": 1,
    "tx_json": {
        "Account": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Amount": "1000000000",
        "Destination": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"", "", "", "Missing field 'tx_json.Signers'."}}},

    {"Offline sign_for using 'seed' instead of 'secret'.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rJrxi4Wxev4bnAGVNP9YCdKPdAoKfAmcsi",
    "key_type": "ed25519",
    "seed": "sh1yJfwoi98zCygwijUzuHmJDeVKd",
    "offline": 1,
    "tx_json": {
        "Account": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Amount": "1000000000",
        "Destination": "rJrxi4Wxev4bnAGVNP9YCdKPdAoKfAmcsi",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"", "", "", "Missing field 'tx_json.Signers'."}}},

    {"Malformed seed in sign_for.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rJrxi4Wxev4bnAGVNP9YCdKPdAoKfAmcsi",
    "key_type": "ed25519",
    "seed": "sh1yJfwoi98zCygwjUzuHmJDeVKd",
    "offline": 1,
    "tx_json": {
        "Account": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Amount": "1000000000",
        "Destination": "rJrxi4Wxev4bnAGVNP9YCdKPdAoKfAmcsi",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Disallowed seed.",
       "Disallowed seed.",
       "Disallowed seed.",
       "Missing field 'tx_json.Signers'."}}},

    {"Missing 'Account' in sign_for.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Amount": "1000000000",
        "Destination": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'tx_json.Account'.",
       "Missing field 'tx_json.Account'.",
       "Missing field 'tx_json.Account'.",
       "Missing field 'tx_json.Account'."}}},

    {"Missing 'Amount' in sign_for.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Destination": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'tx_json.Amount'.",
       "Missing field 'tx_json.Amount'.",
       "Missing field 'tx_json.Amount'.",
       "Missing field 'tx_json.Amount'."}}},

    {"Missing 'Destination' in sign_for.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Amount": "1000000000",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'tx_json.Destination'.",
       "Missing field 'tx_json.Destination'.",
       "Missing field 'tx_json.Destination'.",
       "Missing field 'tx_json.Destination'."}}},

    {"Missing 'Destination' in sign_for, use DeliverMax",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "DeliverMax": "1000000000",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'tx_json.Destination'.",
       "Missing field 'tx_json.Destination'.",
       "Missing field 'tx_json.Destination'.",
       "Missing field 'tx_json.Destination'."}}},

    {"Missing 'Fee' in sign_for.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Amount": "1000000000",
        "Destination": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Secret does not match account.",
       "Secret does not match account.",
       "Missing field 'tx_json.Fee'.",
       "Missing field 'tx_json.Fee'."}}},

    {"Missing 'Sequence' in sign_for.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Amount": "1000000000",
        "Destination": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Fee": 50,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Secret does not match account.",
       "Secret does not match account.",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Missing 'SigningPubKey' in sign_for is automatically filled in.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Amount": "1000000000",
        "Destination": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Fee": 50,
        "Sequence": 0,
        "TransactionType": "Payment"
    }
})",
     {{"Secret does not match account.",
       "Secret does not match account.",
       "",
       "Missing field 'tx_json.SigningPubKey'."}}},

    {"In sign_for, an account may not sign for itself.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
    "secret": "a",
    "tx_json": {
        "Account": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Amount": "1000000000",
        "Destination": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Fee": 50,
        "Sequence": 0,
        "TransactionType": "Payment"
    }
})",
     {{"",
       "",
       "A Signer may not be the transaction's Account "
       "(rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA).",
       "Missing field 'tx_json.SigningPubKey'."}}},

    {"Cannot put duplicate accounts in Signers array",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Amount" : "1000000000",
        "Destination" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Fee" : "50",
        "Sequence" : 0,
        "Signers" : [
            {
                "Signer" : {
                    "Account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
                    "SigningPubKey" : "0330E7FC9D56BB25D6893BA3F317AE5BCF33B3291BD63DB32654A313222F7FD020",
                    "TxnSignature" : "304502210080EB23E78A841DDC5E3A4F10DE6EAF052207D6B519BF8954467ADB221B3F349002202CA458E8D4E4DE7176D27A91628545E7B295A5DFC8ADF0B5CD3E279B6FA02998"
                }
            }
        ],
        "SigningPubKey" : "",
        "TransactionType" : "Payment"
    }
})",
     {{"Already multisigned.",
       "Already multisigned.",
       "Duplicate Signers:Signer:Account entries "
       "(rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh) are not allowed.",
       ""}}},

    {"Correctly append to pre-established Signers array",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
    "secret": "c",
    "tx_json": {
        "Account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Amount" : "1000000000",
        "Destination" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Fee" : "50",
        "Sequence" : 0,
        "Signers" : [
            {
                "Signer" : {
                    "Account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
                    "SigningPubKey" : "0330E7FC9D56BB25D6893BA3F317AE5BCF33B3291BD63DB32654A313222F7FD020",
                    "TxnSignature" : "304502210080EB23E78A841DDC5E3A4F10DE6EAF052207D6B519BF8954467ADB221B3F349002202CA458E8D4E4DE7176D27A91628545E7B295A5DFC8ADF0B5CD3E279B6FA02998"
                }
            }
        ],
        "SigningPubKey" : "",
        "TransactionType" : "Payment"
    }
})",
     {{"Already multisigned.", "Already multisigned.", "", ""}}},

    {"Append to pre-established Signers array with bad signature",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
    "secret": "c",
    "tx_json": {
        "Account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Amount" : "1000000000",
        "Destination" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Fee" : "50",
        "Sequence" : 0,
        "Signers" : [
            {
                "Signer" : {
                    "Account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
                    "SigningPubKey" : "0330E7FC9D56BB25D6893BA3F317AE5BCF33B3291BD63DB32654A313222F7FD020",
                    "TxnSignature" : "304502210080EB23E78A841DDC5E3A4F10DE6EAF052207D6B519BF8954467ACB221B3F349002202CA458E8D4E4DE7176D27A91628545E7B295A5DFC8ADF0B5CD3E279B6FA02998"
                }
            }
        ],
        "SigningPubKey" : "",
        "TransactionType" : "Payment"
    }
})",
     {{"Already multisigned.",
       "Already multisigned.",
       "Invalid signature.",
       "Invalid signature."}}},

    {"Non-empty 'SigningPubKey' in sign_for.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Amount": "1000000000",
        "Destination": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "1",
        "TransactionType": "Payment"
    }
})",
     {{"Secret does not match account.",
       "Secret does not match account.",
       "When multi-signing 'tx_json.SigningPubKey' must be empty.",
       "When multi-signing 'tx_json.SigningPubKey' must be empty."}}},

    {"Missing 'TransactionType' in sign_for.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Amount": "1000000000",
        "Destination": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
    }
})",
     {{"Missing field 'tx_json.TransactionType'.",
       "Missing field 'tx_json.TransactionType'.",
       "Missing field 'tx_json.TransactionType'.",
       "Missing field 'tx_json.TransactionType'."}}},

    {"TxnSignature in sign_for.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
    "secret": "c",
    "tx_json": {
        "Account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Amount" : "1000000000",
        "Destination" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Fee" : "50",
        "Sequence" : 0,
        "Signers" : [
            {
                "Signer" : {
                    "Account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
                    "SigningPubKey" : "0330E7FC9D56BB25D6893BA3F317AE5BCF33B3291BD63DB32654A313222F7FD020",
                    "TxnSignature" : "304502210080EB23E78A841DDC5E3A4F10DE6EAF052207D6B519BF8954467ADB221B3F349002202CA458E8D4E4DE7176D27A91628545E7B295A5DFC8ADF0B5CD3E279B6FA02998"
                }
            }
        ],
        "SigningPubKey" : "",
        "TxnSignature" : "304502210080EB23E78A841DDC5E3A4F10DE6EAF052207D6B519BF8954467ADB221B3F349002202CA458E8D4E4DE7176D27A91628545E7B295A5DFC8ADF0B5CD3E279B6FA02998",
        "TransactionType" : "Payment"
    }
})",
     {{"Already multisigned.",
       "Already multisigned.",
       "Already single-signed.",
       "Signing of transaction is malformed."}}},

    {"Invalid field 'tx_json': string instead of object",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": ""
})",
     {{"Invalid field 'tx_json', not object.",
       "Invalid field 'tx_json', not object.",
       "Invalid field 'tx_json', not object.",
       "Invalid field 'tx_json', not object."}}},

    {"Invalid field 'tx_json': integer instead of object",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": 20160331
})",
     {{"Invalid field 'tx_json', not object.",
       "Invalid field 'tx_json', not object.",
       "Invalid field 'tx_json', not object.",
       "Invalid field 'tx_json', not object."}}},

    {"Invalid field 'tx_json': array instead of object",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": [ "hello", "world" ]
})",
     {{"Invalid field 'tx_json', not object.",
       "Invalid field 'tx_json', not object.",
       "Invalid field 'tx_json', not object.",
       "Invalid field 'tx_json', not object."}}},

    {"Pass in Fee with minimal payment, both Amount and DeliverMax.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "r9zN9x52FiCFAcicCLMQKbj1nxYhxJbbSy",
    "secret": "ssgN6zTvtM1q9XV8DvJpWm8LBYWiY",
    "tx_json": {
        "Fee": 10,
        "Account": "r9zN9x52FiCFAcicCLMQKbj1nxYhxJbbSy",
        "Amount": "1000000000",
        "DeliverMax": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
     {{"Source account not found.",
       "Source account not found.",
       "Missing field 'tx_json.Sequence'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Minimal submit_multisigned.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "Signers" : [
             {
                "Signer" : {
                    "Account" : "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "SigningPubKey" : "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8",
                    "TxnSignature" : "3045022100909D01399AFFAD1E30D250CE61F93975B7F61E47B5244D78C3E86D9806535D95022012E389E0ACB016334052B7FE07FA6CEFDC8BE82CB410FA841D5049641C89DC8F"
                }
            }
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       ""}}},

    {"Minimal submit_multisigned with bad signature.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "Signers": [
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Invalid signature."}}},

    {"Missing tx_json in submit_multisigned.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "Signers": [
        {
            "Signer": {
                "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
            }
        }
    ]
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Missing field 'tx_json'."}}},

    {"Missing sequence in submit_multisigned.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Signers": [
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Missing SigningPubKey in submit_multisigned.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Signers": [
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "Sequence": 0,
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Missing field 'tx_json.SigningPubKey'."}}},

    {"Non-empty SigningPubKey in submit_multisigned.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "Signers": [
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "When multi-signing 'tx_json.SigningPubKey' must be empty."}}},

    {"Missing TransactionType in submit_multisigned.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Signers": [
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "Sequence": 0,
        "SigningPubKey": "",
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Missing field 'tx_json.TransactionType'."}}},

    {"Missing Account in submit_multisigned.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "Signers": [
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Missing field 'tx_json.Account'."}}},

    {"Malformed Account in submit_multisigned.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "NotAnAccount",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "Signers": [
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Invalid field 'tx_json.Account'."}}},

    {"Account not in ledger in submit_multisigned.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rDg53Haik2475DJx8bjMDSDPj4VX7htaMd",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "Signers": [
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Source account not found."}}},

    {"Missing Fee in submit_multisigned.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Sequence": 0,
        "Signers": [
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Missing field 'tx_json.Fee'."}}},

    {"Non-numeric Fee in submit_multisigned.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50.1,
        "Sequence": 0,
        "Signers": [
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Field 'tx_json.Fee' has invalid data."}}},

    {"Missing Amount in submit_multisigned Payment.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50000000,
        "Sequence": 0,
        "Signers": [
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Missing field 'tx_json.Amount'."}}},

    {"Invalid Amount in submit_multisigned Payment.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "NotANumber",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "Signers": [
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Invalid field 'tx_json.Amount'."}}},

    {"Invalid DeliverMax in submit_multisigned Payment.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "DeliverMax": "NotANumber",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "Signers": [
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Invalid field 'tx_json.Amount'."}}},

    {"No build_path in submit_multisigned.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "build_path": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "Signers": [
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Field 'build_path' not allowed in this context."}}},

    {"Missing Destination in submit_multisigned Payment.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Fee": 50,
        "Sequence": 0,
        "Signers": [
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Missing field 'tx_json.Destination'."}}},

    {"Malformed Destination in submit_multisigned Payment.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "NotADestination",
        "Fee": 50,
        "Sequence": 0,
        "Signers": [
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Invalid field 'tx_json.Destination'."}}},

    {"Missing Signers field in submit_multisigned.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Missing field 'tx_json.Signers'."}}},

    {"Signers not an array in submit_multisigned.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "Signers": {
            "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
            "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
            "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
        },
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Field 'tx_json.Signers' is not a JSON array."}}},

    {"Empty Signers array in submit_multisigned.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "Signers": [
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "tx_json.Signers array may not be empty."}}},

    {"Duplicate Signer in submit_multisigned.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "Signers": [
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            },
            {
                "Signer": {
                    "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "Duplicate Signers:Signer:Account entries "
       "(rPcNzota6B8YBokhYtcTNqQVCngtbnWfux) are not allowed."}}},

    {"Signer is tx_json Account in submit_multisigned.",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "Signers": [
            {
                "Signer": {
                    "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
                    "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                    "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
                }
            }
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "A Signer may not be the transaction's Account "
       "(rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh)."}}},

    {"Empty Signers array in submit_multisigned, use DeliverMax",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "DeliverMax": "10000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "Signers": [
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "tx_json.Signers array may not be empty."}}},

    {"Empty Signers array in submit_multisigned, use DeliverMax and Amount",
     __LINE__,
     R"({
    "command": "submit_multisigned",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "10000000",
        "DeliverMax": "10000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "Signers": [
        ],
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Missing field 'secret'.",
       "Missing field 'secret'.",
       "Missing field 'account'.",
       "tx_json.Signers array may not be empty."}}},

    {"Payment cannot specify different DeliverMax and Amount.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "debug_signing": 0,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "DeliverMax": "1000000020",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
     {{"Cannot specify differing 'Amount' and 'DeliverMax'",
       "Cannot specify differing 'Amount' and 'DeliverMax'",
       "Cannot specify differing 'Amount' and 'DeliverMax'",
       "Cannot specify differing 'Amount' and 'DeliverMax'"}}},
    {"Payment cannot specify bad DomainID.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "debug_signing": 0,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment",
        "DomainID": "invalid",
    }
})",
     {{"Unable to parse 'DomainID'.",
       "Unable to parse 'DomainID'.",
       "Unable to parse 'DomainID'.",
       "Unable to parse 'DomainID'."}}},

    {"Minimal delegated transaction.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "secret": "a",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment",
        "Delegate": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
    }
})",
     {{"",
       "",
       "Missing field 'account'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Delegate not well formed.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "secret": "a",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rJrxi4Wxev4bnAGVNP9YCdKPdAoKfAmcsi",
        "TransactionType": "Payment",
        "Delegate": "NotAnAccount"
    }
})",
     {{"Invalid field 'tx_json.Delegate'.",
       "Invalid field 'tx_json.Delegate'.",
       "Missing field 'account'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Delegate not in ledger.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "secret": "a",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rJrxi4Wxev4bnAGVNP9YCdKPdAoKfAmcsi",
        "TransactionType": "Payment",
        "Delegate": "rDg53Haik2475DJx8bjMDSDPj4VX7htaMd"
    }
})",
     {{"Delegate account not found.",
       "Delegate account not found.",
       "Missing field 'account'.",
       "Missing field 'tx_json.Sequence'."}}},

    {"Delegate and secret not match.",
     __LINE__,
     R"({
    "command": "doesnt_matter",
    "secret": "aa",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rJrxi4Wxev4bnAGVNP9YCdKPdAoKfAmcsi",
        "TransactionType": "Payment",
        "Delegate": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
    }
})",
     {{"Secret does not match account.",
       "Secret does not match account.",
       "Missing field 'account'.",
       "Missing field 'tx_json.Sequence'."}}},

};

class JSONRPC_test : public beast::unit_test::suite
{
public:
    void
    testBadRpcCommand()
    {
        testcase("bad RPC command");
        test::jtx::Env env(*this);
        Json::Value const result{
            env.rpc("bad_command", R"({"MakingThisUp": 0})")};

        BEAST_EXPECT(result[jss::result][jss::error] == "unknownCmd");
        BEAST_EXPECT(
            result[jss::result][jss::request][jss::command] == "bad_command");
    }

    void
    testAutoFillFails()
    {
        testcase("autofill fails");
        using namespace test::jtx;

        // test batch raw transactions max size
        {
            Env env(*this);
            auto ledger = env.current();
            auto const& feeTrack = env.app().getFeeTrack();
            Json::Value req;
            Account const alice("alice");
            Account const bob("bob");
            env.fund(XRP(100000), alice);
            env.close();

            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            auto const seq = env.seq(alice);
            auto jt = env.jtnofill(
                batch::outer(alice, env.seq(alice), batchFee, tfAllOrNothing),
                batch::inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::inner(pay(alice, bob, XRP(2)), seq + 2),
                batch::inner(pay(alice, bob, XRP(3)), seq + 3),
                batch::inner(pay(alice, bob, XRP(4)), seq + 4),
                batch::inner(pay(alice, bob, XRP(5)), seq + 5),
                batch::inner(pay(alice, bob, XRP(6)), seq + 6),
                batch::inner(pay(alice, bob, XRP(7)), seq + 7),
                batch::inner(pay(alice, bob, XRP(8)), seq + 8),
                batch::inner(pay(alice, bob, XRP(9)), seq + 9));

            jt.jv.removeMember(jss::Fee);
            jt.jv.removeMember(jss::TxnSignature);
            req[jss::tx_json] = jt.jv;
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrack,
                env.app().getTxQ(),
                env.app());
            BEAST_EXPECT(result.size() == 0);
            BEAST_EXPECT(
                req[jss::tx_json].isMember(jss::Fee) &&
                req[jss::tx_json][jss::Fee] ==
                    env.current()->fees().base.jsonClipped());
        }

        // test signers max size
        {
            Env env(*this);
            auto ledger = env.current();
            auto const& feeTrack = env.app().getFeeTrack();
            Json::Value req;
            Account const alice("alice");
            Account const bob("bob");
            env.fund(XRP(100000), alice, bob);
            env.close();

            auto jt = env.jtnofill(
                noop(alice),
                msig(
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice,
                    alice));

            req[jss::tx_json] = jt.jv;
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrack,
                env.app().getTxQ(),
                env.app());
            BEAST_EXPECT(result.size() == 0);
            BEAST_EXPECT(
                req[jss::tx_json].isMember(jss::Fee) &&
                req[jss::tx_json][jss::Fee] ==
                    env.current()->fees().base.jsonClipped());
        }
    }

    void
    testAutoFillFees()
    {
        testcase("autofill fees");
        test::jtx::Env env(*this);
        auto const baseFee =
            static_cast<int>(env.current()->fees().base.drops());
        auto ledger = env.current();
        auto const& feeTrack = env.app().getFeeTrack();

        {
            Json::Value req;
            Json::Reader().parse(
                "{ \"fee_mult_max\" : 1, \"tx_json\" : { } } ", req);
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrack,
                env.app().getTxQ(),
                env.app());

            BEAST_EXPECT(!RPC::contains_error(result));
            BEAST_EXPECT(
                req[jss::tx_json].isMember(jss::Fee) &&
                req[jss::tx_json][jss::Fee] == baseFee);
        }

        {
            Json::Value req;
            Json::Reader().parse(
                "{ \"fee_mult_max\" : 3, \"fee_div_max\" : 2, "
                "\"tx_json\" : { } } ",
                req);
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrack,
                env.app().getTxQ(),
                env.app());

            BEAST_EXPECT(!RPC::contains_error(result));
            BEAST_EXPECT(
                req[jss::tx_json].isMember(jss::Fee) &&
                req[jss::tx_json][jss::Fee] == baseFee);
        }

        {
            Json::Value req;
            Json::Reader().parse(
                "{ \"fee_mult_max\" : 0, \"tx_json\" : { } } ", req);
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrack,
                env.app().getTxQ(),
                env.app());

            BEAST_EXPECT(RPC::contains_error(result));
            BEAST_EXPECT(!req[jss::tx_json].isMember(jss::Fee));
        }

        {
            // 3/6 = 1/2, but use the bigger number make sure
            // we're dividing.
            Json::Value req;
            Json::Reader().parse(
                "{ \"fee_mult_max\" : 3, \"fee_div_max\" : 6, "
                "\"tx_json\" : { } } ",
                req);
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrack,
                env.app().getTxQ(),
                env.app());

            BEAST_EXPECT(RPC::contains_error(result));
            BEAST_EXPECT(!req[jss::tx_json].isMember(jss::Fee));
        }

        {
            Json::Value req;
            Json::Reader().parse(
                "{ \"fee_mult_max\" : 0, \"fee_div_max\" : 2, "
                "\"tx_json\" : { } } ",
                req);
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrack,
                env.app().getTxQ(),
                env.app());

            BEAST_EXPECT(RPC::contains_error(result));
            BEAST_EXPECT(!req[jss::tx_json].isMember(jss::Fee));
        }

        {
            Json::Value req;
            Json::Reader().parse(
                "{ \"fee_mult_max\" : 10, \"fee_div_max\" : 0, "
                "\"tx_json\" : { } } ",
                req);
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrack,
                env.app().getTxQ(),
                env.app());

            BEAST_EXPECT(RPC::contains_error(result));
            BEAST_EXPECT(!req[jss::tx_json].isMember(jss::Fee));
        }

        {
            // transaction with a higher base fee
            Json::Value req;
            test::jtx::Account const alice("alice");
            req[jss::tx_json] =
                test::jtx::acctdelete(env.master.human(), alice.human());
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrack,
                env.app().getTxQ(),
                env.app());

            BEAST_EXPECT(result.size() == 0);
            BEAST_EXPECT(
                req[jss::tx_json].isMember(jss::Fee) &&
                req[jss::tx_json][jss::Fee] ==
                    env.current()->fees().increment.jsonClipped());
        }
    }

    void
    testAutoFillEscalatedFees()
    {
        testcase("autofill escalated fees");
        using namespace test::jtx;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->loadFromString("[" SECTION_SIGNING_SUPPORT "]\ntrue");
                    cfg->section("transaction_queue")
                        .set("minimum_txn_in_ledger_standalone", "3");
                    return cfg;
                })};
        LoadFeeTrack const& feeTrackOuter = env.app().getFeeTrack();

        {
            // high mult, no tx
            Json::Value req;
            Json::Reader().parse(
                R"({
                "fee_mult_max" : 1000,
                "tx_json" : { }
            })",
                req);
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrackOuter,
                env.app().getTxQ(),
                env.app());

            BEAST_EXPECT(!RPC::contains_error(result));
            BEAST_EXPECT(
                req[jss::tx_json].isMember(jss::Fee) &&
                req[jss::tx_json][jss::Fee] == 10);
        }

        {
            // low mult, no tx
            Json::Value req;
            Json::Reader().parse(
                R"({
                "fee_mult_max" : 5,
                "tx_json" : { }
            })",
                req);
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrackOuter,
                env.app().getTxQ(),
                env.app());

            BEAST_EXPECT(!RPC::contains_error(result));
            BEAST_EXPECT(
                req[jss::tx_json].isMember(jss::Fee) &&
                req[jss::tx_json][jss::Fee] == 10);
        }

        // put 4 transactions into the open ledger
        for (auto i = 0; i < 4; ++i)
        {
            env(noop(env.master));
        }

        {
            // high mult, 4 txs
            Json::Value req;
            Json::Reader().parse(
                R"({
                "fee_mult_max" : 1000,
                "tx_json" : { }
            })",
                req);
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrackOuter,
                env.app().getTxQ(),
                env.app());

            BEAST_EXPECT(!RPC::contains_error(result));
            BEAST_EXPECT(
                req[jss::tx_json].isMember(jss::Fee) &&
                req[jss::tx_json][jss::Fee] == 8889);
        }

        {
            // low mult, 4 tx
            Json::Value req;
            Json::Reader().parse(
                R"({
                "fee_mult_max" : 5,
                "tx_json" : { }
            })",
                req);
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrackOuter,
                env.app().getTxQ(),
                env.app());

            BEAST_EXPECT(RPC::contains_error(result));
            BEAST_EXPECT(!req[jss::tx_json].isMember(jss::Fee));
        }

        {
            // different low mult, 4 tx
            Json::Value req;
            Json::Reader().parse(
                R"({
                "fee_mult_max" : 1000,
                "fee_div_max" : 3,
                "tx_json" : { }
            })",
                req);
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrackOuter,
                env.app().getTxQ(),
                env.app());

            BEAST_EXPECT(RPC::contains_error(result));
            BEAST_EXPECT(!req[jss::tx_json].isMember(jss::Fee));
        }

        {
            // high mult, 4 tx
            Json::Value req;
            Json::Reader().parse(
                R"({
                "fee_mult_max" : 8000,
                "fee_div_max" : 3,
                "tx_json" : { }
            })",
                req);
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrackOuter,
                env.app().getTxQ(),
                env.app());

            BEAST_EXPECT(!RPC::contains_error(result));
            BEAST_EXPECT(
                req[jss::tx_json].isMember(jss::Fee) &&
                req[jss::tx_json][jss::Fee] == 8889);
        }

        {
            // negative mult
            Json::Value req;
            Json::Reader().parse(
                R"({
                "fee_mult_max" : -5,
                "tx_json" : { }
            })",
                req);
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrackOuter,
                env.app().getTxQ(),
                env.app());

            BEAST_EXPECT(RPC::contains_error(result));
        }

        {
            // negative div
            Json::Value req;
            Json::Reader().parse(
                R"({
                "fee_div_max" : -2,
                "tx_json" : { }
            })",
                req);
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrackOuter,
                env.app().getTxQ(),
                env.app());

            BEAST_EXPECT(RPC::contains_error(result));
        }

        {
            // negative mult & div
            Json::Value req;
            Json::Reader().parse(
                R"({
                "fee_mult_max" : -2,
                "fee_div_max" : -3,
                "tx_json" : { }
            })",
                req);
            Json::Value result = checkFee(
                req,
                Role::ADMIN,
                true,
                env.app().config(),
                feeTrackOuter,
                env.app().getTxQ(),
                env.app());

            BEAST_EXPECT(RPC::contains_error(result));
        }

        env.close();

        {
            // Call "sign" with nothing in the open ledger
            Json::Value toSign;
            toSign[jss::tx_json] = noop(env.master);
            toSign[jss::secret] = "masterpassphrase";
            auto rpcResult = env.rpc("json", "sign", to_string(toSign));
            auto result = rpcResult[jss::result];

            BEAST_EXPECT(!RPC::contains_error(result));
            BEAST_EXPECT(
                result[jss::tx_json].isMember(jss::Fee) &&
                result[jss::tx_json][jss::Fee] == "10");
            BEAST_EXPECT(
                result[jss::tx_json].isMember(jss::Sequence) &&
                result[jss::tx_json][jss::Sequence].isConvertibleTo(
                    Json::ValueType::uintValue));
        }

        {
            // Call "sign" with enough transactions in the open ledger
            // to escalate the fee.
            for (;;)
            {
                auto metrics = env.app().getTxQ().getMetrics(*env.current());
                if (metrics.openLedgerFeeLevel > metrics.minProcessingFeeLevel)
                    break;
                env(noop(env.master));
            }

            Json::Value toSign;
            toSign[jss::tx_json] = noop(env.master);
            toSign[jss::secret] = "masterpassphrase";
            toSign[jss::fee_mult_max] = 900;
            auto rpcResult = env.rpc("json", "sign", to_string(toSign));
            auto result = rpcResult[jss::result];

            BEAST_EXPECT(!RPC::contains_error(result));
            BEAST_EXPECT(
                result[jss::tx_json].isMember(jss::Fee) &&
                result[jss::tx_json][jss::Fee] == "7813");
            BEAST_EXPECT(
                result[jss::tx_json].isMember(jss::Sequence) &&
                result[jss::tx_json][jss::Sequence].isConvertibleTo(
                    Json::ValueType::uintValue));

            env.close();
        }

        {
            // Call "sign" with higher server load
            {
                auto& feeTrack = env.app().getFeeTrack();
                BEAST_EXPECT(feeTrack.getLoadFactor() == 256);
                for (int i = 0; i < 8; ++i)
                    feeTrack.raiseLocalFee();
                BEAST_EXPECT(feeTrack.getLoadFactor() == 1220);
            }

            Json::Value toSign;
            toSign[jss::tx_json] = noop(env.master);
            toSign[jss::secret] = "masterpassphrase";
            auto rpcResult = env.rpc("json", "sign", to_string(toSign));
            auto result = rpcResult[jss::result];

            BEAST_EXPECT(!RPC::contains_error(result));
            BEAST_EXPECT(
                result[jss::tx_json].isMember(jss::Fee) &&
                result[jss::tx_json][jss::Fee] == "47");
            BEAST_EXPECT(
                result[jss::tx_json].isMember(jss::Sequence) &&
                result[jss::tx_json][jss::Sequence].isConvertibleTo(
                    Json::ValueType::uintValue));
        }

        {
            // Call "sign" with higher server load and
            // enough transactions to escalate the fee
            BEAST_EXPECT(feeTrackOuter.getLoadFactor() == 1220);

            for (;;)
            {
                auto metrics = env.app().getTxQ().getMetrics(*env.current());
                if (metrics.openLedgerFeeLevel > metrics.minProcessingFeeLevel)
                    break;
                env(noop(env.master), fee(47));
            }

            Env_ss envs(env);

            Json::Value toSign;
            toSign[jss::tx_json] = noop(env.master);
            toSign[jss::secret] = "masterpassphrase";
            // Max fee = 7000 drops
            toSign[jss::fee_mult_max] = 700;
            auto rpcResult = env.rpc("json", "sign", to_string(toSign));
            auto result = rpcResult[jss::result];

            BEAST_EXPECT(!RPC::contains_error(result));
            BEAST_EXPECT(
                result[jss::tx_json].isMember(jss::Fee) &&
                result[jss::tx_json][jss::Fee] == "6806");
            BEAST_EXPECT(
                result[jss::tx_json].isMember(jss::Sequence) &&
                result[jss::tx_json][jss::Sequence].isConvertibleTo(
                    Json::ValueType::uintValue));
        }
    }

    void
    testAutoFillNetworkID()
    {
        testcase("autofill NetworkID");
        using namespace test::jtx;
        Env env{*this, envconfig([&](std::unique_ptr<Config> cfg) {
                    cfg->NETWORK_ID = 1025;
                    return cfg;
                })};

        {
            Json::Value toSign;
            toSign[jss::tx_json] = noop(env.master);

            BEAST_EXPECT(!toSign[jss::tx_json].isMember(jss::NetworkID));
            toSign[jss::secret] = "masterpassphrase";
            auto rpcResult = env.rpc("json", "sign", to_string(toSign));
            auto result = rpcResult[jss::result];

            BEAST_EXPECT(!RPC::contains_error(result));
            BEAST_EXPECT(
                result[jss::tx_json].isMember(jss::NetworkID) &&
                result[jss::tx_json][jss::NetworkID] == 1025);
        }
    }

    // A function that can be called as though it would process a transaction.
    static void
    fakeProcessTransaction(
        std::shared_ptr<Transaction>&,
        bool,
        bool,
        NetworkOPs::FailHard)
    {
        ;
    }

    void
    testTransactionRPC()
    {
        testcase("sign/submit RPCs");
        using namespace std::chrono_literals;
        using namespace test::jtx;
        // Use jtx to set up a ledger so the tests will do the right thing.
        Account const a{"a"};  // rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA
        Account const g{"g"};  // rLPwWB1itaUGMV8kbMLLysjGkEpTM2Soy4
        auto const USD = g["USD"];

        // Account: rJrxi4Wxev4bnAGVNP9YCdKPdAoKfAmcsi
        // seed:    sh1yJfwoi98zCygwijUzuHmJDeVKd
        Account const ed{"ed", KeyType::ed25519};
        // master is rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh.
        // "b" (not in the ledger) is rDg53Haik2475DJx8bjMDSDPj4VX7htaMd.
        // "c" (phantom signer) is rPcNzota6B8YBokhYtcTNqQVCngtbnWfux.

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->FEES.reference_fee = 10;
            return cfg;
        }));
        env.fund(XRP(100000), a, ed, g);
        env.close();

        env(trust(a, USD(1000)));
        env(trust(env.master, USD(1000)));
        env(pay(g, a, USD(50)));
        env(pay(g, env.master, USD(50)));
        env.close();

        ProcessTransactionFn processTxn = fakeProcessTransaction;

        // A list of all the functions we want to test.
        using signFunc = Json::Value (*)(
            Json::Value params,
            unsigned int apiVersion,
            NetworkOPs::FailHard failType,
            Role role,
            std::chrono::seconds validatedLedgerAge,
            Application& app);

        using submitFunc = Json::Value (*)(
            Json::Value params,
            unsigned int apiVersion,
            NetworkOPs::FailHard failType,
            Role role,
            std::chrono::seconds validatedLedgerAge,
            Application& app,
            ProcessTransactionFn const& processTransaction);

        using TestStuff =
            std::tuple<signFunc, submitFunc, char const*, unsigned int>;

        static TestStuff const testFuncs[] = {
            TestStuff{transactionSign, nullptr, "sign", 0},
            TestStuff{nullptr, transactionSubmit, "submit", 1},
            TestStuff{transactionSignFor, nullptr, "sign_for", 2},
            TestStuff{
                nullptr,
                transactionSubmitMultiSigned,
                "submit_multisigned",
                3}};

        for (auto testFunc : testFuncs)
        {
            // For each JSON test.
            for (auto const& txnTest : txnTestArray)
            {
                Json::Value req;
                Json::Reader().parse(txnTest.json, req);
                if (RPC::contains_error(req))
                    Throw<std::runtime_error>(
                        "Internal JSONRPC_test error.  Bad test JSON.");

                static Role const testedRoles[] = {
                    Role::GUEST, Role::USER, Role::ADMIN, Role::FORBID};

                for (Role testRole : testedRoles)
                {
                    Json::Value result;
                    auto const signFn = get<0>(testFunc);
                    if (signFn != nullptr)
                    {
                        assert(get<1>(testFunc) == nullptr);
                        result = signFn(
                            req,
                            1,
                            NetworkOPs::FailHard::yes,
                            testRole,
                            1s,
                            env.app());
                    }
                    else
                    {
                        auto const submitFn = get<1>(testFunc);
                        assert(submitFn != nullptr);
                        result = submitFn(
                            req,
                            1,
                            NetworkOPs::FailHard::yes,
                            testRole,
                            1s,
                            env.app(),
                            processTxn);
                    }

                    std::string errStr;
                    if (RPC::contains_error(result))
                        errStr = result["error_message"].asString();

                    if (errStr == txnTest.expMsg[get<3>(testFunc)])
                    {
                        pass();
                    }
                    else
                    {
                        std::ostringstream description;
                        description << txnTest.description << "  Called "
                                    << get<2>(testFunc) << "().  Got \'"
                                    << errStr << "\'";
                        fail(description.str(), __FILE__, txnTest.line);
                    }
                }
            }
        }
    }

    void
    run() override
    {
        testBadRpcCommand();
        testAutoFillFails();
        testAutoFillFees();
        testAutoFillEscalatedFees();
        testAutoFillNetworkID();
        testTransactionRPC();
    }
};

BEAST_DEFINE_TESTSUITE(JSONRPC, ripple_app, ripple);

}  // namespace RPC
}  // namespace ripple
