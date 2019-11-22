//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.
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


#include <ripple/net/RPCCall.h>
#include <ripple/beast/unit_test.h>
#include <ripple/json/json_reader.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <test/jtx.h>

#include <initializer_list>
#include <vector>
#include <boost/algorithm/string.hpp>

namespace ripple {
namespace test {

struct RPCCallTestData
{
    char const* const description;
    int const line;
    // List of passed arguments.
    std::vector<char const*> const args;

    // If it throws, what does it throw?
    enum Exception
    {
        no_exception = 0,
        bad_cast
    };
    Exception const throwsWhat;

    // Expected JSON response.
    char const* const exp;

    RPCCallTestData (char const* description_, int line_,
        std::initializer_list<char const*> const& args_,
        Exception throwsWhat_, char const* exp_)
    : description (description_)
    , line (line_)
    , args (args_)
    , throwsWhat (throwsWhat_)
    , exp (exp_)
    { }

    RPCCallTestData () = delete;
    RPCCallTestData (RPCCallTestData const&) = delete;
    RPCCallTestData (RPCCallTestData&&) = delete;
    RPCCallTestData& operator= (RPCCallTestData const&) = delete;
    RPCCallTestData& operator= (RPCCallTestData&&) = delete;
};

static RPCCallTestData const rpcCallTestArray [] =
{
// account_channels ------------------------------------------------------------
{
    "account_channels: minimal.", __LINE__,
    {
        "account_channels",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_channels",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
      }
    ]
    })"
},
{
    "account_channels: account and ledger hash.", __LINE__,
    {
        "account_channels",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_channels",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "destination_account" : "FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210"
      }
    ]
    })"
},
{
    "account_channels: account and ledger index.", __LINE__,
    {
        "account_channels",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "closed"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_channels",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "destination_account" : "closed"
      }
    ]
    })"
},
{
    "account_channels: two accounts.", __LINE__,
    {
        "account_channels",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_channels",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "destination_account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
      }
    ]
    })"
},
{
    "account_channels: two accounts and ledger hash.", __LINE__,
    {
        "account_channels",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_channels",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "destination_account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
         "ledger_hash" : "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
      }
    ]
    })"
},
{
    "account_channels: two accounts and ledger index.", __LINE__,
    {
        "account_channels",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "90210"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_channels",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "destination_account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
         "ledger_index" : 90210
      }
    ]
    })"
},
{
    "account_channels: too few arguments.", __LINE__,
    {
        "account_channels",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_channels",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "account_channels: too many arguments.", __LINE__,
    {
        "account_channels",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "current",
        "strict"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_channels",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "account_channels: invalid accountID.", __LINE__,
    {
        "account_channels",
        "",  // Note: very few values are detected as bad!
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_channels",
    "params" : [
      {
         "error" : "actMalformed",
         "error_code" : 35,
         "error_message" : "Account malformed."
      }
    ]
    })"
},

// account_currencies ----------------------------------------------------------
{
    "account_currencies: minimal.", __LINE__,
    {
        "account_currencies",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_currencies",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
      }
    ]
    })"
},
{
    "account_currencies: strict.", __LINE__,
    {
        "account_currencies",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "strict"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_currencies",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "strict" : 1
      }
    ]
    })"
},
{
    "account_currencies: ledger index.", __LINE__,
    {
        "account_currencies",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "42"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_currencies",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 42
      }
    ]
    })"
},
{
    "account_currencies: validated ledger.", __LINE__,
    {
        "account_currencies",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "validated"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_currencies",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : "validated"
      }
    ]
    })"
},
{
    "account_currencies: too few arguments.", __LINE__,
    {
        "account_currencies",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_currencies",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "account_currencies: too many arguments.", __LINE__,
    {
        "account_currencies",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "current",
        "strict"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_currencies",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "account_currencies: invalid second argument.", __LINE__,
    {
        "account_currencies",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "yup"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_currencies",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 0
      }
    ]
    })"
},
{
    "account_currencies: invalid accountID.", __LINE__,
    {
        "account_currencies",
        "",  // Note: very few values are detected as bad!
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_currencies",
    "params" : [
       {
          "error" : "actMalformed",
          "error_code" : 35,
          "error_message" : "Account malformed."
       }
    ]
    })",
},
{
    "account_currencies: floating point first argument.", __LINE__,
    {
        "account_currencies",
        "3.14159",
        "strict"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_currencies",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "3.14159",
         "strict" : 1
      }
    ]
    })"
},

// account_info ----------------------------------------------------------------
{
    "account_info: minimal.", __LINE__,
    {
        "account_info",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_info",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
      }
    ]
    })"
},
{
    "account_info: with numeric ledger index.", __LINE__,
    {
        "account_info",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "77777"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_info",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 77777
      }
    ]
    })"
},
{
    "account_info: with text ledger index.", __LINE__,
    {
        "account_info",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "closed"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_info",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : "closed"
      }
    ]
    })"
},
{
    "account_info: with ledger hash.", __LINE__,
    {
        "account_info",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_info",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_hash" : "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
      }
    ]
    })"
},
{
    // Note: this works, but it doesn't match the documentation.
    "account_info: strict.", __LINE__,
    {
        "account_info",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "strict"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_info",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "strict" : 1
      }
    ]
    })"
},
{
    // Note: Somewhat according to the docs, this is should be valid syntax.
    "account_info: with ledger index and strict.", __LINE__,
    {
        "account_info",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "validated",
        "strict"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_info",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "account_info: too few arguments.", __LINE__,
    {
        "account_info",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_info",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "account_info: too many arguments.", __LINE__,
    {
        "account_info",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "current",
        "strict",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_info",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "account_info: invalid accountID.", __LINE__,
    {
        "account_info",
        "",  // Note: very few values are detected as bad!
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_info",
    "params" : [
       {
          "error" : "actMalformed",
          "error_code" : 35,
          "error_message" : "Account malformed."
       }
    ]
    })",
},

// account_lines ---------------------------------------------------------------
{
    "account_lines: minimal.", __LINE__,
    {
        "account_lines",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_lines",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
      }
    ]
    })"
},
{
    "account_lines: peer.", __LINE__,
    {
        "account_lines",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_lines",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "peer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
      }
    ]
    })"
},
{
    "account_lines: peer and numeric ledger index.", __LINE__,
    {
        "account_lines",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "888888888"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_lines",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 888888888,
         "peer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
      }
    ]
    })"
},
{
    "account_lines: peer and text ledger index.", __LINE__,
    {
        "account_lines",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "closed"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_lines",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : "closed",
         "peer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
      }
    ]
    })"
},
{
    "account_lines: peer and ledger hash.", __LINE__,
    {
        "account_lines",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "FFFFEEEEDDDDCCCCBBBBAAAA9999888877776666555544443333222211110000"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_lines",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_hash" : "FFFFEEEEDDDDCCCCBBBBAAAA9999888877776666555544443333222211110000",
         "peer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
      }
    ]
    })"
},
{
    "account_lines: too few arguments.", __LINE__,
    {
        "account_lines",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_lines",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    // Note: I believe this _ought_ to be detected as too many arguments.
    "account_lines: four arguments.", __LINE__,
    {
        "account_lines",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "12345678",
        "current"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_lines",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 12345678,
         "peer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
      }
    ]
    })"
},
{
    // Note: I believe this _ought_ to be detected as too many arguments.
    "account_lines: five arguments.", __LINE__,
    {
        "account_lines",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "12345678",
        "current",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_lines",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 12345678,
         "peer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
      }
    ]
    })"
},
{
    "account_lines: too many arguments.", __LINE__,
    {
        "account_lines",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "12345678",
        "current",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "validated"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_lines",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "account_lines: first invalid accountID.", __LINE__,
    {
        "account_lines",
        "",  // Note: very few values are detected as bad!
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_lines",
    "params" : [
       {
          "error" : "actMalformed",
          "error_code" : 35,
          "error_message" : "Account malformed."
       }
    ]
    })",
},
{
    "account_lines: second invalid accountID.", __LINE__,
    {
        "account_lines",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        ""  // Note: very few values are detected as bad!
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_lines",
    "params" : [
       {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
       }
    ]
    })",
},
{
    "account_lines: invalid ledger selector.", __LINE__,
    {
        "account_lines",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "not_a_ledger"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_lines",
    "params" : [
       {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 0,
         "peer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
       }
    ]
    })",
},

// account_objects -------------------------------------------------------------
{
    "account_objects: minimal.", __LINE__,
    {
        "account_objects",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_objects",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
      }
    ]
    })"
},
{
    "account_objects: with numeric ledger index.", __LINE__,
    {
        "account_objects",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "77777"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_objects",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 77777
      }
    ]
    })"
},
{
    "account_objects: with text ledger index.", __LINE__,
    {
        "account_objects",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "closed"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_objects",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : "closed"
      }
    ]
    })"
},
{
    "account_objects: with ledger hash.", __LINE__,
    {
        "account_objects",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_objects",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_hash" : "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
      }
    ]
    })"
},
{
    // Note: this works, but it doesn't match the documentation.
    "account_objects: strict.", __LINE__,
    {
        "account_objects",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "strict"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_objects",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "strict" : 1
      }
    ]
    })"
},
{
    "account_objects: with ledger index and strict.", __LINE__,
    {
        "account_objects",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "validated",
        "strict"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_objects",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : "validated",
         "strict" : 1
      }
    ]
    })"
},
{
    "account_objects: too few arguments.", __LINE__,
    {
        "account_objects",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_objects",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    // Note: I believe this _ought_ to be detected as too many arguments.
    "account_objects: four arguments.", __LINE__,
    {
        "account_objects",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "current",
        "extra",
        "strict",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_objects",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "strict" : 1
      }
    ]
    })"
},
{
    // Note: I believe this _ought_ to be detected as too many arguments.
    "account_objects: five arguments.", __LINE__,
    {
        "account_objects",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "current",
        "extra1",
        "extra2",
        "strict",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_objects",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "strict" : 1
      }
    ]
    })"
},
{
    "account_objects: too many arguments.", __LINE__,
    {
        "account_objects",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "current",
        "extra1",
        "extra2",
        "extra3",
        "strict",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_objects",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "account_objects: invalid accountID.", __LINE__,
    {
        "account_objects",
        "",  // Note: very few values are detected as bad!
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_objects",
    "params" : [
       {
          "error" : "actMalformed",
          "error_code" : 35,
          "error_message" : "Account malformed."
       }
    ]
    })",
},
{
    // Note: there is code in place to return rpcLGR_IDX_MALFORMED.  That
    // cannot currently occur because jvParseLedger() always returns true.
    "account_objects: invalid ledger selection 1.", __LINE__,
    {
        "account_objects",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "no_ledger"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_objects",
    "params" : [
       {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 0
       }
    ]
    })",
},
{
    // Note: there is code in place to return rpcLGR_IDX_MALFORMED.  That
    // cannot currently occur because jvParseLedger() always returns true.
    "account_objects: invalid ledger selection 2.", __LINE__,
    {
        "account_objects",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "no_ledger",
        "strict"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_objects",
    "params" : [
       {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 0,
         "strict" : 1
       }
    ]
    })",
},

