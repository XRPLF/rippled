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

#include <BeastConfig.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/json/json_reader.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/rpc/impl/TransactionSign.h>
#include <ripple/test/jtx.h>
#include <beast/unit_test/suite.h>

namespace ripple {

namespace RPC {

struct TxnTestData
{
    char const* const description;
    char const* const json;
    char const* const expMsg[4];

    // Default and copy ctors should be deleted, but that displeases gcc 4.6.3.
//  TxnTestData () = delete;
//  TxnTestData (TxnTestData const&) = delete;
//  TxnTestData (TxnTestData&&) = delete;
    TxnTestData& operator= (TxnTestData const&) = delete;
    TxnTestData& operator= (TxnTestData&&) = delete;
};

static TxnTestData const txnTestArray [] =
{

{ "Minimal payment.",
R"({
    "command": "doesnt_matter",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
{
"",
"",
"Missing field 'account'.",
"Missing field 'tx_json.Sequence'."}},

{ "Pass in Fee with minimal payment.",
R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Fee": 10,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
{
"",
"",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "Pass in Sequence.",
R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
{
"",
"",
"Missing field 'tx_json.SigningPubKey'.",
"Missing field 'tx_json.SigningPubKey'."}},

{ "Pass in Sequence and Fee with minimal payment.",
R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "tx_json": {
        "Sequence": 0,
        "Fee": 10,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
{
"",
"",
"Missing field 'tx_json.SigningPubKey'.",
"Missing field 'tx_json.SigningPubKey'."}},

{ "Add 'fee_mult_max' field.",
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
{
"",
"",
"Missing field 'tx_json.SigningPubKey'.",
"Missing field 'tx_json.SigningPubKey'."}},

{ "fee_mult_max is ignored if 'Fee' is present.",
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
{
"",
"",
"Missing field 'tx_json.SigningPubKey'.",
"Missing field 'tx_json.SigningPubKey'."}},

{ "Invalid 'fee_mult_max' field.",
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
{
"Invalid field 'fee_mult_max', not a number.",
"Invalid field 'fee_mult_max', not a number.",
"Missing field 'tx_json.SigningPubKey'.",
"Missing field 'tx_json.SigningPubKey'."}},

{ "Invalid value for 'fee_mult_max' field.",
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
{
"Fee of 10 exceeds the requested tx limit of 0",
"Fee of 10 exceeds the requested tx limit of 0",
"Missing field 'tx_json.SigningPubKey'.",
"Missing field 'tx_json.SigningPubKey'."}},

{ "Missing 'Amount'.",
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
{
"Missing field 'tx_json.Amount'.",
"Missing field 'tx_json.Amount'.",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "Invalid 'Amount'.",
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
{
"Invalid field 'tx_json.Amount'.",
"Invalid field 'tx_json.Amount'.",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "Missing 'Destination'.",
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
{
"Missing field 'tx_json.Destination'.",
"Missing field 'tx_json.Destination'.",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "Invalid 'Destination'.",
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
{
"Invalid field 'tx_json.Destination'.",
"Invalid field 'tx_json.Destination'.",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "Cannot create XRP to XRP paths.",
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
{
"Cannot build XRP to XRP paths.",
"Cannot build XRP to XRP paths.",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "Successful 'build_path'.",
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
{
"",
"",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "Not valid to include both 'Paths' and 'build_path'.",
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
{
"Cannot specify both 'tx_json.Paths' and 'build_path'",
"Cannot specify both 'tx_json.Paths' and 'build_path'",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "Successful 'SendMax'.",
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
{
"",
"",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "Even though 'Amount' may not be XRP for pathfinding, 'SendMax' may be XRP.",
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
{
"",
"",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "'secret' must be present.",
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
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "'secret' must be non-empty.",
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
{
"Invalid field 'secret'.",
"Invalid field 'secret'.",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "'tx_json' must be present.",
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
{
"Missing field 'tx_json'.",
"Missing field 'tx_json'.",
"Missing field 'tx_json'.",
"Missing field 'tx_json'."}},

{ "'TransactionType' must be present.",
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
{
"Missing field 'tx_json.TransactionType'.",
"Missing field 'tx_json.TransactionType'.",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "The 'TransactionType' must be one of the pre-established transaction types.",
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
{
"Field 'tx_json.TransactionType' has invalid data.",
"Field 'tx_json.TransactionType' has invalid data.",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "The 'TransactionType', however, may be represented with an integer.",
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
{
"",
"",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "'Account' must be present.",
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
{
"Missing field 'tx_json.Account'.",
"Missing field 'tx_json.Account'.",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "'Account' must be well formed.",
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
{
"Invalid field 'tx_json.Account'.",
"Invalid field 'tx_json.Account'.",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "The 'offline' tag may be added to the transaction.",
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
{
"",
"",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "If 'offline' is true then a 'Sequence' field must be supplied.",
R"({
    "command": "doesnt_matter",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "secret": "masterpassphrase",
    "offline": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})",
{
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "Valid transaction if 'offline' is true.",
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
{
"",
"",
"Missing field 'tx_json.SigningPubKey'.",
"Missing field 'tx_json.SigningPubKey'."}},

{ "A 'Flags' field may be specified.",
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
{
"",
"",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "The 'Flags' field must be numeric.",
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
{
"Field 'tx_json.Flags' has invalid data.",
"Field 'tx_json.Flags' has invalid data.",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "It's okay to add a 'debug_signing' field.",
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
{
"",
"",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "Minimal sign_for.",
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
{
"Secret does not match account.",
"Secret does not match account.",
"",
"Missing field 'Signers'."}},

{ "Missing 'Account' in sign_for.",
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
{
"Missing field 'tx_json.Account'.",
"Missing field 'tx_json.Account'.",
"Missing field 'tx_json.Account'.",
"Missing field 'tx_json.Account'."}},

{ "Missing 'Amount' in sign_for.",
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
{
"Missing field 'tx_json.Amount'.",
"Missing field 'tx_json.Amount'.",
"Missing field 'tx_json.Amount'.",
"Missing field 'tx_json.Amount'."}},

{ "Missing 'Destination' in sign_for.",
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
{
"Missing field 'tx_json.Destination'.",
"Missing field 'tx_json.Destination'.",
"Missing field 'tx_json.Destination'.",
"Missing field 'tx_json.Destination'."}},

{ "Missing 'Fee' in sign_for.",
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
{
"Secret does not match account.",
"Secret does not match account.",
"Missing field 'tx_json.Fee'.",
"Missing field 'tx_json.Fee'."}},

{ "Missing 'Sequence' in sign_for.",
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
{
"Secret does not match account.",
"Secret does not match account.",
"Missing field 'tx_json.Sequence'.",
"Missing field 'tx_json.Sequence'."}},

{ "Missing 'SigningPubKey' in sign_for.",
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
{
"Secret does not match account.",
"Secret does not match account.",
"Missing field 'tx_json.SigningPubKey'.",
"Missing field 'tx_json.SigningPubKey'."}},

{ "Non-empty 'SigningPubKey' in sign_for.",
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
{
"Secret does not match account.",
"Secret does not match account.",
"When multi-signing 'tx_json.SigningPubKey' must be empty.",
"When multi-signing 'tx_json.SigningPubKey' must be empty."}},

{ "Missing 'TransactionType' in sign_for.",
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
{
"Missing field 'tx_json.TransactionType'.",
"Missing field 'tx_json.TransactionType'.",
"Missing field 'tx_json.TransactionType'.",
"Missing field 'tx_json.TransactionType'."}},

{ "Minimal submit_multisigned.",
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
    ],
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
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
""}},

{ "Missing tx_json in submit_multisigned.",
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
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Missing field 'tx_json'."}},

{ "Missing sequence in submit_multisigned.",
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
    ],
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Missing field 'tx_json.Sequence'."}},

{ "Missing SigningPubKey in submit_multisigned.",
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
    ],
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "TransactionType": "Payment"
    }
})",
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Missing field 'tx_json.SigningPubKey'."}},

{ "Non-empty SigningPubKey in submit_multisigned.",
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
    ],
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8",
        "TransactionType": "Payment"
    }
})",
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"When multi-signing 'tx_json.SigningPubKey' must be empty."}},

{ "Missing TransactionType in submit_multisigned.",
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
    ],
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
    }
})",
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Missing field 'tx_json.TransactionType'."}},

{ "Missing Account in submit_multisigned.",
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
    ],
    "tx_json": {
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Missing field 'tx_json.Account'."}},

{ "Malformed Account in submit_multisigned.",
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
    ],
    "tx_json": {
        "Account": "NotAnAccount",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Invalid field 'tx_json.Account'."}},

{ "Account not in ledger in submit_multisigned.",
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
    ],
    "tx_json": {
        "Account": "rDg53Haik2475DJx8bjMDSDPj4VX7htaMd",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Source account not found."}},

{ "Missing Fee in submit_multisigned.",
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
    ],
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Missing field 'tx_json.Fee'."}},

{ "Non-numeric Fee in submit_multisigned.",
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
    ],
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50.1,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Field 'tx_json.Fee' has invalid data."}},

{ "Missing Amount in submit_multisigned Payment.",
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
    ],
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50000000,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Missing field 'tx_json.Amount'."}},

{ "Invalid Amount in submit_multisigned Payment.",
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
    ],
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "NotANumber",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Invalid field 'tx_json.Amount'."}},

{ "No build_path in submit_multisigned.",
R"({
    "command": "submit_multisigned",
    "build_path": 1,
    "Signers": [
        {
            "Signer": {
                "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
            }
        }
    ],
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
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Field 'build_path' not allowed in this context."}},

{ "Missing Destination in submit_multisigned Payment.",
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
    ],
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Missing field 'tx_json.Destination'."}},

{ "Malformed Destination in submit_multisigned Payment.",
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
    ],
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "NotADestination",
        "Fee": 50,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})",
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Invalid field 'tx_json.Destination'."}},

{ "Missing Signers field in submit_multisigned.",
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
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Missing field 'Signers'."}},

{ "Signers not an array in submit_multisigned.",
R"({
    "command": "submit_multisigned",
    "Signers": {
        "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
        "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
        "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
    },
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
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Expected Signers to be an array."}},

{ "Empty Signers array in submit_multisigned.",
R"({
    "command": "submit_multisigned",
    "Signers": [
    ],
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
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Signers array may not be empty."}},

{ "Duplicate Signer in submit_multisigned.",
R"({
    "command": "submit_multisigned",
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
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"Duplicate Signers:Signer:Account entries (rPcNzota6B8YBokhYtcTNqQVCngtbnWfux) are not allowed."}},

{ "Signer is tx_json Account in submit_multisigned.",
R"({
    "command": "submit_multisigned",
    "Signers": [
        {
            "Signer": {
                "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
                "TxnSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                "SigningPubKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
            }
        }
    ],
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
{
"Missing field 'secret'.",
"Missing field 'secret'.",
"Missing field 'account'.",
"A Signer may not be the transaction's Account (rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh)."}},

};


class JSONRPC_test : public beast::unit_test::suite
{
public:
    void testAutoFillFees ()
    {
        test::jtx::Env env(*this);
        Config const config;
        std::shared_ptr<const ReadView> ledger =
            std::make_shared<Ledger>(create_genesis, config, env.app().family());
        LoadFeeTrack const feeTrack;

        {
            Json::Value req;
            Json::Reader ().parse (
                "{ \"fee_mult_max\" : 1, \"tx_json\" : { } } ", req);
            Json::Value result =
                checkFee (req, Role::ADMIN, true, feeTrack, ledger);

            expect (! RPC::contains_error (result), "Legal checkFee");
        }

        {
            Json::Value req;
            Json::Reader ().parse (
                "{ \"fee_mult_max\" : 0, \"tx_json\" : { } } ", req);
            Json::Value result =
                checkFee (req, Role::ADMIN, true, feeTrack, ledger);

            expect (RPC::contains_error (result), "Invalid checkFee");
        }
    }

    // A function that can be called as though it would process a transaction.
    static void fakeProcessTransaction (
        Transaction::pointer&, bool, bool, NetworkOPs::FailHard)
    {
        ;
    }

    void testTransactionRPC ()
    {
        // Use jtx to set up a ledger so the tests will do the right thing.
        test::jtx::Account const a {"a"}; // rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA
        test::jtx::Account const g {"g"}; // rLPwWB1itaUGMV8kbMLLysjGkEpTM2Soy4
        auto const USD = g["USD"];
        // master is rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh.
        // "b" (not in the ledger) is rDg53Haik2475DJx8bjMDSDPj4VX7htaMd.
        // "c" (phantom signer) is rPcNzota6B8YBokhYtcTNqQVCngtbnWfux.

        test::jtx::Env env(*this);
        env.fund(test::jtx::XRP(100000), a, g);
        env.close();

        env(trust(a, USD(1000)));
        env(trust(env.master, USD(1000)));
        env(pay(g, a, USD(50)));
        env(pay(g, env.master, USD(50)));
        env.close();

        auto const ledger = env.open();

        ProcessTransactionFn processTxn = fakeProcessTransaction;

        // A list of all the functions we want to test.
        using signFunc = Json::Value (*) (
            Json::Value params,
            NetworkOPs::FailHard failType,
            Role role,
            int validatedLedgerAge,
            Application& app,
            std::shared_ptr<ReadView const> ledger);

        using submitFunc = Json::Value (*) (
            Json::Value params,
            NetworkOPs::FailHard failType,
            Role role,
            int validatedLedgerAge,
            Application& app,
            std::shared_ptr<ReadView const> ledger,
            ProcessTransactionFn const& processTransaction);

        using TestStuff =
            std::tuple <signFunc, submitFunc, char const*, unsigned int>;

        static TestStuff const testFuncs [] =
        {
            TestStuff {transactionSign, nullptr,              "sign",               0},
            TestStuff {nullptr, transactionSubmit,            "submit",             1},
            TestStuff {transactionSignFor, nullptr,           "sign_for",           2},
            TestStuff {nullptr, transactionSubmitMultiSigned, "submit_multisigned", 3}
        };

        for (auto testFunc : testFuncs)
        {
            // For each JSON test.
            for (auto const& txnTest : txnTestArray)
            {
                Json::Value req;
                Json::Reader ().parse (txnTest.json, req);
                if (RPC::contains_error (req))
                    throw std::runtime_error (
                        "Internal JSONRPC_test error.  Bad test JSON.");

                static Role const testedRoles[] =
                    {Role::GUEST, Role::USER, Role::ADMIN, Role::FORBID};

                for (Role testRole : testedRoles)
                {
                    Json::Value result;
                    auto const signFn = get<0>(testFunc);
                    if (signFn != nullptr)
                    {
                        assert (get<1>(testFunc) == nullptr);
                        result = signFn (
                            req,
                            NetworkOPs::FailHard::yes,
                            testRole,
                            1,
                            env.app(),
                            ledger);
                    }
                    else
                    {
                        auto const submitFn = get<1>(testFunc);
                        assert (submitFn != nullptr);
                        result = submitFn (
                            req,
                            NetworkOPs::FailHard::yes,
                            testRole,
                            1,
                            env.app(),
                            ledger,
                            processTxn);
                    }

                    std::string errStr;
                    if (RPC::contains_error (result))
                        errStr = result["error_message"].asString ();

                    std::string const expStr (txnTest.expMsg[get<3>(testFunc)]);
                    expect (errStr == expStr,
                        "Expected: \"" + expStr + "\"\n  Got: \"" + errStr +
                        "\"\nIn " + std::string (get<2>(testFunc)) +
                            ": " + txnTest.description);
                }
            }
        }
    }

    void run ()
    {
        testAutoFillFees ();
        testTransactionRPC ();
    }
};

BEAST_DEFINE_TESTSUITE(JSONRPC,ripple_app,ripple);

} // RPC
} // ripple

