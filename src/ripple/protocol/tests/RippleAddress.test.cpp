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

#include <BeastConfig.h>
#include <ripple/protocol/digest.h>
#include <ripple/basics/TestSuite.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/protocol/RipplePublicKey.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/types.h>
#include <ripple/basics/StringUtilities.h>

namespace ripple {

class RippleAddress_test : public ripple::TestSuite
{
public:
    void run()
    {
        // Construct a seed.
        RippleAddress naSeed;

        expect (naSeed.setSeedGeneric ("masterpassphrase"));
        expect (naSeed.humanSeed () == "snoPBrXtMeMyMHUVTgbuqAfg1SUTb", naSeed.humanSeed ());

        // Create node public/private key pair
        RippleAddress naNodePublic    = RippleAddress::createNodePublic (naSeed);
        RippleAddress naNodePrivate   = RippleAddress::createNodePrivate (naSeed);

        expect (naNodePublic.humanNodePublic () == "n94a1u4jAz288pZLtw6yFWVbi89YamiC6JBXPVUj5zmExe5fTVg9", naNodePublic.humanNodePublic ());
        expect (naNodePrivate.humanNodePrivate () == "pnen77YEeUd4fFKG7iycBWcwKpTaeFRkW2WFostaATy1DSupwXe", naNodePrivate.humanNodePrivate ());

        // Check node signing.
        Blob vucTextSrc = strCopy ("Hello, nurse!");
        uint256 uHash   = sha512Half(makeSlice(vucTextSrc));
        Blob vucTextSig;

        naNodePrivate.signNodePrivate (uHash, vucTextSig);
        expect (naNodePublic.verifyNodePublic (uHash, vucTextSig, ECDSA::strict), "Verify failed.");

        // Construct a public generator from the seed.
        RippleAddress   generator     = RippleAddress::createGeneratorPublic (naSeed);

        expect (generator.humanGenerator () == "fhuJKrhSDzV2SkjLn9qbwm5AaRmrxDPfFsHDCP6yfDZWcxDFz4mt", generator.humanGenerator ());

        // Create ed25519 account public/private key pair.
        KeyPair keys = generateKeysFromSeed (KeyType::ed25519, naSeed);
        expectEquals (keys.publicKey.humanAccountPublic(), "aKGheSBjmCsKJVuLNKRAKpZXT6wpk2FCuEZAXJupXgdAxX5THCqR");

        // Check ed25519 account signing.
        vucTextSig = keys.secretKey.accountPrivateSign (vucTextSrc);

        expect (!vucTextSig.empty(), "ed25519 signing failed.");
        expect (keys.publicKey.accountPublicVerify (vucTextSrc, vucTextSig, ECDSA()), "ed25519 verify failed.");

        // Create account #0 public/private key pair.
        RippleAddress   naAccountPublic0    = RippleAddress::createAccountPublic (generator, 0);
        RippleAddress   naAccountPrivate0   = RippleAddress::createAccountPrivate (generator, naSeed, 0);

        expect (toBase58(calcAccountID(naAccountPublic0)) == "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh");
        expect (naAccountPublic0.humanAccountPublic () == "aBQG8RQAzjs1eTKFEAQXr2gS4utcDiEC9wmi7pfUPTi27VCahwgw", naAccountPublic0.humanAccountPublic ());

        // Create account #1 public/private key pair.
        RippleAddress   naAccountPublic1    = RippleAddress::createAccountPublic (generator, 1);
        RippleAddress   naAccountPrivate1   = RippleAddress::createAccountPrivate (generator, naSeed, 1);

        expect (toBase58(calcAccountID(naAccountPublic1)) == "r4bYF7SLUMD7QgSLLpgJx38WJSY12ViRjP");
        expect (naAccountPublic1.humanAccountPublic () == "aBPXpTfuLy1Bhk3HnGTTAqnovpKWQ23NpFMNkAF6F1Atg5vDyPrw", naAccountPublic1.humanAccountPublic ());

        // Check account signing.
        vucTextSig = naAccountPrivate0.accountPrivateSign (vucTextSrc);

        expect (!vucTextSig.empty(), "Signing failed.");
        expect (naAccountPublic0.accountPublicVerify (vucTextSrc, vucTextSig, ECDSA::strict), "Verify failed.");
        expect (!naAccountPublic1.accountPublicVerify (vucTextSrc, vucTextSig, ECDSA::not_strict), "Anti-verify failed.");
        expect (!naAccountPublic1.accountPublicVerify (vucTextSrc, vucTextSig, ECDSA::strict), "Anti-verify failed.");

        vucTextSig = naAccountPrivate1.accountPrivateSign (vucTextSrc);

        expect (!vucTextSig.empty(), "Signing failed.");
        expect (naAccountPublic1.accountPublicVerify (vucTextSrc, vucTextSig, ECDSA::strict), "Verify failed.");
        expect (!naAccountPublic0.accountPublicVerify (vucTextSrc, vucTextSig, ECDSA::not_strict), "Anti-verify failed.");
        expect (!naAccountPublic0.accountPublicVerify (vucTextSrc, vucTextSig, ECDSA::strict), "Anti-verify failed.");

        // Check account encryption.
        Blob vucTextCipher
            = naAccountPrivate0.accountPrivateEncrypt (naAccountPublic1, vucTextSrc);
        Blob vucTextRecovered
            = naAccountPrivate1.accountPrivateDecrypt (naAccountPublic0, vucTextCipher);

        expect (vucTextSrc == vucTextRecovered, "Encrypt-decrypt failed.");

        {
            RippleAddress nSeed;
            uint128 seed1, seed2;
            seed1.SetHex ("71ED064155FFADFA38782C5E0158CB26");
            nSeed.setSeed (seed1);
            expect (nSeed.humanSeed() == "shHM53KPZ87Gwdqarm1bAmPeXg8Tn",
                "Incorrect human seed");
            expect (nSeed.humanSeed1751() == "MAD BODY ACE MINT OKAY HUB WHAT DATA SACK FLAT DANA MATH",
                "Incorrect 1751 seed");
        }
    }
};

//------------------------------------------------------------------------------

class RippleIdentifier_test : public beast::unit_test::suite
{
public:
    void run ()
    {
        testcase ("Seed");
        RippleAddress seed;
        expect (seed.setSeedGeneric ("masterpassphrase"));
        expect (seed.humanSeed () == "snoPBrXtMeMyMHUVTgbuqAfg1SUTb", seed.humanSeed ());

        testcase ("RipplePublicKey");
        RippleAddress deprecatedPublicKey (RippleAddress::createNodePublic (seed));
        expect (deprecatedPublicKey.humanNodePublic () ==
            "n94a1u4jAz288pZLtw6yFWVbi89YamiC6JBXPVUj5zmExe5fTVg9",
                deprecatedPublicKey.humanNodePublic ());
        RipplePublicKey publicKey = deprecatedPublicKey.toPublicKey();
        expect (publicKey.to_string() == deprecatedPublicKey.humanNodePublic(),
            publicKey.to_string());

        testcase ("Generator");
        RippleAddress generator (RippleAddress::createGeneratorPublic (seed));
        expect (generator.humanGenerator () ==
            "fhuJKrhSDzV2SkjLn9qbwm5AaRmrxDPfFsHDCP6yfDZWcxDFz4mt",
                generator.humanGenerator ());
    }
};

BEAST_DEFINE_TESTSUITE(RippleAddress,ripple_data,ripple);
BEAST_DEFINE_TESTSUITE(RippleIdentifier,ripple_data,ripple);

} // ripple
