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
#include <ripple/basics/TestSuite.h>
#include <ripple/app/misc/ValidatorList.h>

namespace ripple {
namespace tests {

class ValidatorList_test : public ripple::TestSuite
{
private:
    static
    PublicKey
    asPublicKey(RippleAddress const& raPublicKey)
    {
        auto const& blob = raPublicKey.getNodePublic();

        if (blob.empty())
            LogicError ("Can't convert invalid RippleAddress to PublicKey");

        return PublicKey(Slice(blob.data(), blob.size()));
    }

    static
    RippleAddress
    randomNode ()
    {
        return RippleAddress::createNodePublic (
            RippleAddress::createSeedRandom ());
    }

    static
    bool
    isPresent (
        std::vector<RippleAddress> container,
        RippleAddress const& item)
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

        std::vector<RippleAddress> network;

        while (network.size () != 8)
            network.push_back (randomNode());

        Section s1;

        // Correct (empty) configuration
        expect (validators->load (s1));
        expect (validators->size() == 0);

        // Correct configuration
        s1.append (network[0].humanNodePublic());
        s1.append (network[1].humanNodePublic() + " Comment");
        s1.append (network[2].humanNodePublic() + " Multi Word Comment");
        s1.append (network[3].humanNodePublic() + "    Leading Whitespace");
        s1.append (network[4].humanNodePublic() + " Trailing Whitespace    ");
        s1.append (network[5].humanNodePublic() + "    Leading & Trailing Whitespace    ");
        s1.append (network[6].humanNodePublic() + "    Leading, Trailing & Internal    Whitespace    ");
        s1.append (network[7].humanNodePublic() + "    ");

        expect (validators->load (s1));

        for (auto const& n : network)
            expect (validators->trusted (n));

        // Incorrect configurations:
        Section s2;
        s2.append ("NotAPublicKey");

        expect (!validators->load (s2));

        Section s3;
        s3.append ("@" + network[0].humanNodePublic());
        expect (!validators->load (s3));

        Section s4;
        s4.append (network[0].humanNodePublic() + "!");
        expect (!validators->load (s4));

        Section s5;
        s5.append (network[0].humanNodePublic() + "!  Comment");
        expect (!validators->load (s5));

        // Check if we properly terminate when we encounter
        // a malformed or unparseable entry:
        auto const badNode = randomNode();
        auto const goodNode = randomNode ();

        Section s6;
        s6.append (badNode.humanNodePublic() + "XXX");
        s6.append (goodNode.humanNodePublic());

        expect (!validators->load (s6));
        expect (!validators->trusted (badNode));
        expect (!validators->trusted (goodNode));
    }

    void
    testMembership ()
    {
        // The servers on the permanentValidators
        std::vector<RippleAddress> permanentValidators;
        std::vector<RippleAddress> ephemeralValidators;

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

            std::vector<RippleAddress> p (
                permanentValidators.begin (),
                permanentValidators.begin () + 16);

            while (p.size () != 32)
                p.push_back (randomNode());

            std::vector<RippleAddress> e (
                ephemeralValidators.begin (),
                ephemeralValidators.begin () + 16);

            while (e.size () != 32)
                e.push_back (randomNode());

            auto vl = std::make_unique <ValidatorList> (beast::Journal ());

            for (auto const& v : p)
                vl->insertPermanentKey (v, v.ToString());

            for (auto const& v : e)
                vl->insertEphemeralKey (asPublicKey (v), v.ToString());

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
        expect (!vl->insertEphemeralKey (asPublicKey(v), "Ephemeral"));
        {
            auto member = vl->member (v);
            expect (static_cast<bool>(member));
            expect (member->compare("Permanent") == 0);
        }
        // Removing the key as ephemeral fails:
        expect (!vl->removeEphemeralKey (asPublicKey(v)));
        {
            auto member = vl->member (v);
            expect (static_cast<bool>(member));
            expect (member->compare("Permanent") == 0);
        }
        // Deleting the key as permanent succeeds:
        expect (vl->removePermanentKey (v));
        expect (!static_cast<bool>(vl->trusted (v)));

        // Insert an ephemeral validator key
        expect (vl->insertEphemeralKey (asPublicKey(v), "Ephemeral"));
        {
            auto member = vl->member (v);
            expect (static_cast<bool>(member));
            expect (member->compare("Ephemeral") == 0);
        }
        // Inserting the same ephemeral key fails
        expect (!vl->insertEphemeralKey (asPublicKey(v), ""));
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
        expect (vl->removeEphemeralKey (asPublicKey(v)));
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