// account_offers --------------------------------------------------------------
{
    "account_offers: minimal.", __LINE__,
    {
        "account_offers",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_offers",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
      }
    ]
    })"
},
{
    "account_offers: with numeric ledger index.", __LINE__,
    {
        "account_offers",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "987654321"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_offers",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 987654321
      }
    ]
    })"
},
{
    "account_offers: with text ledger index.", __LINE__,
    {
        "account_offers",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "validated"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_offers",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : "validated"
      }
    ]
    })"
},
{
    "account_offers: with ledger hash.", __LINE__,
    {
        "account_offers",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_offers",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_hash" : "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
      }
    ]
    })"
},
{
    // Note: this works, but it doesn't match the documentation.
    "account_offers: strict.", __LINE__,
    {
        "account_offers",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "strict"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_offers",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "strict" : 1
      }
    ]
    })"
},
{
    // Note: this works, but doesn't match the documentation.
    "account_offers: with ledger index and strict.", __LINE__,
    {
        "account_offers",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "validated",
        "strict"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_offers",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : "validated",
         "strict" : 1
      }
    ]
    })"
},
{
    "account_offers: too few arguments.", __LINE__,
    {
        "account_offers",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_offers",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    // Note: I believe this _ought_ to be detected as too many arguments.
    "account_offers: four arguments.", __LINE__,
    {
        "account_offers",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "current",
        "extra",
        "strict",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_offers",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "strict" : 1
      }
    ]
    })"
},
{
    "account_offers: too many arguments.", __LINE__,
    {
        "account_offers",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "current",
        "extra1",
        "extra2",
        "strict",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_offers",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "account_offers: invalid accountID.", __LINE__,
    {
        "account_offers",
        "",  // Note: very few values are detected as bad!
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_offers",
    "params" : [
       {
          "error" : "actMalformed",
          "error_code" : 35,
          "error_message" : "Account malformed."
       }
    ]
    })",
},
{
    // Note: there is code in place to return rpcLGR_IDX_MALFORMED.  That
    // cannot currently occur because jvParseLedger() always returns true.
    "account_offers: invalid ledger selection 1.", __LINE__,
    {
        "account_offers",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "no_ledger"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_offers",
    "params" : [
       {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 0
       }
    ]
    })",
},
{
    // Note: there is code in place to return rpcLGR_IDX_MALFORMED.  That
    // cannot currently occur because jvParseLedger() always returns true.
    "account_offers: invalid ledger selection 2.", __LINE__,
    {
        "account_offers",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "no_ledger",
        "strict"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_offers",
    "params" : [
       {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 0,
         "strict" : 1
       }
    ]
    })",
},

// account_tx ------------------------------------------------------------------
{
    "account_tx: minimal.", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_tx",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
      }
    ]
    })"
},
{
    "account_tx: ledger_index .", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "444"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_tx",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 444
      }
    ]
    })"
},
{
    "account_tx: ledger_index plus trailing params.", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "707",
        "descending",
        "binary",
        "count"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_tx",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "count" : true,
         "binary" : true,
         "descending" : true,
         "ledger_index" : 707
      }
    ]
    })"
},
{
    "account_tx: ledger_index_min and _max.", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "-1",
        "-1"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_tx",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index_max" : -1,
         "ledger_index_min" : -1
      }
    ]
    })"
},
{
    "account_tx: ledger_index_min and _max plus trailing params.", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "-1",
        "413",
        "binary",
        "count",
        "descending"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_tx",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "binary" : true,
         "count" : true,
         "descending" : true,
         "ledger_index_max" : 413,
         "ledger_index_min" : -1
      }
    ]
    })"
},
{
    "account_tx: ledger_index_min and _max plus limit.", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "247",
        "-1",
        "300"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_tx",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index_max" : -1,
         "ledger_index_min" : 247,
         "limit" : 300
      }
    ]
    })"
},
{
    "account_tx: ledger_index_min and _max, limit, trailing args.", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "247",
        "-1",
        "300",
        "count",
        "descending",
        "binary"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_tx",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "binary" : true,
         "count" : true,
         "descending" : true,
         "ledger_index_max" : -1,
         "ledger_index_min" : 247,
         "limit" : 300
      }
    ]
    })"
},
{
    "account_tx: ledger_index_min and _max plus limit and offset.", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "589",
        "590",
        "67",
        "45"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_tx",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index_max" : 590,
         "ledger_index_min" : 589,
         "limit" : 67,
         "offset" : 45
      }
    ]
    })"
},
{
    "account_tx: ledger_index_min and _max, limit, offset, trailing.", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "589",
        "590",
        "67",
        "45",
        "descending",
        "count"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_tx",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "count" : true,
         "descending" : true,
         "ledger_index_max" : 590,
         "ledger_index_min" : 589,
         "limit" : 67,
         "offset" : 45
      }
    ]
    })"
},
{
    "account_tx: too few arguments.", __LINE__,
    {
        "account_tx",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_tx",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "account_tx: too many arguments.", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "589",
        "590",
        "67",
        "45",
        "extra",
        "descending",
        "count",
        "binary"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_tx",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "account_tx: invalid accountID.", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj9!VRWn96DkukG4bwdtyTh"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_tx",
    "params" : [
       {
          "error" : "actMalformed",
          "error_code" : 35,
          "error_message" : "Account malformed."
       }
    ]
    })",
},
{
    // Note: not currently detected as bad input.
    "account_tx: invalid ledger.", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "-478.7"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_tx",
    "params" : [
       {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 0
       }
    ]
    })",
},
{
    "account_tx: max less than min.", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "580",
        "579"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "account_tx",
    "params" : [
       {
         "error" : "lgrIdxsInvalid",
         "error_code" : 55,
         "error_message" : "Ledger indexes invalid."
       }
    ]
    })",
},
{
    // Note: this really shouldn't throw, but does at the moment.
    "account_tx: non-integer min.", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Binary",
        "-1"
    },
    RPCCallTestData::bad_cast,
    R"()",
},
{
    // Note: this really shouldn't throw, but does at the moment.
    "account_tx: non-integer max.", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "-1",
        "counts"
    },
    RPCCallTestData::bad_cast,
    R"()",
},
{
    // Note: this really shouldn't throw, but does at the moment.
    "account_tx: non-integer offset.", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "-1",
        "-1",
        "decending"
    },
    RPCCallTestData::bad_cast,
    R"()",
},
{
    // Note: this really shouldn't throw, but does at the moment.
    "account_tx: non-integer limit.", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "-1",
        "-1",
        "300",
        "false"
    },
    RPCCallTestData::bad_cast,
    R"()",
},
{
    // Note: this really shouldn't throw, but does at the moment.
    "account_tx: RIPD-1570.", __LINE__,
    {
        "account_tx",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "-1",
        "-1",
        "2",
        "false",
        "false",
        "false"
    },
    RPCCallTestData::bad_cast,
    R"()"
},

// book_offers -----------------------------------------------------------------
{
    "book_offers: minimal no issuer.", __LINE__,
    {
        "book_offers",
        "USD",
        "EUR",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "book_offers",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "taker_gets" : {
            "currency" : "EUR"
         },
         "taker_pays" : {
            "currency" : "USD"
         }
      }
    ]
    })"
},
{
    "book_offers: minimal with currency/issuer", __LINE__,
    {
        "book_offers",
        "USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "EUR/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "book_offers",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "taker_gets" : {
            "currency" : "EUR",
            "issuer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
         },
         "taker_pays" : {
            "currency" : "USD",
            "issuer" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
         }
      }
    ]
    })"
},
{
    // Note: documentation suggests that "issuer" is the wrong type.
    // Should it be "taker" instead?
    "book_offers: add issuer.", __LINE__,
    {
        "book_offers",
        "USD",
        "EUR",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "book_offers",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "issuer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
         "taker_gets" : {
            "currency" : "EUR"
         },
         "taker_pays" : {
            "currency" : "USD"
         }
      }
    ]
    })"
},
{
    "book_offers: add issuer and numeric ledger index.", __LINE__,
    {
        "book_offers",
        "USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "EUR",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "666"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "book_offers",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "issuer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
         "ledger_index" : 666,
         "taker_gets" : {
            "currency" : "EUR"
         },
         "taker_pays" : {
            "currency" : "USD",
            "issuer" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
         }
      }
    ]
    })"
},
{
    "book_offers: add issuer and text ledger index.", __LINE__,
    {
        "book_offers",
        "USD",
        "EUR/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "current"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "book_offers",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "issuer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
         "ledger_index" : "current",
         "taker_gets" : {
            "currency" : "EUR",
            "issuer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
         },
         "taker_pays" : {
            "currency" : "USD"
         }
      }
    ]
    })"
},
{
    "book_offers: add issuer and ledger hash.", __LINE__,
    {
        "book_offers",
        "USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "EUR/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "book_offers",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "issuer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
         "ledger_hash" : "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789",
         "taker_gets" : {
            "currency" : "EUR",
            "issuer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
         },
         "taker_pays" : {
            "currency" : "USD",
            "issuer" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
         }
      }
    ]
    })"
},
{
    "book_offers: issuer, ledger hash, and limit.", __LINE__,
    {
        "book_offers",
        "USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "EUR/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789",
        "junk",  // Note: indexing bug in parseBookOffers() requires junk param.
        "200",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "book_offers",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "issuer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
         "ledger_hash" : "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789",
         "limit" : 200,
         "proof" : true,
         "taker_gets" : {
            "currency" : "EUR",
            "issuer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
         },
         "taker_pays" : {
            "currency" : "USD",
            "issuer" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
         }
      }
    ]
    })"
},
{
    // Note: parser supports "marker", but the docs don't cover it.
    "book_offers: issuer, ledger hash, limit, and marker.", __LINE__,
    {
        "book_offers",
        "USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "EUR/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789",
        "junk",  // Note: indexing bug in parseBookOffers() requires junk param.
        "200",
        "MyMarker"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "book_offers",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "issuer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
         "ledger_hash" : "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789",
         "limit" : 200,
         "marker" : "MyMarker",
         "proof" : true,
         "taker_gets" : {
            "currency" : "EUR",
            "issuer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
         },
         "taker_pays" : {
            "currency" : "USD",
            "issuer" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
         }
      }
    ]
    })"
},
{
    "book_offers: too few arguments.", __LINE__,
    {
        "book_offers",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "book_offers",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "book_offers: too many arguments.", __LINE__,
    {
        "book_offers",
        "USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "EUR/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789",
        "junk",  // Note: indexing bug in parseBookOffers() requires junk param.
        "200",
        "MyMarker",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "book_offers",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},

{
    "book_offers: taker pays no currency.", __LINE__,
    {
        "book_offers",
        "/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "EUR/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "book_offers",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid currency/issuer '/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh'"
      }
    ]
    })"
},
{
    "book_offers: taker gets no currency.", __LINE__,
    {
        "book_offers",
        "USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "book_offers",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid currency/issuer '/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA'"
      }
    ]
    })"
},
{
    "book_offers: invalid issuer.", __LINE__,
    {
        "book_offers",
        "USD",
        "EUR",
        "not_a_valid_issuer"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "book_offers",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "issuer" : "not_a_valid_issuer",
         "taker_gets" : {
            "currency" : "EUR"
         },
         "taker_pays" : {
            "currency" : "USD"
         }
      }
    ]
    })"
},
{
    "book_offers: invalid text ledger index.", __LINE__,
    {
        "book_offers",
        "USD",
        "EUR/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "not_a_ledger"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "book_offers",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "issuer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
         "ledger_index" : 0,
         "taker_gets" : {
            "currency" : "EUR",
            "issuer" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA"
         },
         "taker_pays" : {
            "currency" : "USD"
         }
      }
    ]
    })"
},
{
    // Note: this really shouldn't throw, but does at the moment.
    "book_offers: non-numeric limit.", __LINE__,
    {
        "book_offers",
        "USD/rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "EUR/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789",
        "junk",  // Note: indexing bug in parseBookOffers() requires junk param.
        "not_a_number",
    },
    RPCCallTestData::bad_cast,
    R"()"
},

