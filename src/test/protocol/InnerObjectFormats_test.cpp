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

#include <BeastConfig.h>
#include <ripple/basics/contract.h>
#include <ripple/protocol/InnerObjectFormats.h>
#include <ripple/protocol/ErrorCodes.h>          // RPC::containsError
#include <ripple/json/json_reader.h>             // Json::Reader
#include <ripple/protocol/STParsedJSON.h>        // STParsedJSONObject
#include <ripple/beast/unit_test.h>
#include <test/jtx.h>

namespace ripple {

namespace InnerObjectFormatsUnitTestDetail
{

struct TestJSONTxt
{
    std::string const txt;
    bool const expectFail;
};

static TestJSONTxt const testArray[] =
{

// Valid SignerEntry
{R"({
    "Account" : "rDg53Haik2475DJx8bjMDSDPj4VX7htaMd",
    "SignerEntries" :
    [
        {
            "SignerEntry" :
            {
                "Account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                "SignerWeight" : 4
            }
        },
        {
            "SignerEntry" :
            {
                "Account" : "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                "SignerWeight" : 3
            }
        }
    ],
    "SignerQuorum" : 7,
    "TransactionType" : "SignerListSet"
})", false
},

// SignerEntry missing Account
{R"({
    "Account" : "rDg53Haik2475DJx8bjMDSDPj4VX7htaMd",
    "SignerEntries" :
    [
        {
            "SignerEntry" :
            {
                "Account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                "SignerWeight" : 4
            }
        },
        {
            "SignerEntry" :
            {
                "SignerWeight" : 3
            }
        }
    ],
    "SignerQuorum" : 7,
    "TransactionType" : "SignerListSet"
})", true
},

// SignerEntry missing SignerWeight
{R"({
    "Account" : "rDg53Haik2475DJx8bjMDSDPj4VX7htaMd",
    "SignerEntries" :
    [
        {
            "SignerEntry" :
            {
                "Account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                "SignerWeight" : 4
            }
        },
        {
            "SignerEntry" :
            {
                "Account" : "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
            }
        }
    ],
    "SignerQuorum" : 7,
    "TransactionType" : "SignerListSet"
})", true
},

// SignerEntry with unexpected Amount
{R"({
    "Account" : "rDg53Haik2475DJx8bjMDSDPj4VX7htaMd",
    "SignerEntries" :
    [
        {
            "SignerEntry" :
            {
                "Account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                "SignerWeight" : 4
            }
        },
        {
            "SignerEntry" :
            {
                "Amount" : "1000000",
                "Account" : "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                "SignerWeight" : 3
            }
        }
    ],
    "SignerQuorum" : 7,
    "TransactionType" : "SignerListSet"
})", true
},

// SignerEntry with no Account and unexpected Amount
{R"({
    "Account" : "rDg53Haik2475DJx8bjMDSDPj4VX7htaMd",
    "SignerEntries" :
    [
        {
            "SignerEntry" :
            {
                "Account" : "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                "SignerWeight" : 4
            }
        },
        {
            "SignerEntry" :
            {
                "Amount" : "10000000",
                "SignerWeight" : 3
            }
        }
    ],
    "SignerQuorum" : 7,
    "TransactionType" : "SignerListSet"
})", true
},

};

} // namespace InnerObjectFormatsUnitTestDetail


class InnerObjectFormatsParsedJSON_test : public beast::unit_test::suite
{
public:
    void run()
    {
        using namespace InnerObjectFormatsUnitTestDetail;

        // Instantiate a jtx::Env so debugLog writes are exercised.
        test::jtx::Env env (*this);

        for (auto const& test : testArray)
        {
            Json::Value req;
            Json::Reader ().parse (test.txt, req);
            if (RPC::contains_error (req))
            {
                Throw<std::runtime_error> (
                    "Internal InnerObjectFormatsParsedJSON error.  Bad JSON.");
            }
            STParsedJSONObject parsed ("request", req);
            bool const noObj = parsed.object == boost::none;
            if ( noObj == test.expectFail )
            {
                pass ();
            }
            else
            {
                std::string errStr ("Unexpected STParsedJSON result on:\n");
                errStr += test.txt;
                fail (errStr);
            }
        }
    }
};

BEAST_DEFINE_TESTSUITE(InnerObjectFormatsParsedJSON,ripple_app,ripple);

} // ripple
