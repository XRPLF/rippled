//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Dev Null Productions

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

#include <ripple/app/tx/apply.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/Feature.h>
#include <test/jtx/Env.h>

namespace ripple {

class Apply_test : public beast::unit_test::suite
{
public:
    void
    run() override
    {
        testcase("Require Fully Canonicial Signature");
        testFullyCanonicalSigs();
    }

    void
    testFullyCanonicalSigs()
    {
        // Construct a payments w/out a fully-canonical tx
        // clang-format off
        std::uint8_t const non_fully_canonical_tx[] =
        {
            0x12, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x01, 0x20, 0x1B, 0x00, 0x49, 0x7D,
            0x9C, 0x61, 0x40, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x69, 0x50, 0x68, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x0C, 0x73, 0x21, 0x03, 0x76, 0x7C, 0x7B, 0x2C, 0x13, 0xAD, 0x90, 0x05, 0x0A, 0x42, 0x63, 0x74, 0x5E, 0x4B,
            0xAB, 0x2B, 0x97, 0x54, 0x17, 0xFA, 0x22, 0xE8, 0x77, 0x80, 0xE1, 0x50, 0x6D, 0xDA, 0xF2, 0x11, 0x39, 0xBE,
            0x74, 0x48, 0x30, 0x46, 0x02, 0x21, 0x00, 0xE9, 0x56, 0x70, 0x98, 0x8A, 0x34, 0xC4, 0xDB, 0x0F, 0xA7, 0x3A,
            0x8B, 0xFD, 0x63, 0x83, 0x87, 0x2A, 0xF4, 0x38, 0xC1, 0x47, 0xA6, 0x2B, 0xC8, 0x38, 0x74, 0x06, 0x29, 0x8C,
            0x3E, 0xAD, 0xC1, 0x02, 0x21, 0x00, 0xA7, 0xDC, 0x80, 0x50, 0x8E, 0xD5, 0xA4, 0x75, 0x07, 0x05, 0xC7, 0x02,
            0xA8, 0x1C, 0xBF, 0x9D, 0x2C, 0x2D, 0xC3, 0xAF, 0xED, 0xBE, 0xD3, 0x7B, 0xBC, 0xCD, 0x97, 0xBC, 0x8C, 0x40,
            0xE0, 0x8F, 0x81, 0x14, 0xE2, 0x5A, 0x26, 0x43, 0x7D, 0x92, 0x3E, 0xEF, 0x4D, 0x6D, 0x81, 0x5D, 0xF9, 0x33,
            0x68, 0xB6, 0x2E, 0x64, 0x40, 0x84, 0x83, 0x14, 0xBB, 0x85, 0x99, 0x69, 0x36, 0xE4, 0xF5, 0x95, 0x28, 0x77,
            0x74, 0x68, 0x4D, 0xC2, 0xAC, 0x62, 0x66, 0x02, 0x4B, 0xEF
        };
        // clang-format on

        STTx const tx =
            *std::make_shared<STTx const>(makeSlice(non_fully_canonical_tx));

        {
            test::jtx::Env no_fully_canonical(
                *this,
                test::jtx::supported_amendments() -
                    featureRequireFullyCanonicalSig);

            Validity valid = checkValidity(
                                 no_fully_canonical.app().getHashRouter(),
                                 tx,
                                 no_fully_canonical.current()->rules(),
                                 no_fully_canonical.app().config())
                                 .first;

            if (valid != Validity::Valid)
                fail("Non-Fully canoncial signature was not permitted");
        }

        {
            test::jtx::Env fully_canonical(
                *this, test::jtx::supported_amendments());

            Validity valid = checkValidity(
                                 fully_canonical.app().getHashRouter(),
                                 tx,
                                 fully_canonical.current()->rules(),
                                 fully_canonical.app().config())
                                 .first;
            if (valid == Validity::Valid)
                fail("Non-Fully canoncial signature was permitted");
        }

        pass();
    }
};

BEAST_DEFINE_TESTSUITE(Apply, app, ripple);

}  // namespace ripple