// can_delete ------------------------------------------------------------------
{
    "can_delete: minimal.", __LINE__,
    {
        "can_delete",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "can_delete",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "can_delete: ledger index.", __LINE__,
    {
        "can_delete",
        "4294967295",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "can_delete",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "can_delete" : 4294967295
      }
    ]
    })"
},
{
    "can_delete: ledger hash.", __LINE__,
    {
        "can_delete",
        "FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "can_delete",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "can_delete" : "FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210"
      }
    ]
    })"
},
{
    "can_delete: always.", __LINE__,
    {
        "can_delete",
        "always",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "can_delete",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "can_delete" : "always"
      }
    ]
    })"
},
{
    "can_delete: never.", __LINE__,
    {
        "can_delete",
        "never",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "can_delete",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "can_delete" : "never"
      }
    ]
    })"
},
{
    "can_delete: now.", __LINE__,
    {
        "can_delete",
        "now",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "can_delete",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "can_delete" : "now"
      }
    ]
    })"
},
{
    "can_delete: too many arguments.", __LINE__,
    {
        "can_delete",
        "always",
        "never"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "can_delete",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "can_delete: invalid argument.", __LINE__,
    {
        "can_delete",
        "invalid"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "can_delete",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "can_delete" : "invalid"
      }
    ]
    })"
},
{
    // Note: this should return an error but not throw.
    "can_delete: ledger index > 32 bits.", __LINE__,
    {
        "can_delete",
        "4294967296",
    },
    RPCCallTestData::bad_cast,
    R"()"
},
{
    // Note: this really shouldn't throw since it's a legitimate ledger hash.
    "can_delete: ledger hash with no alphas.", __LINE__,
    {
        "can_delete",
        "0123456701234567012345670123456701234567012345670123456701234567",
    },
    RPCCallTestData::bad_cast,
    R"()"
},

// channel_authorize -----------------------------------------------------------
{
    "channel_authorize: minimal.", __LINE__,
    {
        "channel_authorize",
        "secret_can_be_anything",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "18446744073709551615"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_authorize",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "amount" : "18446744073709551615",
         "channel_id" : "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
         "secret" : "secret_can_be_anything"
      }
    ]
    })"
},
{
    "channel_authorize: too few arguments.", __LINE__,
    {
        "channel_authorize",
        "secret_can_be_anything",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_authorize",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "channel_authorize: too many arguments.", __LINE__,
    {
        "channel_authorize",
        "secp256k1",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "2000",
        "whatever",
        "whenever"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_authorize",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "channel_authorize: bad key type.", __LINE__,
    {
        "channel_authorize",
        "secp257k1",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "2000",
        "whatever"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_authorize",
    "params" : [
      {
         "error" : "badKeyType",
         "error_code" : 1,
         "error_message" : "Bad key type."
      }
    ]
    })"
},
{
    "channel_authorize: channel_id too short.", __LINE__,
    {
        "channel_authorize",
        "secret_can_be_anything",
        "123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "2000"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_authorize",
    "params" : [
      {
         "error" : "channelMalformed",
         "error_code" : 43,
         "error_message" : "Payment channel is malformed."
      }
    ]
    })"
},
{
    "channel_authorize: channel_id too long.", __LINE__,
    {
        "channel_authorize",
        "secret_can_be_anything",
        "10123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "2000"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_authorize",
    "params" : [
      {
         "error" : "channelMalformed",
         "error_code" : 43,
         "error_message" : "Payment channel is malformed."
      }
    ]
    })"
},
{
    "channel_authorize: channel_id not hex.", __LINE__,
    {
        "channel_authorize",
        "secret_can_be_anything",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEZ",
        "2000"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_authorize",
    "params" : [
      {
         "error" : "channelMalformed",
         "error_code" : 43,
         "error_message" : "Payment channel is malformed."
      }
    ]
    })"
},
{
    "channel_authorize: negative amount.", __LINE__,
    {
        "channel_authorize",
        "secret_can_be_anything",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "-1"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_authorize",
    "params" : [
      {
         "error" : "channelAmtMalformed",
         "error_code" : 44,
         "error_message" : "Payment channel amount is malformed."
      }
    ]
    })"
},
{
    "channel_authorize: amount > 64 bits.", __LINE__,
    {
        "channel_authorize",
        "secret_can_be_anything",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "18446744073709551616"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_authorize",
    "params" : [
      {
         "error" : "channelAmtMalformed",
         "error_code" : 44,
         "error_message" : "Payment channel amount is malformed."
      }
    ]
    })"
},

// channel_verify --------------------------------------------------------------
{
    "channel_verify: public key.", __LINE__,
    {
        "channel_verify",
        "aB4BXXLuPu8DpVuyq1DBiu3SrPdtK9AYZisKhu8mvkoiUD8J9Gov",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "0",
        "DEADBEEF"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_verify",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "amount" : "0",
         "channel_id" : "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
         "public_key" : "aB4BXXLuPu8DpVuyq1DBiu3SrPdtK9AYZisKhu8mvkoiUD8J9Gov",
         "signature" : "DEADBEEF"
      }
    ]
    })"
},
{
    "channel_verify: public key hex.", __LINE__,
    {
        "channel_verify",
        "021D93E21C44160A1B3B66DA1F37B86BE39FFEA3FC4B95FAA2063F82EE823599F6",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "18446744073709551615",
        "DEADBEEF"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_verify",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "amount" : "18446744073709551615",
         "channel_id" : "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
         "public_key" : "021D93E21C44160A1B3B66DA1F37B86BE39FFEA3FC4B95FAA2063F82EE823599F6",
         "signature" : "DEADBEEF"
      }
    ]
    })"
},
{
    "channel_verify: too few arguments.", __LINE__,
    {
        "channel_verify",
        "aB4BXXLuPu8DpVuyq1DBiu3SrPdtK9AYZisKhu8mvkoiUD8J9Gov",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_verify",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "channel_verify: too many arguments.", __LINE__,
    {
        "channel_verify",
        "aB4BXXLuPu8DpVuyq1DBiu3SrPdtK9AYZisKhu8mvkoiUD8J9Gov",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "2000",
        "DEADBEEF",
        "Whatever"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_verify",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "channel_verify: malformed public key.", __LINE__,
    {
        "channel_verify",
        "aB4BXXLuPu8DpVuyq1DBiu3SrPdtK9AYZisKhu8mvkoiUD8J9GoV",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "2000",
        "DEADBEEF"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_verify",
    "params" : [
      {
         "error" : "publicMalformed",
         "error_code" : 60,
         "error_message" : "Public key is malformed."
      }
    ]
    })"
},
{
    "channel_verify: malformed hex public key.", __LINE__,
    {
        "channel_verify",
        "021D93E21C44160A1B3B66DA1F37B86BE39FFEA3FC4B95FAA2063F82EE823599F",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "2000",
        "DEADBEEF"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_verify",
    "params" : [
      {
         "error" : "publicMalformed",
         "error_code" : 60,
         "error_message" : "Public key is malformed."
      }
    ]
    })"
},
{
    "channel_verify: invalid channel id.", __LINE__,
    {
        "channel_verify",
        "aB4BXXLuPu8DpVuyq1DBiu3SrPdtK9AYZisKhu8mvkoiUD8J9Gov",
        "10123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "2000",
        "DEADBEEF"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_verify",
    "params" : [
      {
         "error" : "channelMalformed",
         "error_code" : 43,
         "error_message" : "Payment channel is malformed."
      }
    ]
    })"
},
{
    "channel_verify: short channel id.", __LINE__,
    {
        "channel_verify",
        "aB4BXXLuPu8DpVuyq1DBiu3SrPdtK9AYZisKhu8mvkoiUD8J9Gov",
        "123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "2000",
        "DEADBEEF"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_verify",
    "params" : [
      {
         "error" : "channelMalformed",
         "error_code" : 43,
         "error_message" : "Payment channel is malformed."
      }
    ]
    })"
},
{
    "channel_verify: amount too small.", __LINE__,
    {
        "channel_verify",
        "021D93E21C44160A1B3B66DA1F37B86BE39FFEA3FC4B95FAA2063F82EE823599F6",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "-1",
        "DEADBEEF"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_verify",
    "params" : [
      {
         "error" : "channelAmtMalformed",
         "error_code" : 44,
         "error_message" : "Payment channel amount is malformed."
      }
    ]
    })"
},
{
    "channel_verify: amount too large.", __LINE__,
    {
        "channel_verify",
        "021D93E21C44160A1B3B66DA1F37B86BE39FFEA3FC4B95FAA2063F82EE823599F6",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "18446744073709551616",
        "DEADBEEF"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_verify",
    "params" : [
      {
         "error" : "channelAmtMalformed",
         "error_code" : 44,
         "error_message" : "Payment channel amount is malformed."
      }
    ]
    })"
},
{
    "channel_verify: non-hex signature.", __LINE__,
    {
        "channel_verify",
        "aB4BXXLuPu8DpVuyq1DBiu3SrPdtK9AYZisKhu8mvkoiUD8J9Gov",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        "40000000",
        "ThisIsNotHexadecimal"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "channel_verify",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "amount" : "40000000",
         "channel_id" : "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
         "public_key" : "aB4BXXLuPu8DpVuyq1DBiu3SrPdtK9AYZisKhu8mvkoiUD8J9Gov",
         "signature" : "ThisIsNotHexadecimal"
      }
    ]
    })"
},

