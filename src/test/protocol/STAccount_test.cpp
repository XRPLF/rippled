//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2015 Ripple Labs Inc.

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
#include <ripple/protocol/STAccount.h>
#include <ripple/beast/unit_test.h>

namespace ripple {

struct STAccount_test : public beast::unit_test::suite
{
    void
    testSTAccount()
    {
        {
            // Test default constructor.
            STAccount const defaultAcct;
            BEAST_EXPECT(defaultAcct.getSType() == STI_ACCOUNT);
            BEAST_EXPECT(defaultAcct.getText() == "");
            BEAST_EXPECT(defaultAcct.isDefault() == true);
            BEAST_EXPECT(defaultAcct.value() == AccountID {});
            {
#ifdef NDEBUG // Qualified because the serialization asserts in a debug build.
                Serializer s;
                defaultAcct.add (s); // Asserts in debug build
                BEAST_EXPECT(s.size() == 1);
                BEAST_EXPECT(s.getHex() == "00");
                SerialIter sit (s.slice ());
                STAccount const deserializedDefault (sit, sfAccount);
                BEAST_EXPECT(deserializedDefault.isEquivalent (defaultAcct));
#endif // NDEBUG
            }
            {
                // Construct a deserialized default STAccount.
                Serializer s;
                s.addVL (nullptr, 0);
                SerialIter sit (s.slice ());
                STAccount const deserializedDefault (sit, sfAccount);
                BEAST_EXPECT(deserializedDefault.isEquivalent (defaultAcct));
            }

            // Test constructor from SField.
            STAccount const sfAcct {sfAccount};
            BEAST_EXPECT(sfAcct.getSType() == STI_ACCOUNT);
            BEAST_EXPECT(sfAcct.getText() == "");
            BEAST_EXPECT(sfAcct.isDefault());
            BEAST_EXPECT(sfAcct.value() == AccountID {});
            BEAST_EXPECT(sfAcct.isEquivalent (defaultAcct));
            {
                Serializer s;
                sfAcct.add (s);
                BEAST_EXPECT(s.size() == 1);
                BEAST_EXPECT(s.getHex() == "00");
                SerialIter sit (s.slice ());
                STAccount const deserializedSf (sit, sfAccount);
                BEAST_EXPECT(deserializedSf.isEquivalent(sfAcct));
            }

            // Test constructor from SField and AccountID.
            STAccount const zeroAcct {sfAccount, AccountID{}};
            BEAST_EXPECT(zeroAcct.getText() == "rrrrrrrrrrrrrrrrrrrrrhoLvTp");
            BEAST_EXPECT(! zeroAcct.isDefault());
            BEAST_EXPECT(zeroAcct.value() == AccountID {0});
            BEAST_EXPECT(! zeroAcct.isEquivalent (defaultAcct));
            BEAST_EXPECT(! zeroAcct.isEquivalent (sfAcct));
            {
                Serializer s;
                zeroAcct.add (s);
                BEAST_EXPECT(s.size() == 21);
                BEAST_EXPECT(s.getHex() ==
                    "140000000000000000000000000000000000000000");
                SerialIter sit (s.slice ());
                STAccount const deserializedZero (sit, sfAccount);
                BEAST_EXPECT(deserializedZero.isEquivalent (zeroAcct));
            }
            {
                // Construct from a VL that is not exactly 160 bits.
                Serializer s;
                const std::uint8_t bits128[] {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
                s.addVL (bits128, sizeof (bits128));
                SerialIter sit (s.slice ());
                try
                {
                    // Constructing an STAccount with a bad size should throw.
                    STAccount const deserializedBadSize (sit, sfAccount);
                }
                catch (std::runtime_error const& ex)
                {
                    BEAST_EXPECT(ex.what() == std::string("Invalid STAccount size"));
                }

            }

            // Interestingly, equal values but different types are equivalent!
            STAccount const regKey {sfRegularKey, AccountID{}};
            BEAST_EXPECT(regKey.isEquivalent (zeroAcct));

            // Test assignment.
            STAccount assignAcct;
            BEAST_EXPECT(assignAcct.isEquivalent (defaultAcct));
            BEAST_EXPECT(assignAcct.isDefault());
            assignAcct = AccountID{};
            BEAST_EXPECT(! assignAcct.isEquivalent (defaultAcct));
            BEAST_EXPECT(assignAcct.isEquivalent (zeroAcct));
            BEAST_EXPECT(! assignAcct.isDefault());
        }
    }

    void
    run() override
    {
        testSTAccount();
    }
};

BEAST_DEFINE_TESTSUITE(STAccount,protocol,ripple);

}
