//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================
BOOST_AUTO_TEST_SUITE (ripple_address)

BOOST_AUTO_TEST_CASE ( check_crypto )
{
    // Construct a seed.
    RippleAddress   naSeed;

    BOOST_CHECK (naSeed.setSeedGeneric ("masterpassphrase"));
    BOOST_CHECK_MESSAGE (naSeed.humanSeed () == "snoPBrXtMeMyMHUVTgbuqAfg1SUTb", naSeed.humanSeed ());

    // Create node public/private key pair
    RippleAddress   naNodePublic    = RippleAddress::createNodePublic (naSeed);
    RippleAddress   naNodePrivate   = RippleAddress::createNodePrivate (naSeed);

    BOOST_CHECK_MESSAGE (naNodePublic.humanNodePublic () == "n94a1u4jAz288pZLtw6yFWVbi89YamiC6JBXPVUj5zmExe5fTVg9", naNodePublic.humanNodePublic ());
    BOOST_CHECK_MESSAGE (naNodePrivate.humanNodePrivate () == "pnen77YEeUd4fFKG7iycBWcwKpTaeFRkW2WFostaATy1DSupwXe", naNodePrivate.humanNodePrivate ());

    // Check node signing.
    Blob vucTextSrc = strCopy ("Hello, nurse!");
    uint256 uHash   = Serializer::getSHA512Half (vucTextSrc);
    Blob vucTextSig;

    naNodePrivate.signNodePrivate (uHash, vucTextSig);
    BOOST_CHECK_MESSAGE (naNodePublic.verifyNodePublic (uHash, vucTextSig), "Verify failed.");

    // Construct a public generator from the seed.
    RippleAddress   naGenerator     = RippleAddress::createGeneratorPublic (naSeed);

    BOOST_CHECK_MESSAGE (naGenerator.humanGenerator () == "fhuJKrhSDzV2SkjLn9qbwm5AaRmrxDPfFsHDCP6yfDZWcxDFz4mt", naGenerator.humanGenerator ());

    // Create account #0 public/private key pair.
    RippleAddress   naAccountPublic0    = RippleAddress::createAccountPublic (naGenerator, 0);
    RippleAddress   naAccountPrivate0   = RippleAddress::createAccountPrivate (naGenerator, naSeed, 0);

    BOOST_CHECK_MESSAGE (naAccountPublic0.humanAccountID () == "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", naAccountPublic0.humanAccountID ());
    BOOST_CHECK_MESSAGE (naAccountPublic0.humanAccountPublic () == "aBQG8RQAzjs1eTKFEAQXr2gS4utcDiEC9wmi7pfUPTi27VCahwgw", naAccountPublic0.humanAccountPublic ());
    BOOST_CHECK_MESSAGE (naAccountPrivate0.humanAccountPrivate () == "p9JfM6HHi64m6mvB6v5k7G2b1cXzGmYiCNJf6GHPKvFTWdeRVjh", naAccountPrivate0.humanAccountPrivate ());

    // Create account #1 public/private key pair.
    RippleAddress   naAccountPublic1    = RippleAddress::createAccountPublic (naGenerator, 1);
    RippleAddress   naAccountPrivate1   = RippleAddress::createAccountPrivate (naGenerator, naSeed, 1);

    BOOST_CHECK_MESSAGE (naAccountPublic1.humanAccountID () == "r4bYF7SLUMD7QgSLLpgJx38WJSY12ViRjP", naAccountPublic1.humanAccountID ());
    BOOST_CHECK_MESSAGE (naAccountPublic1.humanAccountPublic () == "aBPXpTfuLy1Bhk3HnGTTAqnovpKWQ23NpFMNkAF6F1Atg5vDyPrw", naAccountPublic1.humanAccountPublic ());
    BOOST_CHECK_MESSAGE (naAccountPrivate1.humanAccountPrivate () == "p9JEm822LMrzJii1k7TvdphfENTp6G5jr253Xa5rkzUWVr8ogQt", naAccountPrivate1.humanAccountPrivate ());

    // Check account signing.
    BOOST_CHECK_MESSAGE (naAccountPrivate0.accountPrivateSign (uHash, vucTextSig), "Signing failed.");
    BOOST_CHECK_MESSAGE (naAccountPublic0.accountPublicVerify (uHash, vucTextSig), "Verify failed.");
    BOOST_CHECK_MESSAGE (!naAccountPublic1.accountPublicVerify (uHash, vucTextSig), "Anti-verify failed.");

    BOOST_CHECK_MESSAGE (naAccountPrivate1.accountPrivateSign (uHash, vucTextSig), "Signing failed.");
    BOOST_CHECK_MESSAGE (naAccountPublic1.accountPublicVerify (uHash, vucTextSig), "Verify failed.");
    BOOST_CHECK_MESSAGE (!naAccountPublic0.accountPublicVerify (uHash, vucTextSig), "Anti-verify failed.");

    // Check account encryption.
    Blob vucTextCipher
        = naAccountPrivate0.accountPrivateEncrypt (naAccountPublic1, vucTextSrc);
    Blob vucTextRecovered
        = naAccountPrivate1.accountPrivateDecrypt (naAccountPublic0, vucTextCipher);

    BOOST_CHECK_MESSAGE (vucTextSrc == vucTextRecovered, "Encrypt-decrypt failed.");
}

BOOST_AUTO_TEST_SUITE_END ()