// connect ---------------------------------------------------------------------
{
    "connect: minimal.", __LINE__,
    {
        "connect",
        "ThereIsNoCheckingOnTheIPFormat",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "connect",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ip" : "ThereIsNoCheckingOnTheIPFormat"
      }
    ]
    })"
},
{
    "connect: ip and port.", __LINE__,
    {
        "connect",
        "ThereIsNoCheckingOnTheIPFormat",
        "6561"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "connect",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ip" : "ThereIsNoCheckingOnTheIPFormat",
         "port" : 6561
      }
    ]
    })"
},
{
    "connect: too few arguments.", __LINE__,
    {
        "connect",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "connect",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "connect: too many arguments.", __LINE__,
    {
        "connect",
        "ThereIsNoCheckingOnTheIPFormat",
        "6561",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "connect",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    // Note: this should return an error but not throw.
    "connect: port too small.", __LINE__,
    {
        "connect",
        "ThereIsNoCheckingOnTheIPFormat",
        "-1",
    },
    RPCCallTestData::bad_cast,
    R"()"
},
{
    // Note: this should return an error but not throw.
    "connect: port too large.", __LINE__,
    {
        "connect",
        "ThereIsNoCheckingOnTheIPFormat",
        "4294967296",
    },
    RPCCallTestData::bad_cast,
    R"()"
},

// consensus_info --------------------------------------------------------------
{
    "consensus_info: minimal.", __LINE__,
    {
        "consensus_info",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "consensus_info",
    "params" : [
      {
         "api_version" : %MAX_API_VER%
      }
    ]
    })"
},
{
    "consensus_info: too many arguments.", __LINE__,
    {
        "consensus_info",
        "whatever"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "consensus_info",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},

// deposit_authorized ----------------------------------------------------------
{
    "deposit_authorized: minimal.", __LINE__,
    {
        "deposit_authorized",
        "source_account_NotValidated",
        "destination_account_NotValidated",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "deposit_authorized",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "destination_account" : "destination_account_NotValidated",
         "source_account" : "source_account_NotValidated"
      }
    ]
    })"
},
{
    "deposit_authorized: with text ledger index.", __LINE__,
    {
        "deposit_authorized",
        "source_account_NotValidated",
        "destination_account_NotValidated",
        "validated"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "deposit_authorized",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "destination_account" : "destination_account_NotValidated",
         "ledger_index" : "validated",
         "source_account" : "source_account_NotValidated"
      }
    ]
    })"
},
{
    "deposit_authorized: with ledger index.", __LINE__,
    {
        "deposit_authorized",
        "source_account_NotValidated",
        "destination_account_NotValidated",
        "4294967295"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "deposit_authorized",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "destination_account" : "destination_account_NotValidated",
         "ledger_index" : 4294967295,
         "source_account" : "source_account_NotValidated"
      }
    ]
    })"
},
{
    "deposit_authorized: with ledger hash.", __LINE__,
    {
        "deposit_authorized",
        "source_account_NotValidated",
        "destination_account_NotValidated",
        "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "deposit_authorized",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "destination_account" : "destination_account_NotValidated",
         "ledger_hash" : "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789",
         "source_account" : "source_account_NotValidated"
      }
    ]
    })"
},
{
    "deposit_authorized: too few arguments.", __LINE__,
    {
        "deposit_authorized",
        "source_account_NotValidated",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "deposit_authorized",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "deposit_authorized: too many arguments.", __LINE__,
    {
        "deposit_authorized",
        "source_account_NotValidated",
        "destination_account_NotValidated",
        "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789",
        "spare"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "deposit_authorized",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "deposit_authorized: invalid ledger selection.", __LINE__,
    {
        "deposit_authorized",
        "source_account_NotValidated",
        "destination_account_NotValidated",
        "NotALedger",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "deposit_authorized",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "destination_account" : "destination_account_NotValidated",
         "ledger_index" : 0,
         "source_account" : "source_account_NotValidated"
      }
    ]
    })"
},

// download_shard --------------------------------------------------------------
{
    "download_shard: minimal.", __LINE__,
    {
        "download_shard",
        "20",
        "url_NotValidated",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "download_shard",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "shards" : [
            {
               "index" : 20,
               "url" : "url_NotValidated"
            }
         ]
      }
    ]
    })"
},
{
    "download_shard: novalidate.", __LINE__,
    {
        "download_shard",
        "novalidate",
        "20",
        "url_NotValidated",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "download_shard",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "shards" : [
            {
               "index" : 20,
               "url" : "url_NotValidated"
            }
         ],
         "validate" : false
      }
    ]
    })"
},
{
    "download_shard: many shards.", __LINE__,
    {
        "download_shard",
        "200000000",
        "url_NotValidated0",
        "199999999",
        "url_NotValidated1",
        "199999998",
        "url_NotValidated2",
        "199999997",
        "url_NotValidated3",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "download_shard",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "shards" : [
            {
               "index" : 200000000,
               "url" : "url_NotValidated0"
            },
            {
               "index" : 199999999,
               "url" : "url_NotValidated1"
            },
            {
               "index" : 199999998,
               "url" : "url_NotValidated2"
            },
            {
               "index" : 199999997,
               "url" : "url_NotValidated3"
            }
         ]
      }
    ]
    })"
},
{
    "download_shard: novalidate many shards.", __LINE__,
    {
        "download_shard",
        "novalidate",
        "2000000",
        "url_NotValidated0",
        "2000001",
        "url_NotValidated1",
        "2000002",
        "url_NotValidated2",
        "2000003",
        "url_NotValidated3",
        "2000004",
        "url_NotValidated4",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "download_shard",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "shards" : [
            {
               "index" : 2000000,
               "url" : "url_NotValidated0"
            },
            {
               "index" : 2000001,
               "url" : "url_NotValidated1"
            },
            {
               "index" : 2000002,
               "url" : "url_NotValidated2"
            },
            {
               "index" : 2000003,
               "url" : "url_NotValidated3"
            },
            {
               "index" : 2000004,
               "url" : "url_NotValidated4"
            }
         ],
         "validate" : false
      }
    ]
    })"
},
{
    "download_shard: too few arguments.", __LINE__,
    {
        "download_shard",
        "20"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "download_shard",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    // Note: this should return an error but not throw.
    "download_shard: novalidate too few arguments.", __LINE__,
    {
        "download_shard",
        "novalidate",
        "20"
    },
    RPCCallTestData::bad_cast,
    R"()"
},
{
    "download_shard: novalidate at end.", __LINE__,
    {
        "download_shard",
        "20",
        "url_NotValidated",
        "novalidate",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "download_shard",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "shards" : [
            {
               "index" : 20,
               "url" : "url_NotValidated"
            }
         ],
         "validate" : false
      }
    ]
    })"
},
{
    "download_shard: novalidate in middle.", __LINE__,
    {
        "download_shard",
        "20",
        "url_NotValidated20",
        "novalidate",
        "200",
        "url_NotValidated200",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "download_shard",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},
{
    // Note: this should return an error but not throw.
    "download_shard: arguments swapped.", __LINE__,
    {
        "download_shard",
        "url_NotValidated",
        "20",
    },
    RPCCallTestData::bad_cast,
    R"()"
},
{
    "download_shard: index too small.", __LINE__,
    {
        "download_shard",
        "-1",
        "url_NotValidated",
    },
    RPCCallTestData::bad_cast,
    R"()"
},
{
    "download_shard: index too big.", __LINE__,
    {
        "download_shard",
        "4294967296",
        "url_NotValidated",
    },
    RPCCallTestData::bad_cast,
    R"()"
},

// feature ---------------------------------------------------------------------
{
    "feature: minimal.", __LINE__,
    {
        "feature",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "feature",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "feature: with name.", __LINE__,
    {
        "feature",
        "featureNameOrHexIsNotValidated"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "feature",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "feature" : "featureNameOrHexIsNotValidated"
      }
    ]
    })"
},
{
    "feature: accept.", __LINE__,
    {
        "feature",
        "FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210",
        "accept"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "feature",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "feature" : "FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210",
         "vetoed" : false
      }
    ]
    })"
},
{
    "feature: reject.", __LINE__,
    {
        "feature",
        "0",
        "reject"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "feature",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "feature" : "0",
         "vetoed" : true
      }
    ]
    })"
},
{
    "feature: too many arguments.", __LINE__,
    {
        "feature",
        "featureNameOrHexIsNotValidated",
        "accept",
        "anotherArg"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "feature",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "feature: neither accept nor reject.", __LINE__,
    {
        "feature",
        "featureNameOrHexIsNotValidated",
        "veto",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "feature",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},

// fetch_info ------------------------------------------------------------------
{
    "fetch_info: minimal.", __LINE__,
    {
        "fetch_info",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "fetch_info",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "fetch_info: clear.", __LINE__,
    {
        "fetch_info",
        "clear"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "fetch_info",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "clear" : true
      }
    ]
    })"
},
{
    "fetch_info: too many arguments.", __LINE__,
    {
        "fetch_info",
        "clear",
        "other"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "fetch_info",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "fetch_info: other trailing argument.", __LINE__,
    {
        "fetch_info",
        "too"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "fetch_info",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "too" : true
      }
    ]
    })"
},

// gateway_balances ------------------------------------------------------------
{
    "gateway_balances: minimal.", __LINE__,
    {
        "gateway_balances",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "gateway_balances",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
      }
    ]
    })"
},
{
    "gateway_balances: with ledger index.", __LINE__,
    {
        "gateway_balances",
        "890765",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "gateway_balances",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : "890765"
      }
    ]
    })"
},
{
    "gateway_balances: with text ledger index.", __LINE__,
    {
        "gateway_balances",
        "current",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "gateway_balances",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : "current"
      }
    ]
    })"
},
{
    "gateway_balances: with 64 character ledger hash.", __LINE__,
    {
        "gateway_balances",
        "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "gateway_balances",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_hash" : "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV"
      }
    ]
    })"
},
{
    "gateway_balances: 1 hotwallet.", __LINE__,
    {
        "gateway_balances",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "hotwallet_is_not_validated"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "gateway_balances",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "hotwallet" : [ "hotwallet_is_not_validated" ]
      }
    ]
    })"
},
{
    "gateway_balances: 3 hotwallets.", __LINE__,
    {
        "gateway_balances",
        "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "hotwallet_is_not_validated_1",
        "hotwallet_is_not_validated_2",
        "hotwallet_is_not_validated_3",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "gateway_balances",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "hotwallet" : [
            "hotwallet_is_not_validated_1",
            "hotwallet_is_not_validated_2",
            "hotwallet_is_not_validated_3"
         ],
         "ledger_hash" : "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV"
      }
    ]
    })"
},
{
    "gateway_balances: too few arguments.", __LINE__,
    {
        "gateway_balances",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "gateway_balances",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "gateway_balances: empty first argument.", __LINE__,
    {
        "gateway_balances",
        ""
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "gateway_balances",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid first parameter"
      }
    ]
    })"
},
{
    "gateway_balances: with ledger index but no gateway.", __LINE__,
    {
        "gateway_balances",
        "890765",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "gateway_balances",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid hotwallet"
      }
    ]
    })"
},
{
    "gateway_balances: with text ledger index but no gateway.", __LINE__,
    {
        "gateway_balances",
        "current",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "gateway_balances",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid hotwallet"
      }
    ]
    })"
},
{
    "gateway_balances: with 64 character ledger hash but no gateway.", __LINE__,
    {
        "gateway_balances",
        "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "gateway_balances",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid hotwallet"
      }
    ]
    })"
},

