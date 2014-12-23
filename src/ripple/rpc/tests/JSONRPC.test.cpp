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

#include <ripple/app/paths/FindPaths.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/json/json_reader.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/rpc/impl/TransactionSign.h>
#include <beast/unit_test.h>

namespace ripple {

namespace RPC {

// Struct used to test calls to transactionSign and transactionSubmit.
struct TxnTestData
{
    // Gah, without constexpr I can't make this an enum and initialize
    // OR operators at compile time.  Punting with integer constants.
    static unsigned int const allGood         = 0x0;
    static unsigned int const signFail        = 0x1;
    static unsigned int const submitFail      = 0x2;

    char const* const json;
    unsigned int result;

    TxnTestData () = delete;
    TxnTestData (TxnTestData const&) = delete;
    TxnTestData& operator= (TxnTestData const&) = delete;
    TxnTestData (char const* jsonIn, unsigned int resultIn)
    : json (jsonIn)
    , result (resultIn)
    { }
};

// Declare storage for statics to avoid link errors.
unsigned int const TxnTestData::allGood;
unsigned int const TxnTestData::signFail;
unsigned int const TxnTestData::submitFail;


static TxnTestData const txnTestArray [] =
{

// Minimal payment.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// Pass in Fee with minimal payment.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Fee": 10,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// Pass in Sequence.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// Pass in Sequence and Fee with minimal payment.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Sequence": 0,
        "Fee": 10,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// Add "fee_mult_max" field.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "fee_mult_max": 7,
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// "fee_mult_max is ignored if "Fee" is present.
{R"({
    "command": "submit",
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
})", TxnTestData::allGood},

// Invalid "fee_mult_max" field.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "fee_mult_max": "NotAFeeMultiplier",
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// Invalid value for "fee_mult_max" field.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "fee_mult_max": 0,
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// Missing "Amount".
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// Invalid "Amount".
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "NotAnAmount",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// Missing "Destination".
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// Invalid "Destination".
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "NotADestination",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// Cannot create XRP to XRP paths.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "build_path": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// Successful "build_path".
{R"({
    "command": "submit",
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
})", TxnTestData::allGood},

// Not valid to include both "Paths" and "build_path".
{R"({
    "command": "submit",
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
})", TxnTestData::signFail | TxnTestData::submitFail},

// Successful "SendMax".
{R"({
    "command": "submit",
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
})", TxnTestData::allGood},

// Even though "Amount" may not be XRP for pathfinding, "SendMax" may be XRP.
{R"({
    "command": "submit",
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
})", TxnTestData::allGood},

// "secret" must be present.
{R"({
    "command": "submit",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// "secret" must be non-empty.
{R"({
    "command": "submit",
    "secret": "",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// "tx_json" must be present.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "rx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// "TransactionType" must be present.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// The "TransactionType" must be one of the pre-established transaction types.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "tt"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// The "TransactionType", however, may be represented with an integer.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": 0
    }
})", TxnTestData::allGood},

// "Account" must be present.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// "Account" must be well formed.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "NotAnAccount",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// The "offline" tag may be added to the transaction.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "offline": 0,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// If "offline" is true then a "Sequence" field must be supplied.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "offline": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// Valid transaction if "offline" is true.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "offline": 1,
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// A "Flags' field may be specified.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Flags": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

// The "Flags" field must be numeric.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Flags": "NotGoodFlags",
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::signFail | TxnTestData::submitFail},

// It's okay to add a "debug_signing" field.
{R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "debug_signing": 0,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allGood},

};

class JSONRPC_test : public beast::unit_test::suite
{
public:
    void testAutoFillFees ()
    {
        RippleAddress rootSeedMaster
                = RippleAddress::createSeedGeneric ("masterpassphrase");
        RippleAddress rootGeneratorMaster
                = RippleAddress::createGeneratorPublic (rootSeedMaster);
        RippleAddress rootAddress
                = RippleAddress::createAccountPublic (rootGeneratorMaster, 0);
        std::uint64_t startAmount (100000);
        Ledger::pointer ledger (std::make_shared <Ledger> (
            rootAddress, startAmount));

        using namespace RPCDetail;
        LedgerFacade facade (LedgerFacade::noNetOPs, ledger);

       {
            Json::Value req;
            Json::Value result;
            Json::Reader ().parse (
                R"({ "fee_mult_max" : 1, "tx_json" : { } } )"
                , req);
            autofill_fee (req, facade, result, true);

            expect (! contains_error (result));
        }

        {
            Json::Value req;
            Json::Value result;
            Json::Reader ().parse (
                R"({ "fee_mult_max" : 0, "tx_json" : { } } )"
                , req);
            autofill_fee (req, facade, result, true);

            expect (contains_error (result));
        }
    }

    void testTransactionRPC ()
    {
        // This loop is forward-looking for when there are separate
        // transactionSign () and transcationSubmit () functions.  For now
        // they just have a bool (false = sign, true = submit) and a flag
        // to help classify failure types.
        using TestStuff = std::pair <bool, unsigned int>;
        static TestStuff const testFuncs [] =
        {
            TestStuff {false, TxnTestData::signFail},
            TestStuff {true,  TxnTestData::submitFail},
        };

        for (auto testFunc : testFuncs)
        {
            // For each JSON test.
            for (auto const& txnTest : txnTestArray)
            {
                Json::Value req;
                Json::Reader ().parse (txnTest.json, req);
                if (contains_error (req))
                    throw std::runtime_error (
                        "Internal JSONRPC_test error.  Bad test JSON.");

                static Role const testedRoles[] =
                    {Role::GUEST, Role::USER, Role::ADMIN, Role::FORBID};

                for (Role testRole : testedRoles)
                {
                    // Mock so we can run without a ledger.
                    RPCDetail::LedgerFacade fakeNetOPs (
                        RPCDetail::LedgerFacade::noNetOPs);

                    Json::Value result = transactionSign (
                        req,
                        testFunc.first,
                        true,
                        fakeNetOPs,
                        testRole);

                    expect (contains_error (result) ==
                        static_cast <bool> (txnTest.result & testFunc.second));
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
