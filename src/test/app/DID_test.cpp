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

#include <test/jtx.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <iterator>

namespace ripple {
namespace test {

bool
checkVL(Slice const& result, std::string expected)
{
    Serializer s;
    s.addRaw(result);
    return s.getString() == expected;
}

struct DID_test : public beast::unit_test::suite
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
        env(did::setValid(alice), ter(temDISABLED));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 0);
        env(did::del(alice), ter(temDISABLED));
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
        auto const acctReserve = env.current()->fees().accountReserve(0);
        auto const incReserve = env.current()->fees().increment;
        env.fund(acctReserve, alice);
        env.close();
        BEAST_EXPECT(env.balance(alice) == acctReserve);
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // alice does not have enough XRP to cover the reserve for a DID
        env(did::setValid(alice), ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // Pay alice almost enough to make the reserve for a DID.
        env(pay(env.master, alice, incReserve + drops(19)));
        BEAST_EXPECT(env.balance(alice) == acctReserve + incReserve + drops(9));
        env.close();

        // alice still does not have enough XRP for the reserve of a DID.
        env(did::setValid(alice), ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // Pay alice enough to make the reserve for a DID.
        env(pay(env.master, alice, drops(11)));
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
        env(did::setValid(alice), txflags(0x00010000), ter(temINVALID_FLAG));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // no fields
        env(did::set(alice), ter(temEMPTY_DID));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // all empty fields
        env(did::set(alice),
            did::uri(""),
            did::document(""),
            did::data(""),
            ter(temEMPTY_DID));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // uri is too long
        const std::string longString(257, 'a');
        env(did::set(alice), did::uri(longString), ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // document is too long
        env(did::set(alice), did::document(longString), ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // attestation is too long
        env(did::set(alice),
            did::document("data"),
            did::data(longString),
            ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // some empty fields, some optional fields
        // pre-fix amendment
        auto const fixEnabled = env.current()->rules().enabled(fixEmptyDID);
        env(did::set(alice),
            did::uri(""),
            fixEnabled ? ter(tecEMPTY_DID) : ter(tesSUCCESS));
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
        env(did::del(alice), txflags(0x00010000), ter(temINVALID_FLAG));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        //----------------------------------------------------------------------
        // doApply

        // DID doesn't exist
        env(did::del(alice), ter(tecNO_ENTRY));
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
        env.fund(XRP(5000), alice, bob, charlie, dave, edna, francis);
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, bob) == 0);
        BEAST_EXPECT(ownerCount(env, charlie) == 0);

        // only URI
        env(did::set(alice), did::uri("uri"));
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        // only DIDDocument
        env(did::set(bob), did::document("data"));
        BEAST_EXPECT(ownerCount(env, bob) == 1);

        // only Data
        env(did::set(charlie), did::data("data"));
        BEAST_EXPECT(ownerCount(env, charlie) == 1);

        // URI + Data
        env(did::set(dave), did::uri("uri"), did::data("attest"));
        BEAST_EXPECT(ownerCount(env, dave) == 1);

        // URI + DIDDocument
        env(did::set(edna), did::uri("uri"), did::document("data"));
        BEAST_EXPECT(ownerCount(env, edna) == 1);

        // DIDDocument + Data
        env(did::set(francis), did::document("data"), did::data("attest"));
        BEAST_EXPECT(ownerCount(env, francis) == 1);

        // URI + DIDDocument + Data
        env(did::set(george),
            did::uri("uri"),
            did::document("data"),
            did::data("attest"));
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
            env(did::set(alice), did::uri(initialURI));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(sleDID);
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], initialURI));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfDIDDocument));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfData));
        }

        // Try to delete URI, fails because no elements are set
        {
            env(did::set(alice), did::uri(""), ter(tecEMPTY_DID));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], initialURI));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfDIDDocument));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfData));
        }

        // Set DIDDocument
        std::string const initialDocument = "data";
        {
            env(did::set(alice), did::document(initialDocument));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], initialURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfDIDDocument], initialDocument));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfData));
        }

        // Set Data
        std::string const initialData = "attest";
        {
            env(did::set(alice), did::data(initialData));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], initialURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfDIDDocument], initialDocument));
            BEAST_EXPECT(checkVL((*sleDID)[sfData], initialData));
        }

        // Remove URI
        {
            env(did::set(alice), did::uri(""));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfDIDDocument], initialDocument));
            BEAST_EXPECT(checkVL((*sleDID)[sfData], initialData));
        }

        // Remove Data
        {
            env(did::set(alice), did::data(""));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfDIDDocument], initialDocument));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfData));
        }

        // Remove Data + set URI
        std::string const secondURI = "uri2";
        {
            env(did::set(alice), did::uri(secondURI), did::document(""));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], secondURI));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfDIDDocument));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfData));
        }

        // Remove URI + set DIDDocument
        std::string const secondDocument = "data2";
        {
            env(did::set(alice), did::uri(""), did::document(secondDocument));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfDIDDocument], secondDocument));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfData));
        }

        // Remove DIDDocument + set Data
        std::string const secondData = "randomData";
        {
            env(did::set(alice), did::document(""), did::data(secondData));
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
        FeatureBitset const all{supported_amendments()};
        FeatureBitset const emptyDID{fixEmptyDID};
        testEnabled(all);
        testAccountReserve(all);
        testSetInvalid(all);
        testDeleteInvalid(all);
        testSetModify(all);

        testEnabled(all - emptyDID);
        testAccountReserve(all - emptyDID);
        testSetInvalid(all - emptyDID);
        testDeleteInvalid(all - emptyDID);
        testSetModify(all - emptyDID);
    }
};

BEAST_DEFINE_TESTSUITE(DID, app, ripple);

}  // namespace test
}  // namespace ripple
