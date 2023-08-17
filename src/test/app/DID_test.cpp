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

namespace did {

Json::Value
set(jtx::Account const& account)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::DIDSet;
    jv[jss::Account] = to_string(account.id());
    jv[jss::Flags] = tfUniversal;
    return jv;
}

Json::Value
setValid(jtx::Account const& account)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::DIDSet;
    jv[jss::Account] = to_string(account.id());
    jv[jss::Flags] = tfUniversal;
    jv[sfURI.jsonName] = strHex(std::string{"uri"});
    return jv;
}

/** Sets the optional URI on a DIDSet. */
class uri
{
private:
    std::string uri_;

public:
    explicit uri(std::string const& u) : uri_(strHex(u))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        jtx.jv[sfURI.jsonName] = uri_;
    }
};

/** Sets the optional URI on a DIDSet. */
class data
{
private:
    std::string data_;

public:
    explicit data(std::string const& u) : data_(strHex(u))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        jtx.jv[sfData.jsonName] = data_;
    }
};

Json::Value
del(jtx::Account const& account)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::DIDDelete;
    jv[jss::Account] = to_string(account.id());
    jv[jss::Flags] = tfUniversal;
    return jv;
}

}  // namespace did

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

        // both empty fields
        env(did::set(alice), did::uri(""), did::data(""), ter(temEMPTY_DID));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // uri is too long
        const std::string longString(257, 'a');
        env(did::set(alice), did::uri(longString), ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // data is too long
        env(did::set(alice), did::data(longString), ter(temMALFORMED));
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
        env.fund(XRP(5000), alice, bob, charlie);
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, bob) == 0);
        BEAST_EXPECT(ownerCount(env, charlie) == 0);

        // only URI
        env(did::set(alice), did::uri("uri"));
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        // only Data
        env(did::set(bob), did::data("data"));
        BEAST_EXPECT(ownerCount(env, bob) == 1);

        // both URI and Data
        env(did::set(charlie), did::uri("uri"), did::data("data"));
        BEAST_EXPECT(ownerCount(env, charlie) == 1);
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
            BEAST_EXPECT(!sleDID->isFieldPresent(sfData));
        }

        // Try to delete URI, fails because no elements are set
        {
            env(did::set(alice), did::uri(""), ter(tecEMPTY_DID));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], initialURI));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfData));
        }

        // Set Data
        std::string const initialData = "data";
        {
            env(did::set(alice), did::data("data"));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], initialURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfData], initialData));
        }

        // Remove URI
        {
            env(did::set(alice), did::uri(""));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfURI));
            BEAST_EXPECT(checkVL((*sleDID)[sfData], initialData));
        }

        // Remove Data + set URI
        std::string const secondURI = "uri2";
        {
            env(did::set(alice), did::uri(secondURI), did::data(""));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(checkVL((*sleDID)[sfURI], secondURI));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfData));
        }

        // Remove URI + set Data
        std::string const secondData = "data2";
        {
            env(did::set(alice), did::uri(""), did::data(secondData));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            auto const sleDID = env.le(keylet::did(alice.id()));
            BEAST_EXPECT(!sleDID->isFieldPresent(sfURI));
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
