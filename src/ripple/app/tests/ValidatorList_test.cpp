//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2015 Ripple Labs Inc.

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
#include <ripple/basics/Slice.h>
#include <ripple/basics/TestSuite.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/protocol/SecretKey.h>

namespace ripple {
namespace tests {

class ValidatorList_test : public ripple::TestSuite
{
private:
    static
    PublicKey
    randomNode ()
    {
        return derivePublicKey (
            KeyType::secp256k1,
            randomSecretKey());
    }

    static
    PublicKey
    randomMasterKey ()
    {
        return derivePublicKey (
            KeyType::ed25519,
            randomSecretKey());
    }

    static
    bool
    isPresent (
        std::vector<PublicKey> container,
        PublicKey const& item)
    {
        auto found = std::find (
            std::begin (container),
            std::end (container),
            item);

        return (found != std::end (container));
    }

    void
    testConfigLoad ()
    {
        testcase ("Config Load");

        auto validators = std::make_unique <ValidatorList> (beast::Journal ());

        std::vector<PublicKey> network;
        network.reserve(8);

        while (network.size () != 8)
            network.push_back (randomNode());

        auto format = [](
            PublicKey const &publicKey,
            char const* comment = nullptr)
        {
            auto ret = toBase58(
                TokenType::TOKEN_NODE_PUBLIC,
                publicKey);

            if (comment)
                ret += comment;

            return ret;
        };

        Section s1;

        // Correct (empty) configuration
        expect (validators->load (s1));
        expect (validators->size() == 0);

        // Correct configuration
        s1.append (format (network[0]));
        s1.append (format (network[1], " Comment"));
        s1.append (format (network[2], " Multi Word Comment"));
        s1.append (format (network[3], "    Leading Whitespace"));
        s1.append (format (network[4], " Trailing Whitespace    "));
        s1.append (format (network[5], "    Leading & Trailing Whitespace    "));
        s1.append (format (network[6], "    Leading, Trailing & Internal    Whitespace    "));
        s1.append (format (network[7], "    "));

        expect (validators->load (s1));

        for (auto const& n : network)
            expect (validators->trusted (n));

        // Incorrect configurations:
        Section s2;
        s2.append ("NotAPublicKey");
        expect (!validators->load (s2));

        Section s3;
        s3.append (format (network[0], "!"));
        expect (!validators->load (s3));

        Section s4;
        s4.append (format (network[0], "!  Comment"));
        expect (!validators->load (s4));

        // Check if we properly terminate when we encounter
        // a malformed or unparseable entry:
        auto const node1 = randomNode();
        auto const node2 = randomNode ();

        Section s5;
        s5.append (format (node1, "XXX"));
        s5.append (format (node2));
        expect (!validators->load (s5));
        expect (!validators->trusted (node1));
        expect (!validators->trusted (node2));

        // Add Ed25519 master public keys to permanent validators list
        auto const masterNode1 = randomMasterKey ();
        auto const masterNode2 = randomMasterKey ();

        Section s6;
        s6.append (format (masterNode1));
        s6.append (format (masterNode2, " Comment"));
        expect (validators->load (s6));
        expect (validators->trusted (masterNode1));
        expect (validators->trusted (masterNode2));
    }

    void
    testMembership ()
    {
        // The servers on the permanentValidators
        std::vector<PublicKey> permanentValidators;
        std::vector<PublicKey> ephemeralValidators;

        while (permanentValidators.size () != 64)
            permanentValidators.push_back (randomNode());

        while (ephemeralValidators.size () != 64)
            ephemeralValidators.push_back (randomNode());

        {
            testcase ("Membership: No Validators");

            auto vl = std::make_unique <ValidatorList> (beast::Journal ());

            for (auto const& v : permanentValidators)
                expect (!vl->trusted (v));

            for (auto const& v : ephemeralValidators)
                expect (!vl->trusted (v));
        }

        {
            testcase ("Membership: Non-Empty, Some Present, Some Not Present");

            std::vector<PublicKey> p (
                permanentValidators.begin (),
                permanentValidators.begin () + 16);

            while (p.size () != 32)
                p.push_back (randomNode());

            std::vector<PublicKey> e (
                ephemeralValidators.begin (),
                ephemeralValidators.begin () + 16);

            while (e.size () != 32)
                e.push_back (randomNode());

            auto vl = std::make_unique <ValidatorList> (beast::Journal ());

            for (auto const& v : p)
                vl->insertPermanentKey (v, "");

            for (auto const& v : e)
                vl->insertEphemeralKey (v, "");

            for (auto const& v : p)
                expect (vl->trusted (v));

            for (auto const& v : e)
                expect (vl->trusted (v));

            for (auto const& v : permanentValidators)
                expect (static_cast<bool>(vl->trusted (v)) == isPresent (p, v));

            for (auto const& v : ephemeralValidators)
                expect (static_cast<bool>(vl->trusted (v)) == isPresent (e, v));
        }
    }

    void
    testModification ()
    {
        testcase ("Insertion and Removal");

        auto vl = std::make_unique <ValidatorList> (beast::Journal ());

        auto const v = randomNode ();

        // Inserting a new permanent key succeeds
        expect (vl->insertPermanentKey (v, "Permanent"));
        {
            auto member = vl->member (v);
            expect (static_cast<bool>(member));
            expect (member->compare("Permanent") == 0);
        }
        // Inserting the same permanent key fails:
        expect (!vl->insertPermanentKey (v, ""));
        {
            auto member = vl->member (v);
            expect (static_cast<bool>(member));
            expect (member->compare("Permanent") == 0);
        }
        // Inserting the same key as ephemeral fails:
        expect (!vl->insertEphemeralKey (v, "Ephemeral"));
        {
            auto member = vl->member (v);
            expect (static_cast<bool>(member));
            expect (member->compare("Permanent") == 0);
        }
        // Removing the key as ephemeral fails:
        expect (!vl->removeEphemeralKey (v));
        {
            auto member = vl->member (v);
            expect (static_cast<bool>(member));
            expect (member->compare("Permanent") == 0);
        }
        // Deleting the key as permanent succeeds:
        expect (vl->removePermanentKey (v));
        expect (!static_cast<bool>(vl->trusted (v)));

        // Insert an ephemeral validator key
        expect (vl->insertEphemeralKey (v, "Ephemeral"));
        {
            auto member = vl->member (v);
            expect (static_cast<bool>(member));
            expect (member->compare("Ephemeral") == 0);
        }
        // Inserting the same ephemeral key fails
        expect (!vl->insertEphemeralKey (v, ""));
        {
            auto member = vl->member (v);
            expect (static_cast<bool>(member));
            expect (member->compare("Ephemeral") == 0);
        }
        // Inserting the same key as permanent fails:
        expect (!vl->insertPermanentKey (v, "Permanent"));
        {
            auto member = vl->member (v);
            expect (static_cast<bool>(member));
            expect (member->compare("Ephemeral") == 0);
        }
        // Deleting the key as permanent fails:
        expect (!vl->removePermanentKey (v));
        {
            auto member = vl->member (v);
            expect (static_cast<bool>(member));
            expect (member->compare("Ephemeral") == 0);
        }
        // Deleting the key as ephemeral succeeds:
        expect (vl->removeEphemeralKey (v));
        expect (!vl->trusted(v));
    }

public:
    void
    run() override
    {
        testConfigLoad();
        testMembership ();
        testModification ();
    }
};

BEAST_DEFINE_TESTSUITE(ValidatorList, app, ripple);

} // tests
} // ripple