// get_counts ------------------------------------------------------------------
{
    "get_counts: minimal.", __LINE__,
    {
        "get_counts",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "get_counts",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "get_counts: with maximum count.", __LINE__,
    {
        "get_counts",
        "100"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "get_counts",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "min_count" : 100
      }
    ]
    })"
},
{
    "get_counts: too many arguments.", __LINE__,
    {
        "get_counts",
        "100",
        "whatever"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "get_counts",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "get_counts: count too small.", __LINE__,
    {
        "get_counts",
        "-1",
    },
    RPCCallTestData::bad_cast,
    R"()"
},
{
    "get_counts: count too large.", __LINE__,
    {
        "get_counts",
        "4294967296"
    },
    RPCCallTestData::bad_cast,
    R"()"
},

// json ------------------------------------------------------------------------
{
    "json: minimal.", __LINE__,
    {
        "json",
        "command",
        R"({"json_argument":true})",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "command",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "json_argument" : true,
         "method" : "command"
      }
    ]
    })"
},
{
    "json: null object.", __LINE__,
    {
        "json",
        "command",
        R"({})",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "command",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "method" : "command"
      }
    ]
    })"
},
{
    "json: too few arguments.", __LINE__,
    {
        "json",
        "command"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "json",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "json: too many arguments.", __LINE__,
    {
        "json",
        "command",
        R"({"json_argument":true})",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "json",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "json: array, not object.", __LINE__,
    {
        "json",
        "command",
        R"(["arg1","arg2"])",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "json",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},
{
    "json: invalid json (note closing comma).", __LINE__,
    {
        "json",
        "command",
        R"({"json_argument":true,})",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "json",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},

// json2 -----------------------------------------------------------------------
{
    "json2: minimal object.", __LINE__,
    {
        "json2",
        R"({"jsonrpc":"2.0","ripplerpc":"2.0","id":"A1","method":"call_1"})",
    },
    RPCCallTestData::no_exception,
    R"({
    "id" : "A1",
    "jsonrpc" : "2.0",
    "method" : "call_1",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "id" : "A1",
         "jsonrpc" : "2.0",
         "method" : "call_1",
         "ripplerpc" : "2.0"
      }
    ],
    "ripplerpc" : "2.0"
    })"
},
{
    "json2: object with nested params.", __LINE__,
    {
        "json2",
        R"({
        "jsonrpc" : "2.0",
        "ripplerpc" : "2.0",
        "id" : "A1",
        "method" : "call_1",
        "params" : [{"inner_arg" : "yup"}]
        })",
    },
    RPCCallTestData::no_exception,
    R"({
    "id" : "A1",
    "jsonrpc" : "2.0",
    "method" : "call_1",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "0" : {
            "inner_arg" : "yup"
         },
         "id" : "A1",
         "jsonrpc" : "2.0",
         "method" : "call_1",
         "ripplerpc" : "2.0"
      }
    ],
    "ripplerpc" : "2.0"
    })"
},
{
    "json2: minimal array.", __LINE__,
    {
        "json2",
        R"([{"jsonrpc":"2.0","ripplerpc":"2.0","id":"A1","method":"call_1"}])",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "json2",
    "params" : [
      [
         {
            "api_version" : %MAX_API_VER%,
            "id" : "A1",
            "jsonrpc" : "2.0",
            "method" : "call_1",
            "ripplerpc" : "2.0"
         }
      ]
    ]
    })"
},
{
    "json2: array with object with nested params.", __LINE__,
    {
        "json2",
        R"([
        {"jsonrpc":"2.0",
        "ripplerpc":"2.0",
        "id":"A1",
        "method":"call_1",
        "params" : [{"inner_arg" : "yup"}]}
        ])",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "json2",
   "params" : [
      [
         {
            "api_version" : %MAX_API_VER%,
            "0" : {
               "inner_arg" : "yup"
            },
            "id" : "A1",
            "jsonrpc" : "2.0",
            "method" : "call_1",
            "ripplerpc" : "2.0"
         }
      ]
    ]})"
},
{
    "json2: too few arguments.", __LINE__,
    {
        "json2",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "json2",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "json2: too many arguments.", __LINE__,
    {
        "json2",
        R"({"jsonrpc":"2.0","ripplerpc":"2.0","id":"A1","method":"call_this"})",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "json2",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "json2: malformed json (note extra comma).", __LINE__,
    {
        "json2",
        R"({"jsonrpc":"2.0","ripplerpc":"2.0","id":"A1","method":"call_1",})",
    },
    RPCCallTestData::no_exception,
    R"({
    "id" : "A1",
    "jsonrpc" : "2.0",
    "method" : "json2",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters.",
         "id" : "A1",
         "jsonrpc" : "2.0",
         "ripplerpc" : "2.0"
      }
    ],
    "ripplerpc" : "2.0"
    })"
},
{
    "json2: omit jsonrpc.", __LINE__,
    {
        "json2",
        R"({"ripplerpc":"2.0","id":"A1","method":"call_1"})",
    },
    RPCCallTestData::no_exception,
    R"({
    "id" : "A1",
    "method" : "json2",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters.",
         "id" : "A1",
         "ripplerpc" : "2.0"
      }
    ],
    "ripplerpc" : "2.0"
    })"
},
{
    "json2: wrong jsonrpc version.", __LINE__,
    {
        "json2",
        R"({"jsonrpc":"2.1","ripplerpc":"2.0","id":"A1","method":"call_1"})",
    },
    RPCCallTestData::no_exception,
    R"({
    "id" : "A1",
    "jsonrpc" : "2.1",
    "method" : "json2",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters.",
         "id" : "A1",
         "jsonrpc" : "2.1",
         "ripplerpc" : "2.0"
      }
    ],
    "ripplerpc" : "2.0"
    })"
},
{
    "json2: omit ripplerpc.", __LINE__,
    {
        "json2",
        R"({"jsonrpc":"2.0","id":"A1","method":"call_1"})",
    },
    RPCCallTestData::no_exception,
    R"({
    "id" : "A1",
    "jsonrpc" : "2.0",
    "method" : "json2",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters.",
         "id" : "A1",
         "jsonrpc" : "2.0"
      }
    ]
    })"
},
{
    "json2: wrong ripplerpc version.", __LINE__,
    {
        "json2",
        R"({"jsonrpc":"2.0","ripplerpc":"2.00","id":"A1","method":"call_1"})",
    },
    RPCCallTestData::no_exception,
    R"({
    "id" : "A1",
    "jsonrpc" : "2.0",
    "method" : "json2",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters.",
         "id" : "A1",
         "jsonrpc" : "2.0",
         "ripplerpc" : "2.00"
      }
    ],
    "ripplerpc" : "2.00"
    })"
},
{
    "json2: omit id.", __LINE__,
    {
        "json2",
        R"({"jsonrpc":"2.0","ripplerpc":"2.0","method":"call_1"})",
    },
    RPCCallTestData::no_exception,
    R"({
    "jsonrpc" : "2.0",
    "method" : "json2",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters.",
         "jsonrpc" : "2.0",
         "ripplerpc" : "2.0"
      }
    ],
   "ripplerpc" : "2.0"
    })"
},
{
    "json2: omit method.", __LINE__,
    {
        "json2",
        R"({"jsonrpc":"2.0","ripplerpc":"2.0","id":"A1"})",
    },
    RPCCallTestData::no_exception,
    R"({
    "id" : "A1",
    "jsonrpc" : "2.0",
    "method" : "json2",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters.",
         "id" : "A1",
         "jsonrpc" : "2.0",
         "ripplerpc" : "2.0"
      }
    ],
   "ripplerpc" : "2.0"
    })"
},
{
    "json2: empty outer array.", __LINE__,
    {
        "json2",
        R"([])",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "json2",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},
{
    "json2: empty inner array.", __LINE__,
    {
        "json2",
        R"([{"jsonrpc":"2.0","ripplerpc":"2.0","id":"A1","method":"call_1",[]}])",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "json2",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},
{
    "json2: array with non-json2 object.", __LINE__,
    {
        "json2",
        R"([
            {"jsonrpc" : "2.1",
            "ripplerpc" : "2.0",
            "id" : "A1",
            "method" : "call_1"
            }
        ])",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "json2",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},
{
    "json2: non-object or -array inner params member.", __LINE__,
    {
        "json2",
        R"({
        "jsonrpc" : "2.0",
        "ripplerpc" : "2.0",
        "id" : "A1",
        "method" : "call_1",
        "params" : true
        })",
    },
    RPCCallTestData::no_exception,
    R"({
   "id" : "A1",
   "jsonrpc" : "2.0",
   "method" : "json2",
   "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters.",
         "id" : "A1",
         "jsonrpc" : "2.0",
         "ripplerpc" : "2.0"
      }
   ],
   "ripplerpc" : "2.0"
    })"
},

// ledger ----------------------------------------------------------------------
{
    "ledger: minimal.", __LINE__,
    {
        "ledger"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "ledger: ledger index.", __LINE__,
    {
        "ledger",
        "4294967295"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_index" : 4294967295
      }
    ]
    })"
},
{
    "ledger: text ledger index.", __LINE__,
    {
        "ledger",
        "validated"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_index" : "validated"
      }
    ]
    })"
},
{
    "ledger: ledger hash.", __LINE__,
    {
        "ledger",
        "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_hash" : "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789"
      }
    ]
    })"
},
{
    "ledger: full.", __LINE__,
    {
        "ledger",
        "current",
        "full"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "full" : true,
         "ledger_index" : "current"
      }
    ]
    })"
},
{
    "ledger: tx.", __LINE__,
    {
        "ledger",
        "closed",
        "tx"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "expand" : true,
         "ledger_index" : "closed",
         "transactions" : true
      }
    ]
    })"
},
{
    "ledger: too many arguments.", __LINE__,
    {
        "ledger",
        "4294967295",
        "spare"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_index" : 4294967295
      }
    ]
    })"
},
{
    "ledger: ledger index too small.", __LINE__,
    {
        "ledger",
        "-1"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_index" : 0
      }
    ]
    })"
},
{
    "ledger: ledger index too big.", __LINE__,
    {
        "ledger",
        "4294967296"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_index" : 0
      }
    ]
    })"
},
{
    "ledger: invalid ledger text.", __LINE__,
    {
        "ledger",
        "latest"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_index" : 0
      }
    ]
    })"
},
{
    "ledger: unsupported final argument.", __LINE__,
    {
        "ledger",
        "current",
        "expand"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_index" : "current"
      }
    ]
    })"
},

