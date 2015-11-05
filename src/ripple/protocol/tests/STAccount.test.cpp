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
#include <beast/unit_test/suite.h>

namespace ripple {

struct STAccount_test : public beast::unit_test::suite
{
    void
    testSTAccount()
    {
        {
            // Test default constructor.
            STAccount const defaultAcct;
            expect (defaultAcct.getSType() == STI_ACCOUNT);
            expect (defaultAcct.getText() == "");
            expect (defaultAcct.isDefault() == true);
            expect (defaultAcct.value() == AccountID {});
            {
#ifdef NDEBUG // Qualified because the serialization asserts in a debug build.
                Serializer s;
                defaultAcct.add (s); // Asserts in debug build
                expect (s.size() == 1);
                expect (s.getHex() == "00");
                SerialIter sit (s.slice ());
                STAccount const deserializedDefault (sit, sfAccount);
                expect (deserializedDefault.isEquivalent (defaultAcct));
#endif // NDEBUG
            }
            {
                // Construct a deserialized default STAccount.
                Serializer s;
                s.addVL (nullptr, 0);
                SerialIter sit (s.slice ());
                STAccount const deserializedDefault (sit, sfAccount);
                expect (deserializedDefault.isEquivalent (defaultAcct));
            }

            // Test constructor from SField.
            STAccount const sfAcct {sfAccount};
            expect (sfAcct.getSType() == STI_ACCOUNT);
            expect (sfAcct.getText() == "");
            expect (sfAcct.isDefault());
            expect (sfAcct.value() == AccountID {});
            expect (sfAcct.isEquivalent (defaultAcct));
            {
                Serializer s;
                sfAcct.add (s);
                expect (s.size() == 1);
                expect (s.getHex() == "00");
                SerialIter sit (s.slice ());
                STAccount const deserializedSf (sit, sfAccount);
                expect (deserializedSf.isEquivalent(sfAcct));
            }

            // Test constructor from SField and AccountID.
            STAccount const zeroAcct {sfAccount, AccountID{}};
            expect (zeroAcct.getText() == "rrrrrrrrrrrrrrrrrrrrrhoLvTp");
            expect (! zeroAcct.isDefault());
            expect (zeroAcct.value() == AccountID {0});
            expect (! zeroAcct.isEquivalent (defaultAcct));
            expect (! zeroAcct.isEquivalent (sfAcct));
            {
                Serializer s;
                zeroAcct.add (s);
                expect (s.size() == 21);
                expect (s.getHex() ==
                    "140000000000000000000000000000000000000000");
                SerialIter sit (s.slice ());
                STAccount const deserializedZero (sit, sfAccount);
                expect (deserializedZero.isEquivalent (zeroAcct));
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
                    expect (ex.what() == std::string("Invalid STAccount size"));
                }

            }

            // Interestingly, equal values but different types are equivalent!
            STAccount const regKey {sfRegularKey, AccountID{}};
            expect (regKey.isEquivalent (zeroAcct));

            // Test assignment.
            STAccount assignAcct;
            expect (assignAcct.isEquivalent (defaultAcct));
            expect (assignAcct.isDefault());
            assignAcct = AccountID{};
            expect (! assignAcct.isEquivalent (defaultAcct));
            expect (assignAcct.isEquivalent (zeroAcct));
            expect (! assignAcct.isDefault());
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
