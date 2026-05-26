
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>  // IWYU pragma: keep
#include <test/jtx/did.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>

namespace xrpl::test {

struct DID_test : public beast::unit_test::Suite
{
    void
    testEnabled(FeatureBitset features)
    {
        testcase("featureDID Enabled");

        using namespace jtx;
        // If the DID amendment is not enabled, you should not be able
        // to set or delete DIDs.
        Env env{*this, features - featureDID};
        Account const alice{"alice"};
        env.fund(XRP(5000), alice);
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 0);
        env(did::setValid(alice), Ter(temDISABLED));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 0);
        env(did::del(alice), Ter(temDISABLED));
        env.close();
    }

    void
    testAccountReserve(FeatureBitset features)
    {
        // Verify that the reserve behaves as expected for minting.
        testcase("DID Account Reserve");

        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};

        // Fund alice enough to exist, but not enough to meet
        // the reserve for creating a DID.
        auto const acctReserve = env.current()->fees().reserve;
        auto const incReserve = env.current()->fees().increment;
        auto const baseFee = env.current()->fees().base;
        env.fund(acctReserve, alice);
        env.close();
        BEAST_EXPECT(env.balance(alice) == acctReserve);
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // alice does not have enough XRP to cover the reserve for a DID
        env(did::setValid(alice), Ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // Pay alice almost enough to make the reserve for a DID.
        env(pay(env.master, alice, drops(incReserve + 2 * baseFee - 1)));
        BEAST_EXPECT(env.balance(alice) == acctReserve + incReserve + drops(baseFee - 1));
        env.close();

        // alice still does not have enough XRP for the reserve of a DID.
        env(did::setValid(alice), Ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // Pay alice enough to make the reserve for a DID.
        env(pay(env.master, alice, drops(baseFee + 1)));
        env.close();

        // Now alice can create a DID.
        env(did::setValid(alice));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        // alice deletes her DID.
        env(did::del(alice));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        env.close();
    }

    void
    testSetInvalid(FeatureBitset features)
    {
        testcase("Invalid DIDSet");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        Account const alice{"alice"};
        env.fund(XRP(5000), alice);
        env.close();

        //----------------------------------------------------------------------
        // preflight

        // invalid flags
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        env(did::setValid(alice), Txflags(0x00010000), Ter(temINVALID_FLAG));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // no fields
        env(did::set(alice), Ter(temEMPTY_DID));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // all empty fields
        env(did::set(alice), did::Uri(""), did::Document(""), did::Data(""), Ter(temEMPTY_DID));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // uri is too long
        std::string const longString(257, 'a');
        env(did::set(alice), did::Uri(longString), Ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // document is too long
        env(did::set(alice), did::Document(longString), Ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // attestation is too long
        env(did::set(alice), did::Document("data"), did::Data(longString), Ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // some empty fields, some optional fields
        // pre-fix amendment
        auto const fixEnabled = env.current()->rules().enabled(fixEmptyDID);
        env(did::set(alice), did::Uri(""), fixEnabled ? Ter(tecEMPTY_DID) : Ter(tesSUCCESS));
        env.close();
        auto const expectedOwnerReserve = fixEnabled ? 0 : 1;
        BEAST_EXPECT(ownerCount(env, alice) == expectedOwnerReserve);

        // Modifying a DID to become empty is checked in testSetModify
    }

    void
    testDeleteInvalid(FeatureBitset features)
    {
        testcase("Invalid DIDDelete");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        Account const alice{"alice"};
        env.fund(XRP(5000), alice);
        env.close();

        //----------------------------------------------------------------------
        // preflight

        // invalid flags
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        env(did::del(alice), Txflags(0x00010000), Ter(temINVALID_FLAG));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        //----------------------------------------------------------------------
        // doApply

        // DID doesn't exist
        env(did::del(alice), Ter(tecNO_ENTRY));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    void
    testSetValidInitial(FeatureBitset features)
    {
        testcase("Valid Initial DIDSet");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const charlie{"charlie"};
        Account const dave{"dave"};
        Account const edna{"edna"};
        Account const francis{"francis"};
        Account const george{"george"};
        env.fund(XRP(5000), alice, bob, charlie, dave, edna, francis, george);
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, bob) == 0);
        BEAST_EXPECT(ownerCount(env, charlie) == 0);

        // only URI
        env(did::set(alice), did::Uri("uri"));
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        // only DIDDocument
        env(did::set(bob), did::Document("data"));
        BEAST_EXPECT(ownerCount(env, bob) == 1);

        // only Data
        env(did::set(charlie), did::Data("data"));
        BEAST_EXPECT(ownerCount(env, charlie) == 1);

        // URI + Data
        env(did::set(dave), did::Uri("uri"), did::Data("attest"));
        BEAST_EXPECT(ownerCount(env, dave) == 1);

        // URI + DIDDocument
        env(did::set(edna), did::Uri("uri"), did::Document("data"));
        BEAST_EXPECT(ownerCount(env, edna) == 1);

        // DIDDocument + Data
        env(did::set(francis), did::Document("data"), did::Data("attest"));
        BEAST_EXPECT(ownerCount(env, francis) == 1);

        // URI + DIDDocument + Data
        env(did::set(george), did::Uri("uri"), did::Document("data"), did::Data("attest"));
        BEAST_EXPECT(ownerCount(env, george) == 1);
    }

    void
    testSetModify(FeatureBitset features)
    {
        testcase("Modify DID with DIDSet");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        Account const alice{"alice"};
        env.fund(XRP(5000), alice);
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        auto const ar = env.le(alice);

        // Create DID
        std::string const initialURI = "uri";
        {
            env(did::set(alice), did::Uri(initialURI));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(sleDID);
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], initialURI));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfDIDDocument));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfData));
        }

        // Try to delete URI, fails because no elements are set
        {
            env(did::set(alice), did::Uri(""), Ter(tecEMPTY_DID));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], initialURI));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfDIDDocument));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfData));
        }

        // Set DIDDocument
        std::string const initialDocument = "data";
        {
            env(did::set(alice), did::Document(initialDocument));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], initialURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfDIDDocument], initialDocument));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfData));
        }

        // Set Data
        std::string const initialData = "attest";
        {
            env(did::set(alice), did::Data(initialData));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], initialURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfDIDDocument], initialDocument));
            BEAST_EXPECT(checkVL((*sleDID)[sfData], initialData));
        }

        // Remove URI
        {
            env(did::set(alice), did::Uri(""));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfDIDDocument], initialDocument));
            BEAST_EXPECT(checkVL((*sleDID)[sfData], initialData));
        }

        // Remove Data
        {
            env(did::set(alice), did::Data(""));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfDIDDocument], initialDocument));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfData));
        }

        // Remove Data + set URI
        std::string const secondURI = "uri2";
        {
            env(did::set(alice), did::Uri(secondURI), did::Document(""));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], secondURI));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfDIDDocument));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfData));
        }

        // Remove URI + set DIDDocument
        std::string const secondDocument = "data2";
        {
            env(did::set(alice), did::Uri(""), did::Document(secondDocument));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfDIDDocument], secondDocument));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfData));
        }

        // Remove DIDDocument + set Data
        std::string const secondData = "randomData";
        {
            env(did::set(alice), did::Document(""), did::Data(secondData));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfURI));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfDIDDocument));
            BEAST_EXPECT(checkVL((*sleDID)[sfData], secondData));
        }

        // Delete DID
        {
            env(did::del(alice));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(!sleDID);
        }
    }

    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};
        FeatureBitset const emptyDID{fixEmptyDID};
        testEnabled(all);
        testAccountReserve(all);
        testSetInvalid(all);
        testDeleteInvalid(all);
        testSetValidInitial(all);
        testSetModify(all);

        testEnabled(all - emptyDID);
        testAccountReserve(all - emptyDID);
        testSetInvalid(all - emptyDID);
        testDeleteInvalid(all - emptyDID);
        testSetValidInitial(all - emptyDID);
        testSetModify(all - emptyDID);
    }
};

BEAST_DEFINE_TESTSUITE(DID, app, xrpl);

}  // namespace xrpl::test