// ledger_closed ---------------------------------------------------------------
{
    "ledger_closed: minimal.", __LINE__,
    {
        "ledger_closed"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_closed",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "ledger_closed: too many arguments.", __LINE__,
    {
        "ledger_closed",
        "today"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_closed",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},

// ledger_current --------------------------------------------------------------
{
    "ledger_current: minimal.", __LINE__,
    {
        "ledger_current"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_current",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "ledger_current: too many arguments.", __LINE__,
    {
        "ledger_current",
        "today"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_current",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},

// ledger_header ---------------------------------------------------------------
{
    "ledger_header: ledger index.", __LINE__,
    {
        "ledger_header",
        "4294967295"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_header",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_index" : 4294967295
      }
    ]
    })"
},
{
    "ledger_header: ledger hash.", __LINE__,
    {
        "ledger_header",
        "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_header",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_hash" : "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789"
      }
    ]
    })"
},
{
    "ledger_header: too few arguments.", __LINE__,
    {
        "ledger_header",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_header",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "ledger_header: too many arguments.", __LINE__,
    {
        "ledger_header",
        "4294967295",
        "spare"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_header",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "ledger_header: text ledger index.", __LINE__,
    {
        "ledger_header",
        "current"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_header",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_index" : 0
      }
    ]
    })"
},
{
    "ledger_header: ledger index too small.", __LINE__,
    {
        "ledger_header",
        "-1"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_header",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_index" : 0
      }
    ]
    })"
},
{
    "ledger_header: ledger index too big.", __LINE__,
    {
        "ledger_header",
        "4294967296"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_header",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_index" : 0
      }
    ]
    })"
},

// ledger_request --------------------------------------------------------------
{
    "ledger_request: ledger index.", __LINE__,
    {
        "ledger_request",
        "4294967295"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_request",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_index" : 4294967295
      }
    ]
    })"
},
{
    "ledger_request: ledger hash.", __LINE__,
    {
        "ledger_request",
        "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_request",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_hash" : "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789"
      }
    ]
    })"
},
{
    "ledger_request: too few arguments.", __LINE__,
    {
        "ledger_request",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_request",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "ledger_request: too many arguments.", __LINE__,
    {
        "ledger_request",
        "4294967295",
        "spare"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_request",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "ledger_request: text ledger index.", __LINE__,
    {
        "ledger_request",
        "current"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_request",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_index" : 0
      }
    ]
    })"
},
{
    "ledger_request: ledger index too small.", __LINE__,
    {
        "ledger_request",
        "-1"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_request",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_index" : 0
      }
    ]
    })"
},
{
    "ledger_request: ledger index too big.", __LINE__,
    {
        "ledger_request",
        "4294967296"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ledger_request",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_index" : 0
      }
    ]
    })"
},

// log_level -------------------------------------------------------------------
{
    "log_level: minimal.", __LINE__,
    {
        "log_level",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "log_level",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "log_level: fatal.", __LINE__,
    {
        "log_level",
        "fatal"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "log_level",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "severity" : "fatal"
      }
    ]
    })"
},
{
    "log_level: error.", __LINE__,
    {
        "log_level",
        "error"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "log_level",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "severity" : "error"
      }
    ]
    })"
},
{
    "log_level: warn.", __LINE__,
    {
        "log_level",
        "warn"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "log_level",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "severity" : "warn"
      }
    ]
    })"
},
{
    "log_level: debug.", __LINE__,
    {
        "log_level",
        "debug"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "log_level",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "severity" : "debug"
      }
    ]
    })"
},
{
    "log_level: trace.", __LINE__,
    {
        "log_level",
        "trace"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "log_level",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "severity" : "trace"
      }
    ]
    })"
},
{
    "log_level: base partition.", __LINE__,
    {
        "log_level",
        "base",
        "trace"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "log_level",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "partition" : "base",
         "severity" : "trace"
      }
    ]
    })"
},
{
    "log_level: partiton_name.", __LINE__,
    {
        "log_level",
        "partition_name",
        "fatal"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "log_level",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "partition" : "partition_name",
         "severity" : "fatal"
      }
    ]
    })"
},
{
    "log_level: too many arguments.", __LINE__,
    {
        "log_level",
        "partition_name",
        "fatal",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "log_level",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "log_level: invalid severity.", __LINE__,
    {
        "log_level",
        "err"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "log_level",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "severity" : "err"
      }
    ]
    })"
},
{
    "log_level: swap partition name and severity.", __LINE__,
    {
        "log_level",
        "fatal",
        "partition_name",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "log_level",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "partition" : "fatal",
         "severity" : "partition_name"
      }
    ]
    })"
},

// logrotate -------------------------------------------------------------------
{
    "logrotate: minimal.", __LINE__,
    {
        "logrotate",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "logrotate",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "logrotate: too many arguments.", __LINE__,
    {
        "logrotate",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "logrotate",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},

// owner_info ------------------------------------------------------------------
{
    "owner_info: minimal.", __LINE__,
    {
        "owner_info",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "owner_info",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
      }
    ]
    })"
},
{
    "owner_info: with numeric ledger index.", __LINE__,
    {
        "owner_info",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "987654321"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "owner_info",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 987654321
      }
    ]
    })"
},
{
    "owner_info: with text ledger index.", __LINE__,
    {
        "owner_info",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "validated"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "owner_info",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : "validated"
      }
    ]
    })"
},
{
    "owner_info: with ledger hash.", __LINE__,
    {
        "owner_info",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "owner_info",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_hash" : "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
      }
    ]
    })"
},
{
    // Note: this works, but it doesn't match the documentation.
    "owner_info: strict.", __LINE__,
    {
        "owner_info",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "strict"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "owner_info",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "strict" : 1
      }
    ]
    })"
},
{
    "owner_info: with ledger index and strict.", __LINE__,
    {
        "owner_info",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "validated",
        "strict"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "owner_info",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "owner_info: too few arguments.", __LINE__,
    {
        "owner_info",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "owner_info",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "owner_info: too many arguments.", __LINE__,
    {
        "owner_info",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "current",
        "extra",
        "strict",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "owner_info",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "owner_info: invalid accountID.", __LINE__,
    {
        "owner_info",
        "",  // Note: very few values are detected as bad!
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "owner_info",
    "params" : [
       {
          "error" : "actMalformed",
          "error_code" : 35,
          "error_message" : "Account malformed."
       }
    ]
    })",
},
{
    // Note: there is code in place to return rpcLGR_IDX_MALFORMED.  That
    // cannot currently occur because jvParseLedger() always returns true.
    "owner_info: invalid ledger selection 1.", __LINE__,
    {
        "owner_info",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "no_ledger"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "owner_info",
    "params" : [
       {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 0
       }
    ]
    })",
},
{
    // Note: there is code in place to return rpcLGR_IDX_MALFORMED.  That
    // cannot currently occur because jvParseLedger() always returns true.
    "owner_info: invalid ledger selection 2.", __LINE__,
    {
        "owner_info",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "no_ledger",
        "strict"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "owner_info",
    "params" : [
       {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
       }
    ]
    })",
},

// peers -----------------------------------------------------------------------
{
    "peers: minimal.", __LINE__,
    {
        "peers",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "peers",
    "params" : [
       {
         "api_version" : %MAX_API_VER%,
       }
    ]
    })"
},
{
    "peers: too many arguments.", __LINE__,
    {
        "peers",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "peers",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},

// ping ------------------------------------------------------------------------
{
    "ping: minimal.", __LINE__,
    {
        "ping",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ping",
    "params" : [
       {
         "api_version" : %MAX_API_VER%,
       }
    ]
    })"
},
{
    "ping: too many arguments.", __LINE__,
    {
        "ping",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ping",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},

// print -----------------------------------------------------------------------
{
    "print: minimal.", __LINE__,
    {
        "print",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "print",
    "params" : [
       {
         "api_version" : %MAX_API_VER%,
       }
    ]
    })"
},
{
    // The docs indicate that no arguments are allowed.  So should this error?
    "print: extra argument.", __LINE__,
    {
        "print",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "print",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "params" : [ "extra" ]
      }
    ]
    })"
},
{
    "print: too many arguments.", __LINE__,
    {
        "print",
        "extra1",
        "extra2"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "print",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},

// random ----------------------------------------------------------------------
{
    "random: minimal.", __LINE__,
    {
        "random",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "random",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "random: too many arguments.", __LINE__,
    {
        "random",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "random",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},

// ripple_path_find ------------------------------------------------------------
{
    "ripple_path_find: minimal.", __LINE__,
    {
        "ripple_path_find",
        R"({"json_argument":true})",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ripple_path_find",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "json_argument" : true
      }
    ]
    })"
},
{
    "ripple_path_find: ledger index.", __LINE__,
    {
        "ripple_path_find",
        R"({"json_argument":true})",
        "4294967295"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ripple_path_find",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "json_argument" : true,
         "ledger_index" : 4294967295
      }
    ]
    })"
},
{
    "ripple_path_find: text ledger index.", __LINE__,
    {
        "ripple_path_find",
        R"({"json_argument":true})",
        "closed"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ripple_path_find",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "json_argument" : true,
         "ledger_index" : "closed"
      }
    ]
    })"
},
{
    "ripple_path_find: ledger hash.", __LINE__,
    {
        "ripple_path_find",
        R"({"json_argument":true})",
        "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ripple_path_find",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "json_argument" : true,
         "ledger_hash" : "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV"
      }
    ]
    })"
},

{
    "ripple_path_find: too few arguments.", __LINE__,
    {
        "ripple_path_find",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ripple_path_find",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "ripple_path_find: too many arguments.", __LINE__,
    {
        "ripple_path_find",
        R"({"json_argument":true})",
        "current",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ripple_path_find",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "ripple_path_find: invalid json (note extra comma).", __LINE__,
    {
        "ripple_path_find",
        R"({"json_argument":true,})",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ripple_path_find",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},
{
    "ripple_path_find: ledger index too small.", __LINE__,
    {
        "ripple_path_find",
        R"({"json_argument":true})",
        "-1"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ripple_path_find",
    "params" : [
      {
        "api_version" : %MAX_API_VER%,
        "json_argument" : true,
         "ledger_index" : 0
      }
    ]
    })"
},
{
    "ripple_path_find: ledger index too big.", __LINE__,
    {
        "ripple_path_find",
        R"({"json_argument":true})",
        "4294967296"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ripple_path_find",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "json_argument" : true,
         "ledger_index" : 0
      }
    ]
    })"
},
{
    "ripple_path_find: invalid text ledger index.", __LINE__,
    {
        "ripple_path_find",
        R"({"json_argument":true})",
        "cur"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "ripple_path_find",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "json_argument" : true,
         "ledger_index" : 0
      }
    ]
    })"
},

