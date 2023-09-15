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

#include <ripple/basics/strHex.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

#include <algorithm>
#include <iterator>

namespace ripple {
namespace test {

// Helper function that returns the owner count of an account root.
std::uint32_t
ownerCount(test::jtx::Env const& env, test::jtx::Account const& acct)
{
    std::uint32_t ret{0};
    if (auto const sleAcct = env.le(acct))
        ret = sleAcct->at(sfOwnerCount);
    return ret;
}

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
        testcase("Enabled");

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
        testcase("Account reserve");

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
        testcase("Invalid Set");

        using namespace jtx;
        using namespace std::chrono;

        Env env(*this);
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

        // only attestation
        env(did::set(alice), did::attestation("attest"), ter(tecEMPTY_DID));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // both empty fields
        env(did::set(alice),
            did::uri(""),
            did::document(""),
            ter(temEMPTY_DID));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // both empty fields with attestation
        env(did::set(alice),
            did::uri(""),
            did::document(""),
            did::attestation("attest"),
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
            did::attestation(longString),
            ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // Modifying a DID to become empty is checked in testSetModify
    }

    void
    testDeleteInvalid(FeatureBitset features)
    {
        testcase("Invalid Delete");

        using namespace jtx;
        using namespace std::chrono;

        Env env(*this);
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
        testcase("Valid Initial Set");

        using namespace jtx;
        using namespace std::chrono;

        Env env(*this);
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const charlie{"charlie"};
        Account const dave{"dave"};
        Account const edna{"edna"};
        Account const francis{"francis"};
        env.fund(XRP(5000), alice, bob, charlie, dave, edna, francis);
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, bob) == 0);
        BEAST_EXPECT(ownerCount(env, charlie) == 0);

        // only URI
        env(did::set(alice), did::uri("uri"));
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        // only URI, plus attestation
        env(did::set(bob), did::uri("uri"), did::attestation("attest"));
        BEAST_EXPECT(ownerCount(env, bob) == 1);

        // only DIDDocument
        env(did::set(charlie), did::document("data"));
        BEAST_EXPECT(ownerCount(env, charlie) == 1);

        // only DIDDocument, plus attestation
        env(did::set(dave), did::document("data"), did::attestation("attest"));
        BEAST_EXPECT(ownerCount(env, dave) == 1);

        // both URI and DIDDocument
        env(did::set(edna), did::uri("uri"), did::document("data"));
        BEAST_EXPECT(ownerCount(env, edna) == 1);

        // both URI and DIDDocument, plus Attestation
        env(did::set(francis),
            did::uri("uri"),
            did::document("data"),
            did::attestation("attest"));
        BEAST_EXPECT(ownerCount(env, francis) == 1);
    }

    void
    testSetModify(FeatureBitset features)
    {
        testcase("Modify DID with Set");

        using namespace jtx;
        using namespace std::chrono;

        Env env(*this);
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
            BEAST_EXPECT(!sleDID->isFieldPresent(sfAttestation));
        }

        // Try to delete URI, fails because no elements are set
        {
            env(did::set(alice), did::uri(""), ter(tecEMPTY_DID));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], initialURI));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfDIDDocument));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfAttestation));
        }

        // Set DIDDocument
        std::string const initialDocument = "data";
        {
            env(did::set(alice), did::document(initialDocument));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], initialURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfDIDDocument], initialDocument));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfAttestation));
        }

        // Set Attestation
        std::string const initialAttestation = "attest";
        {
            env(did::set(alice), did::attestation(initialAttestation));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], initialURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfDIDDocument], initialDocument));
            BEAST_EXPECT(checkVL((*sleDID)[sfAttestation], initialAttestation));
        }

        // Try to delete URI/DIDDocument, fails because no elements are set
        // (other than attestation)
        {
            env(did::set(alice),
                did::document(""),
                did::uri(""),
                ter(temEMPTY_DID));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], initialURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfDIDDocument], initialDocument));
            BEAST_EXPECT(checkVL((*sleDID)[sfAttestation], initialAttestation));
        }

        // Remove URI
        {
            env(did::set(alice), did::uri(""));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfDIDDocument], initialDocument));
            BEAST_EXPECT(checkVL((*sleDID)[sfAttestation], initialAttestation));
        }

        // Remove Attestation
        {
            env(did::set(alice), did::attestation(""));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfDIDDocument], initialDocument));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfAttestation));
        }

        // Remove Data + set URI
        std::string const secondURI = "uri2";
        {
            env(did::set(alice), did::uri(secondURI), did::document(""));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], secondURI));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfDIDDocument));
        }

        // Remove URI + set Document
        std::string const secondDocument = "data2";
        {
            env(did::set(alice), did::uri(""), did::document(secondDocument));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfDIDDocument], secondDocument));
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
        testEnabled(all);
        testAccountReserve(all);
        testSetInvalid(all);
        testDeleteInvalid(all);
        testSetModify(all);
    }
};

BEAST_DEFINE_TESTSUITE(DID, app, ripple);

}  // namespace test
}  // namespace ripple
