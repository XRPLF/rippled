
#include <test/jtx/Env.h>

#include <xrpl/basics/contract.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_reader.h>  // Json::Reader
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>    // RPC::containsError
#include <xrpl/protocol/STParsedJSON.h>  // STParsedJSONObject

#include <stdexcept>
#include <string>

namespace xrpl {

namespace InnerObjectFormatsUnitTestDetail {

struct TestJSONTxt
{
    std::string const txt;
    bool const expectFail;
};

static TestJSONTxt const testArray[] = {

    // Valid SignerEntry
    {.txt = R"({
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
})",
     .expectFail = false},

    // SignerEntry missing Account
    {.txt = R"({
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
})",
     .expectFail = true},

    // SignerEntry missing SignerWeight
    {.txt = R"({
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
})",
     .expectFail = true},

    // SignerEntry with unexpected Amount
    {.txt = R"({
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
})",
     .expectFail = true},

    // SignerEntry with no Account and unexpected Amount
    {.txt = R"({
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
})",
     .expectFail = true},

};

}  // namespace InnerObjectFormatsUnitTestDetail

class InnerObjectFormatsParsedJSON_test : public beast::unit_test::suite
{
public:
    void
    run() override
    {
        using namespace InnerObjectFormatsUnitTestDetail;

        // Instantiate a jtx::Env so debugLog writes are exercised.
        test::jtx::Env const env(*this);

        for (auto const& test : testArray)
        {
            Json::Value req;
            Json::Reader().parse(test.txt, req);
            if (RPC::contains_error(req))
            {
                Throw<std::runtime_error>(
                    "Internal InnerObjectFormatsParsedJSON error.  Bad JSON.");
            }
            STParsedJSONObject const parsed("request", req);
            bool const noObj = !parsed.object.has_value();
            if (noObj == test.expectFail)
            {
                pass();
            }
            else
            {
                std::string errStr("Unexpected STParsedJSON result on:\n");
                errStr += test.txt;
                fail(errStr);
            }
        }
    }
};

BEAST_DEFINE_TESTSUITE(InnerObjectFormatsParsedJSON, protocol, xrpl);

}  // namespace xrpl