// sign ------------------------------------------------------------------------
{
    "sign: minimal.", __LINE__,
    {
        "sign",
        "my_secret",
        R"({"json_argument":true})",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "sign",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "secret" : "my_secret",
         "tx_json" : {
            "json_argument" : true
         }
      }
    ]
    })"
},
{
    "sign: offline.", __LINE__,
    {
        "sign",
        "my_secret",
        R"({"json_argument":true})",
        "offline"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "sign",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "offline" : true,
         "secret" : "my_secret",
         "tx_json" : {
            "json_argument" : true
         }
      }
    ]
    })"
},
{
    "sign: too few arguments.", __LINE__,
    {
        "sign",
        "contents_of_blob"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "sign",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "sign: too many arguments.", __LINE__,
    {
        "sign",
        "my_secret",
        R"({"json_argument":true})",
        "offline",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "sign",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "sign: invalid JSON (note extra comma).", __LINE__,
    {
        "sign",
        "my_secret",
        R"({"json_argument":true,})",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "sign",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},
{
    "sign: invalid final argument.", __LINE__,
    {
        "sign",
        "my_secret",
        R"({"json_argument":true})",
        "offlin"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "sign",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},

// sign_for --------------------------------------------------------------------
{
    "sign_for: minimal.", __LINE__,
    {
        "sign_for",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "my_secret",
        R"({"json_argument":true})",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "sign_for",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "secret" : "my_secret",
         "tx_json" : {
            "json_argument" : true
         }
      }
    ]
    })"
},
{
    "sign_for: offline.", __LINE__,
    {
        "sign_for",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "my_secret",
        R"({"json_argument":true})",
        "offline"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "sign_for",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "offline" : true,
         "secret" : "my_secret",
         "tx_json" : {
            "json_argument" : true
         }
      }
    ]
    })"
},
{
    "sign_for: too few arguments.", __LINE__,
    {
        "sign_for",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "my_secret",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "sign_for",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "sign_for: too many arguments.", __LINE__,
    {
        "sign_for",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "my_secret",
        R"({"json_argument":true})",
        "offline",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "sign_for",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "sign_for: invalid json (note extra comma).", __LINE__,
    {
        "sign_for",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "my_secret",
        R"({"json_argument":true,})",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "sign_for",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},
{
    "sign_for: invalid final argument.", __LINE__,
    {
        "sign_for",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "my_secret",
        R"({"json_argument":true})",
        "ofline"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "sign_for",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},

// submit ----------------------------------------------------------------------
{
    "submit: blob.", __LINE__,
    {
        "submit",
        "the blob is unvalidated and may be any length..."
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "submit",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "tx_blob" : "the blob is unvalidated and may be any length..."
      }
    ]
    })"
},
{
    "submit: json.", __LINE__,
    {
        "submit",
        "my_secret",
        R"({"json_argument":true})",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "submit",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "secret" : "my_secret",
         "tx_json" : {
            "json_argument" : true
         }
      }
    ]
    })"
},
{
    "submit: too few arguments.", __LINE__,
    {
        "submit",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "submit",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    // Note: I believe this _ought_ to be detected as too many arguments.
    "submit: four arguments.", __LINE__,
    {
        "submit",
        "my_secret",
        R"({"json_argument":true})",
        "offline"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "submit",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "offline" : true,
         "secret" : "my_secret",
         "tx_json" : {
            "json_argument" : true
         }
      }
    ]
    })"
},
{
    "submit: too many arguments.", __LINE__,
    {
        "submit",
        "my_secret",
        R"({"json_argument":true})",
        "offline",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "submit",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "submit: invalid json (note extra comma).", __LINE__,
    {
        "submit",
        "my_secret",
        R"({"json_argument":true,})",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "submit",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},
{
    "submit: last argument not \"offline\".", __LINE__,
    {
        "submit",
        "my_secret",
        R"({"json_argument":true})",
        "offlne"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "submit",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},

// submit_multisigned ----------------------------------------------------------
{
    "submit_multisigned: json.", __LINE__,
    {
        "submit_multisigned",
        R"({"json_argument":true})",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "submit_multisigned",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "tx_json" : {
            "json_argument" : true
         }
      }
    ]
    })"
},
{
    "submit_multisigned: too few arguments.", __LINE__,
    {
        "submit_multisigned",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "submit_multisigned",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "submit_multisigned: too many arguments.", __LINE__,
    {
        "submit_multisigned",
        R"({"json_argument":true})",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "submit_multisigned",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "submit_multisigned: invalid json (note extra comma).", __LINE__,
    {
        "submit_multisigned",
        R"({"json_argument":true,})",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "submit_multisigned",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
         }
      }
    ]
    })"
},

// server_info -----------------------------------------------------------------
{
    "server_info: minimal.", __LINE__,
    {
        "server_info",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "server_info",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "server_info: counters.", __LINE__,
    {
        "server_info",
        "counters"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "server_info",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "counters" : true
      }
    ]
    })"
},
{
    "server_info: too many arguments.", __LINE__,
    {
        "server_info",
        "counters",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "server_info",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "server_info: non-counters argument.", __LINE__,
    {
        "server_info",
        "counter"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "server_info",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},

// server_state ----------------------------------------------------------------
{
    "server_state: minimal.", __LINE__,
    {
        "server_state",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "server_state",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "server_state: counters.", __LINE__,
    {
        "server_state",
        "counters"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "server_state",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "counters" : true
      }
    ]
    })"
},
{
    "server_state: too many arguments.", __LINE__,
    {
        "server_state",
        "counters",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "server_state",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "server_state: non-counters argument.", __LINE__,
    {
        "server_state",
        "counter"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "server_state",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},

// stop ------------------------------------------------------------------------
{
    "stop: minimal.", __LINE__,
    {
        "stop",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "stop",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "stop: too many arguments.", __LINE__,
    {
        "stop",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "stop",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},

// transaction_entry -----------------------------------------------------------
{
    "transaction_entry: ledger index.", __LINE__,
    {
        "transaction_entry",
        "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV",
        "4294967295"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "transaction_entry",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_index" : 4294967295,
         "tx_hash" : "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV"
      }
    ]
    })"
},
{
    "transaction_entry: text ledger index.", __LINE__,
    {
        "transaction_entry",
        "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV",
        "current"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "transaction_entry",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_index" : "current",
         "tx_hash" : "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV"
      }
    ]
    })"
},
{
    "transaction_entry: ledger hash.", __LINE__,
    {
        "transaction_entry",
        "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV",
        "VUTSRQPONMLKJIHGFEDCBA9876543210VUTSRQPONMLKJIHGFEDCBA9876543210"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "transaction_entry",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "ledger_hash" : "VUTSRQPONMLKJIHGFEDCBA9876543210VUTSRQPONMLKJIHGFEDCBA9876543210",
         "tx_hash" : "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV"
      }
    ]
    })"
},
{
    "transaction_entry: too few arguments.", __LINE__,
    {
        "transaction_entry",
        "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "transaction_entry",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "transaction_entry: too many arguments.", __LINE__,
    {
        "transaction_entry",
        "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV",
        "validated",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "transaction_entry",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "transaction_entry: short tx_hash.", __LINE__,
    {
        "transaction_entry",
        "123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV",
        "validated",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "transaction_entry",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},
{
    "transaction_entry: long tx_hash.", __LINE__,
    {
        "transaction_entry",
        "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUVW",
        "validated",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "transaction_entry",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},
{
    "transaction_entry: small ledger index.", __LINE__,
    {
        "transaction_entry",
        "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV",
        "0",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "transaction_entry",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},
{
    "transaction_entry: large ledger index.", __LINE__,
    {
        "transaction_entry",
        "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV",
        "4294967296",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "transaction_entry",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},
{
    "transaction_entry: short ledger hash.", __LINE__,
    {
        "transaction_entry",
        "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV",
        "VUTSRQPONMLKJIHGFEDCBA9876543210VUTSRQPONMLKJIHGFEDCBA987654321",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "transaction_entry",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},
{
    "transaction_entry: long ledger hash.", __LINE__,
    {
        "transaction_entry",
        "0123456789ABCDEFGHIJKLMNOPQRSTUV0123456789ABCDEFGHIJKLMNOPQRSTUV",
        "VUTSRQPONMLKJIHGFEDCBA9876543210VUTSRQPONMLKJIHGFEDCBA9876543210Z",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "transaction_entry",
    "params" : [
      {
         "error" : "invalidParams",
         "error_code" : 31,
         "error_message" : "Invalid parameters."
      }
    ]
    })"
},

// tx --------------------------------------------------------------------------
{
    "tx: minimal.", __LINE__,
    {
        "tx",
        "transaction_hash_is_not_validated"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "transaction" : "transaction_hash_is_not_validated"
      }
    ]
    })"
},
{
    "tx: binary.", __LINE__,
    {
        "tx",
        "transaction_hash_is_not_validated",
        "binary"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "binary" : true,
         "transaction" : "transaction_hash_is_not_validated"
      }
    ]
    })"
},
{
    "tx: too few arguments.", __LINE__,
    {
        "tx",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "tx: too many arguments.", __LINE__,
    {
        "tx",
        "transaction_hash_is_not_validated",
        "binary",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "tx: invalid final argument is apparently ignored.", __LINE__,
    {
        "tx",
        "transaction_hash_is_not_validated",
        "bin"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "transaction" : "transaction_hash_is_not_validated"
      }
    ]
    })"
},

