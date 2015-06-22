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
#include <ripple/app/paths/FindPaths.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/json/json_reader.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/rpc/impl/TransactionSign.h>
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
            "issuer": "0123456789012345678901234567890123456789"
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
            "issuer": "0123456789012345678901234567890123456789"
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
            "issuer": "0123456789012345678901234567890123456789"
        },
        "SendMax": {
            "value": "5",
            "currency": "USD",
            "issuer": "0123456789012345678901234567890123456789"
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
            "issuer": "0123456789012345678901234567890123456789"
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
"",
"",
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
"",
"",
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
"",
"",
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
"",
"",
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
"",
"",
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

};


class JSONRPC_test : public beast::unit_test::suite
{
public:
    void testAutoFillFees ()
    {
        Config const config;
        auto const ledger =
            std::make_shared<Ledger>(
                create_genesis, config);

        using namespace detail;
        TxnSignApiFacade apiFacade (TxnSignApiFacade::noNetOPs, ledger);

        {
            Json::Value req;
            Json::Reader ().parse (
                "{ \"fee_mult_max\" : 1, \"tx_json\" : { } } ", req);
            Json::Value result =
                checkFee (req, apiFacade, Role::ADMIN, AutoFill::might);

            expect (! RPC::contains_error (result), "Legal checkFee");
        }

        {
            Json::Value req;
            Json::Reader ().parse (
                "{ \"fee_mult_max\" : 0, \"tx_json\" : { } } ", req);
            Json::Value result =
                checkFee (req, apiFacade, Role::ADMIN, AutoFill::might);

            expect (RPC::contains_error (result), "Invalid checkFee");
        }
    }

    void testTransactionRPC ()
    {
        // A list of all the functions we want to test and their fail bits.
        using transactionFunc = Json::Value (*) (
            Json::Value params,
            NetworkOPs::FailHard failType,
            detail::TxnSignApiFacade& apiFacade,
            Role role);

        using TestStuff =
            std::tuple <transactionFunc, char const*, unsigned int>;

        static TestStuff const testFuncs [] =
        {
            TestStuff {transactionSign,              "sign",               0},
            TestStuff {transactionSubmit,            "submit",             1},
            TestStuff {transactionSignFor,           "sign_for",           2},
            TestStuff {transactionSubmitMultiSigned, "submit_multisigned", 3}
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
                    // Mock so we can run without a ledger.
                    detail::TxnSignApiFacade apiFacade (
                        detail::TxnSignApiFacade::noNetOPs);

                    Json::Value result = get<0>(testFunc) (
                        req,
                        NetworkOPs::FailHard::yes,
                        apiFacade,
                        testRole);

                    std::string errStr;
                    if (RPC::contains_error (result))
                        errStr = result["error_message"].asString ();

                    std::string const expStr (txnTest.expMsg[get<2>(testFunc)]);
                    expect (errStr == expStr,
                        "Expected: \"" + expStr + "\"\n  Got: \"" + errStr +
                        "\"\nIn " + std::string (get<1>(testFunc)) +
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