// tx_account ------------------------------------------------------------------
{
    "tx_account: minimal.", __LINE__,
    {
        "tx_account",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx_account",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
      }
    ]
    })"
},
{
    "tx_account: ledger_index .", __LINE__,
    {
        "tx_account",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "4294967295"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx_account",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 4294967295
      }
    ]
    })"
},
{
    "tx_account: ledger_index plus trailing params.", __LINE__,
    {
        "tx_account",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "707",
        "forward",
        "binary",
        "count"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx_account",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "binary" : true,
         "count" : true,
         "forward" : true,
         "ledger_index" : 707
      }
    ]
    })"
},
{
    "tx_account: ledger_index_min and _max.", __LINE__,
    {
        "tx_account",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "2147483647",
        "2147483647"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx_account",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index_max" : 2147483647,
         "ledger_index_min" : 2147483647
      }
    ]
    })"
},
{
    "tx_account: ledger_index_min and _max plus trailing params.", __LINE__,
    {
        "tx_account",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "33333",
        "2147483647",
        "binary",
        "count",
        "forward"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx_account",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "binary" : true,
         "count" : true,
         "forward" : true,
         "ledger_index_max" : 2147483647,
         "ledger_index_min" : 33333
      }
    ]
    })"
},
{
    "tx_account: ledger_index_min and _max plus limit.", __LINE__,
    {
        "tx_account",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "-1",
        "2147483647",
        "2147483647"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx_account",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index_max" : 2147483647,
         "ledger_index_min" : -1,
         "limit" : 2147483647
      }
    ]
    })"
},
{
    "tx_account: ledger_index_min and _max, limit, trailing args.", __LINE__,
    {
        "tx_account",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "1",
        "1",
        "-1",
        "count",
        "forward",
        "binary"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx_account",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "binary" : true,
         "count" : true,
         "forward" : true,
         "ledger_index_max" : 1,
         "ledger_index_min" : 1,
         "limit" : -1
      }
    ]
    })"
},
{
    "tx_account: too few arguments.", __LINE__,
    {
        "tx_account",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx_account",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "tx_account: too many arguments.", __LINE__,
    {
        "tx_account",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "589",
        "590",
        "67",
        "extra",
        "descending",
        "count",
        "binary"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx_account",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "tx_account: invalid accountID.", __LINE__,
    {
        "tx_account",
        "rHb9CJAWyB4rj9!VRWn96DkukG4bwdtyTh"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx_account",
    "params" : [
       {
          "error" : "actMalformed",
          "error_code" : 35,
          "error_message" : "Account malformed."
       }
    ]
    })",
},
{
    // Note: not currently detected as bad input.
    "tx_account: invalid ledger.", __LINE__,
    {
        "tx_account",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "-478.7"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx_account",
    "params" : [
       {
         "api_version" : %MAX_API_VER%,
         "account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
         "ledger_index" : 0
       }
    ]
    })",
},
{
    "tx_account: max less than min.", __LINE__,
    {
        "tx_account",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "580",
        "579"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx_account",
    "params" : [
       {
         "error" : "lgrIdxsInvalid",
         "error_code" : 55,
         "error_message" : "Ledger indexes invalid."
       }
    ]
    })",
},
{
    // Note: this really shouldn't throw, but does at the moment.
    "tx_account: min large but still valid.", __LINE__,
    {
        "tx_account",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "2147483648",
        "2147483648"
    },
    RPCCallTestData::bad_cast,
    R"()",
},
{
    // Note: this really shouldn't throw, but does at the moment.
    "tx_account: max large but still valid.", __LINE__,
    {
        "tx_account",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "2147483647",
        "2147483648"
    },
    RPCCallTestData::bad_cast,
    R"()",
},
{
    // Note: this really shouldn't throw, but does at the moment.
    "tx_account: large limit.", __LINE__,
    {
        "tx_account",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "-1",
        "-1",
        "2147483648"
    },
    RPCCallTestData::bad_cast,
    R"()",
},
{
    // Note: this really shouldn't throw, but does at the moment.
    "tx_account: non-integer min.", __LINE__,
    {
        "tx_account",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Binary",
        "-1"
    },
    RPCCallTestData::bad_cast,
    R"()",
},
{
    // Note: this really shouldn't throw, but does at the moment.
    "tx_account: non-integer max.", __LINE__,
    {
        "tx_account",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "-1",
        "counts"
    },
    RPCCallTestData::bad_cast,
    R"()",
},
{
    // Note: this really shouldn't throw, but does at the moment.
    "tx_account: non-integer limit.", __LINE__,
    {
        "tx_account",
        "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "-1",
        "-1",
        "decending"
    },
    RPCCallTestData::bad_cast,
    R"()",
},

// tx_history ------------------------------------------------------------------
{
    "tx_history: minimal.", __LINE__,
    {
        "tx_history",
        "0"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx_history",
    "params" : [
      {
        "api_version" : %MAX_API_VER%,
        "start" : 0
      }
    ]
    })"
},
{
    "tx_history: too few arguments.", __LINE__,
    {
        "tx_history",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx_history",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    "tx_history: too many arguments.", __LINE__,
    {
        "tx_history",
        "0",
        "1"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "tx_history",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},
{
    // Note: this really shouldn't throw, but does at the moment.
    "tx_history: start too small.", __LINE__,
    {
        "tx_history",
        "-1"
    },
    RPCCallTestData::bad_cast,
    R"()",
},
{
    // Note: this really shouldn't throw, but does at the moment.
    "tx_history: start too big.", __LINE__,
    {
        "tx_history",
        "4294967296"
    },
    RPCCallTestData::bad_cast,
    R"()",
},
{
    // Note: this really shouldn't throw, but does at the moment.
    "tx_history: start not integer.", __LINE__,
    {
        "tx_history",
        "beginning"
    },
    RPCCallTestData::bad_cast,
    R"()",
},

// unl_list --------------------------------------------------------------------
{
    "unl_list: minimal.", __LINE__,
    {
        "unl_list",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "unl_list",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "unl_list: too many arguments.", __LINE__,
    {
        "unl_list",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "unl_list",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},

// validation_create -----------------------------------------------------------
{
    "validation_create: minimal.", __LINE__,
    {
        "validation_create",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "validation_create",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "validation_create: with secret.", __LINE__,
    {
        "validation_create",
        "the form of the secret is not validated"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "validation_create",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "secret" : "the form of the secret is not validated"
      }
    ]
    })"
},
{
    "validation_create: too many arguments.", __LINE__,
    {
        "validation_create",
        "the form of the secret is not validated",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "validation_create",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},

// version ---------------------------------------------------------------------
{
    "version: minimal.", __LINE__,
    {
        "version",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "version",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "version: too many arguments.", __LINE__,
    {
        "version",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "version",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},

// wallet_propose --------------------------------------------------------------
{
    "wallet_propose: minimal.", __LINE__,
    {
        "wallet_propose",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "wallet_propose",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "wallet_propose: with passphrase.", __LINE__,
    {
        "wallet_propose",
        "the form of the passphrase is not validated"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "wallet_propose",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "passphrase" : "the form of the passphrase is not validated"
      }
    ]
    })"
},
{
    "wallet_propose: too many arguments.", __LINE__,
    {
        "wallet_propose",
        "the form of the passphrase is not validated",
        "extra"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "wallet_propose",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},

// internal --------------------------------------------------------------------
{
    "internal: minimal.", __LINE__,
    {
        "internal",
        "command_name"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "internal",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "internal_command" : "command_name",
         "params" : []
      }
    ]
    })"
},
{
    "internal: with parameters.", __LINE__,
    {
        "internal",
        "command_name",
        "string_arg",
        "1",
        "-1",
        "4294967296",
        "3.14159"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "internal",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "internal_command" : "command_name",
         "params" : [ "string_arg", "1", "-1", "4294967296", "3.14159" ]
      }
    ]
    })"
},
{
    "internal: too few arguments.", __LINE__,
    {
        "internal",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "internal",
    "params" : [
      {
         "error" : "badSyntax",
         "error_code" : 1,
         "error_message" : "Syntax error."
      }
    ]
    })"
},

// path_find -------------------------------------------------------------------
{
    "path_find: minimal.", __LINE__,
    {
        "path_find",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "path_find",
    "params" : [
      {
         "error" : "noEvents",
         "error_code" : 7,
         "error_message" : "Current transport does not support events."
      }
    ]
    })"
},
{
    "path_find: with arguments.", __LINE__,
    {
        "path_find",
        "string_arg",
        "1",
        "-1",
        "4294967296",
        "3.14159"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "path_find",
    "params" : [
      {
         "error" : "noEvents",
         "error_code" : 7,
         "error_message" : "Current transport does not support events."
      }
    ]
    })"
},

// subscribe -------------------------------------------------------------------
{
    "subscribe: minimal.", __LINE__,
    {
        "subscribe",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "subscribe",
    "params" : [
      {
         "error" : "noEvents",
         "error_code" : 7,
         "error_message" : "Current transport does not support events."
      }
    ]
    })"
},
{
    "subscribe: with arguments.", __LINE__,
    {
        "subscribe",
        "string_arg",
        "1",
        "-1",
        "4294967296",
        "3.14159"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "subscribe",
    "params" : [
      {
         "error" : "noEvents",
         "error_code" : 7,
         "error_message" : "Current transport does not support events."
      }
    ]
    })"
},

// unsubscribe -----------------------------------------------------------------
{
    "unsubscribe: minimal.", __LINE__,
    {
        "unsubscribe",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "unsubscribe",
    "params" : [
      {
         "error" : "noEvents",
         "error_code" : 7,
         "error_message" : "Current transport does not support events."
      }
    ]
    })"
},
{
    "unsubscribe: with arguments.", __LINE__,
    {
        "unsubscribe",
        "string_arg",
        "1",
        "-1",
        "4294967296",
        "3.14159"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "unsubscribe",
    "params" : [
      {
         "error" : "noEvents",
         "error_code" : 7,
         "error_message" : "Current transport does not support events."
      }
    ]
    })"
},

// unknown_command -------------------------------------------------------------
{
    "unknown_command: minimal.", __LINE__,
    {
        "unknown_command",
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "unknown_command",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
      }
    ]
    })"
},
{
    "unknown_command: with arguments.", __LINE__,
    {
        "unknown_command",
        "string_arg",
        "1",
        "-1",
        "4294967296",
        "3.14159"
    },
    RPCCallTestData::no_exception,
    R"({
    "method" : "unknown_command",
    "params" : [
      {
         "api_version" : %MAX_API_VER%,
         "params" : [ "string_arg", "1", "-1", "4294967296", "3.14159" ]
      }
    ]
    })"
},
};

std::string updateAPIVersionString(const char * const req)
{
    static std::string version_str = std::to_string(RPC::ApiMaximumSupportedVersion);
    static auto place_holder = "%MAX_API_VER%";
    std::string jr(req);
    boost::replace_all(jr, place_holder, version_str);
    return jr;
}

class RPCCall_test : public beast::unit_test::suite
{
public:
    void testRPCCall()
    {
        testcase << "RPCCall";

        test::jtx::Env env(*this);  // Used only for its Journal.

        // For each RPCCall test.
        for (RPCCallTestData const& rpcCallTest : rpcCallTestArray)
        {
            std::vector<std::string> const args {
                rpcCallTest.args.begin(), rpcCallTest.args.end()};

            // Note that, over the long term, none of these tests should
            // throw.  But, for the moment, some of them do.  So handle it.
            Json::Value got;
            try
            {
                got = cmdLineToJSONRPC (args, env.journal);
            }
            catch (std::bad_cast const&)
            {
                if ((rpcCallTest.throwsWhat == RPCCallTestData::bad_cast) &&
                    (std::strlen (rpcCallTest.exp) == 0))
                {
                    pass();
                }
                else
                {
                    fail (rpcCallTest.description, __FILE__, rpcCallTest.line);
                }
                // Try the next test.
                continue;
            }

            Json::Value exp;
            Json::Reader{}.parse (updateAPIVersionString(rpcCallTest.exp), exp);

            // Lambda to remove the "params[0u]:error_code" field if present.
            // Error codes are not expected to be stable between releases.
            auto rmErrorCode = [] (Json::Value& json)
            {
                if (json.isMember (jss::params) &&
                    json[jss::params].isArray() &&
                    json[jss::params].size() > 0 &&
                    json[jss::params][0u].isObject())
                {
                    json[jss::params][0u].removeMember (jss::error_code);
                }
            };
            rmErrorCode (got);
            rmErrorCode (exp);

            // Pass if we didn't expect a throw and we got what we expected.
            if ((rpcCallTest.throwsWhat == RPCCallTestData::no_exception) &&
                (got == exp))
            {
                pass();
            }
            else
            {
                fail (rpcCallTest.description, __FILE__, rpcCallTest.line);
            }
        }
    }

    void run() override
    {
        testRPCCall();
    }
};

BEAST_DEFINE_TESTSUITE(RPCCall,app,ripple);

} // test
} // ripple
