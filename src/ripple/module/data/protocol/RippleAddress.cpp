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

#include <beast/unit_test/suite.h>

namespace ripple {

SETUP_LOG (RippleAddress)

RippleAddress::RippleAddress ()
    : mIsValid (false)
{
    nVersion = VER_NONE;
}

void RippleAddress::clear ()
{
    nVersion = VER_NONE;
    vchData.clear ();
    mIsValid = false;
}

bool RippleAddress::isSet () const
{
    return nVersion != VER_NONE;
}

std::string RippleAddress::humanAddressType () const
{
    switch (nVersion)
    {
    case VER_NONE:
        return "VER_NONE";

    case VER_NODE_PUBLIC:
        return "VER_NODE_PUBLIC";

    case VER_NODE_PRIVATE:
        return "VER_NODE_PRIVATE";

    case VER_ACCOUNT_ID:
        return "VER_ACCOUNT_ID";

    case VER_ACCOUNT_PUBLIC:
        return "VER_ACCOUNT_PUBLIC";

    case VER_ACCOUNT_PRIVATE:
        return "VER_ACCOUNT_PRIVATE";

    case VER_FAMILY_GENERATOR:
        return "VER_FAMILY_GENERATOR";

    case VER_FAMILY_SEED:
        return "VER_FAMILY_SEED";
    }

    return "unknown";
}

//
// NodePublic
//

RippleAddress RippleAddress::createNodePublic (const RippleAddress& naSeed)
{
    CKey            ckSeed (naSeed.getSeed ());
    RippleAddress   naNew;

    // YYY Should there be a GetPubKey() equiv that returns a uint256?
    naNew.setNodePublic (ckSeed.GetPubKey ());

    return naNew;
}

RippleAddress RippleAddress::createNodePublic (Blob const& vPublic)
{
    RippleAddress   naNew;

    naNew.setNodePublic (vPublic);

    return naNew;
}

RippleAddress RippleAddress::createNodePublic (const std::string& strPublic)
{
    RippleAddress   naNew;

    naNew.setNodePublic (strPublic);

    return naNew;
}

uint160 RippleAddress::getNodeID () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - getNodeID");

    case VER_NODE_PUBLIC:
        // Note, we are encoding the left.
        return Hash160 (vchData);

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}
Blob const& RippleAddress::getNodePublic () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - getNodePublic");

    case VER_NODE_PUBLIC:
        return vchData;

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}

std::string RippleAddress::humanNodePublic () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - humanNodePublic");

    case VER_NODE_PUBLIC:
        return ToString ();

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}

bool RippleAddress::setNodePublic (const std::string& strPublic)
{
    mIsValid = SetString (strPublic, VER_NODE_PUBLIC, Base58::getRippleAlphabet ());

    return mIsValid;
}

void RippleAddress::setNodePublic (Blob const& vPublic)
{
    mIsValid        = true;

    SetData (VER_NODE_PUBLIC, vPublic);
}

bool RippleAddress::verifyNodePublic (uint256 const& hash, Blob const& vchSig, ECDSA fullyCanonical) const
{
    CKey    pubkey  = CKey ();
    bool    bVerified;

    bVerified = isCanonicalECDSASig (vchSig, fullyCanonical);

    if (bVerified && !pubkey.SetPubKey (getNodePublic ()))
    {
        // Failed to set public key.
        bVerified   = false;
    }
    else
    {
        bVerified   = pubkey.Verify (hash, vchSig);
    }

    return bVerified;
}

bool RippleAddress::verifyNodePublic (uint256 const& hash, const std::string& strSig, ECDSA fullyCanonical) const
{
    Blob vchSig (strSig.begin (), strSig.end ());

    return verifyNodePublic (hash, vchSig, fullyCanonical);
}

//
// NodePrivate
//

RippleAddress RippleAddress::createNodePrivate (const RippleAddress& naSeed)
{
    uint256         uPrivKey;
    RippleAddress   naNew;
    CKey            ckSeed (naSeed.getSeed ());

    ckSeed.GetPrivateKeyU (uPrivKey);

    naNew.setNodePrivate (uPrivKey);

    return naNew;
}

Blob const& RippleAddress::getNodePrivateData () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - getNodePrivateData");

    case VER_NODE_PRIVATE:
        return vchData;

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}

uint256 RippleAddress::getNodePrivate () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source = getNodePrivate");

    case VER_NODE_PRIVATE:
        return uint256 (vchData);

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}

std::string RippleAddress::humanNodePrivate () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - humanNodePrivate");

    case VER_NODE_PRIVATE:
        return ToString ();

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}

bool RippleAddress::setNodePrivate (const std::string& strPrivate)
{
    mIsValid = SetString (strPrivate, VER_NODE_PRIVATE, Base58::getRippleAlphabet ());

    return mIsValid;
}

void RippleAddress::setNodePrivate (Blob const& vPrivate)
{
    mIsValid = true;

    SetData (VER_NODE_PRIVATE, vPrivate);
}

void RippleAddress::setNodePrivate (uint256 hash256)
{
    mIsValid = true;

    SetData (VER_NODE_PRIVATE, hash256);
}

void RippleAddress::signNodePrivate (uint256 const& hash, Blob& vchSig) const
{
    CKey    ckPrivKey;

    ckPrivKey.SetPrivateKeyU (getNodePrivate ());

    if (!ckPrivKey.Sign (hash, vchSig))
        throw std::runtime_error ("Signing failed.");
}

//
// AccountID
//

uint160 RippleAddress::getAccountID () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - getAccountID");

    case VER_ACCOUNT_ID:
        return uint160 (vchData);

    case VER_ACCOUNT_PUBLIC:
        // Note, we are encoding the left.
        return Hash160 (vchData);

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}

typedef RippleMutex StaticLockType;
typedef std::lock_guard <StaticLockType> StaticScopedLockType;
static StaticLockType s_lock;

static ripple::unordered_map< Blob , std::string > rncMap;

std::string RippleAddress::humanAccountID () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - humanAccountID");

    case VER_ACCOUNT_ID:
    {
        StaticScopedLockType sl (s_lock);

        auto it = rncMap.find (vchData);

        if (it != rncMap.end ())
            return it->second;

        // VFALCO NOTE Why do we throw everything out? We could keep two maps
        //             here, switch back and forth keep one of them full and clear the
        //             other on a swap - but always check both maps for cache hits.
        //
        if (rncMap.size () > 250000)
            rncMap.clear ();

        return rncMap[vchData] = ToString ();
    }

    case VER_ACCOUNT_PUBLIC:
    {
        RippleAddress   accountID;

        (void) accountID.setAccountID (getAccountID ());

        return accountID.ToString ();
    }

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}

bool RippleAddress::setAccountID (const std::string& strAccountID, Base58::Alphabet const& alphabet)
{
    if (strAccountID.empty ())
    {
        setAccountID (uint160 ());

        mIsValid    = true;
    }
    else
    {
        mIsValid = SetString (strAccountID, VER_ACCOUNT_ID, alphabet);
    }

    return mIsValid;
}

void RippleAddress::setAccountID (const uint160& hash160)
{
    mIsValid        = true;

    SetData (VER_ACCOUNT_ID, hash160);
}

//
// AccountPublic
//

RippleAddress RippleAddress::createAccountPublic (const RippleAddress& naGenerator, int iSeq)
{
    CKey            ckPub (naGenerator, iSeq);
    RippleAddress   naNew;

    naNew.setAccountPublic (ckPub.GetPubKey ());

    return naNew;
}

Blob const& RippleAddress::getAccountPublic () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - getAccountPublic");

    case VER_ACCOUNT_ID:
        throw std::runtime_error ("public not available from account id");
        break;

    case VER_ACCOUNT_PUBLIC:
        return vchData;

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}

std::string RippleAddress::humanAccountPublic () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - humanAccountPublic");

    case VER_ACCOUNT_ID:
        throw std::runtime_error ("public not available from account id");

    case VER_ACCOUNT_PUBLIC:
        return ToString ();

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}

bool RippleAddress::setAccountPublic (const std::string& strPublic)
{
    mIsValid = SetString (strPublic, VER_ACCOUNT_PUBLIC, Base58::getRippleAlphabet ());

    return mIsValid;
}

void RippleAddress::setAccountPublic (Blob const& vPublic)
{
    mIsValid = true;

    SetData (VER_ACCOUNT_PUBLIC, vPublic);
}

void RippleAddress::setAccountPublic (const RippleAddress& generator, int seq)
{
    CKey    pubkey  = CKey (generator, seq);

    setAccountPublic (pubkey.GetPubKey ());
}

bool RippleAddress::accountPublicVerify (uint256 const& uHash, Blob const& vucSig, ECDSA fullyCanonical) const
{
    CKey        ckPublic;

    bool        bVerified = isCanonicalECDSASig (vucSig, fullyCanonical);

    if (bVerified && !ckPublic.SetPubKey (getAccountPublic ()))
    {
        // Bad private key.
        WriteLog (lsWARNING, RippleAddress) << "accountPublicVerify: Bad private key.";
        bVerified   = false;
    }
    else
    {
        bVerified   = ckPublic.Verify (uHash, vucSig);
    }

    return bVerified;
}

RippleAddress RippleAddress::createAccountID (const uint160& uiAccountID)
{
    RippleAddress   na;

    na.setAccountID (uiAccountID);

    return na;
}

//
// AccountPrivate
//

RippleAddress RippleAddress::createAccountPrivate (const RippleAddress& naGenerator, const RippleAddress& naSeed, int iSeq)
{
    RippleAddress   naNew;

    naNew.setAccountPrivate (naGenerator, naSeed, iSeq);

    return naNew;
}

uint256 RippleAddress::getAccountPrivate () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - getAccountPrivate");

    case VER_ACCOUNT_PRIVATE:
        return uint256 (vchData);

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}

std::string RippleAddress::humanAccountPrivate () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - humanAccountPrivate");

    case VER_ACCOUNT_PRIVATE:
        return ToString ();

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}

bool RippleAddress::setAccountPrivate (const std::string& strPrivate)
{
    mIsValid = SetString (strPrivate, VER_ACCOUNT_PRIVATE, Base58::getRippleAlphabet ());

    return mIsValid;
}

void RippleAddress::setAccountPrivate (Blob const& vPrivate)
{
    mIsValid        = true;

    SetData (VER_ACCOUNT_PRIVATE, vPrivate);
}

void RippleAddress::setAccountPrivate (uint256 hash256)
{
    mIsValid = true;

    SetData (VER_ACCOUNT_PRIVATE, hash256);
}

void RippleAddress::setAccountPrivate (const RippleAddress& naGenerator, const RippleAddress& naSeed, int seq)
{
    CKey    ckPubkey    = CKey (naSeed.getSeed ());
    CKey    ckPrivkey   = CKey (naGenerator, ckPubkey.GetSecretBN (), seq);
    uint256 uPrivKey;

    ckPrivkey.GetPrivateKeyU (uPrivKey);

    setAccountPrivate (uPrivKey);
}

bool RippleAddress::accountPrivateSign (uint256 const& uHash, Blob& vucSig) const
{
    CKey        ckPrivate;
    bool        bResult;

    if (!ckPrivate.SetPrivateKeyU (getAccountPrivate ()))
    {
        // Bad private key.
        WriteLog (lsWARNING, RippleAddress) << "accountPrivateSign: Bad private key.";
        bResult = false;
    }
    else
    {
        bResult = ckPrivate.Sign (uHash, vucSig);
        CondLog (!bResult, lsWARNING, RippleAddress) << "accountPrivateSign: Signing failed.";
    }

    return bResult;
}

Blob RippleAddress::accountPrivateEncrypt (const RippleAddress& naPublicTo, Blob const& vucPlainText) const
{
    CKey                        ckPrivate;
    CKey                        ckPublic;
    Blob    vucCipherText;

    if (!ckPublic.SetPubKey (naPublicTo.getAccountPublic ()))
    {
        // Bad public key.
        WriteLog (lsWARNING, RippleAddress) << "accountPrivateEncrypt: Bad public key.";
    }
    else if (!ckPrivate.SetPrivateKeyU (getAccountPrivate ()))
    {
        // Bad private key.
        WriteLog (lsWARNING, RippleAddress) << "accountPrivateEncrypt: Bad private key.";
    }
    else
    {
        try
        {
            vucCipherText = ckPrivate.encryptECIES (ckPublic, vucPlainText);
        }
        catch (...)
        {
        }
    }

    return vucCipherText;
}

Blob RippleAddress::accountPrivateDecrypt (const RippleAddress& naPublicFrom, Blob const& vucCipherText) const
{
    CKey                        ckPrivate;
    CKey                        ckPublic;
    Blob    vucPlainText;

    if (!ckPublic.SetPubKey (naPublicFrom.getAccountPublic ()))
    {
        // Bad public key.
        WriteLog (lsWARNING, RippleAddress) << "accountPrivateDecrypt: Bad public key.";
    }
    else if (!ckPrivate.SetPrivateKeyU (getAccountPrivate ()))
    {
        // Bad private key.
        WriteLog (lsWARNING, RippleAddress) << "accountPrivateDecrypt: Bad private key.";
    }
    else
    {
        try
        {
            vucPlainText = ckPrivate.decryptECIES (ckPublic, vucCipherText);
        }
        catch (...)
        {
        }
    }

    return vucPlainText;
}

//
// Generators
//

Blob const& RippleAddress::getGenerator () const
{
    // returns the public generator
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - getGenerator");

    case VER_FAMILY_GENERATOR:
        // Do nothing.
        return vchData;

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}

std::string RippleAddress::humanGenerator () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - humanGenerator");

    case VER_FAMILY_GENERATOR:
        return ToString ();

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}

bool RippleAddress::setGenerator (const std::string& strGenerator)
{
    mIsValid = SetString (strGenerator, VER_FAMILY_GENERATOR, Base58::getRippleAlphabet ());

    return mIsValid;
}

void RippleAddress::setGenerator (Blob const& vPublic)
{
    mIsValid        = true;

    SetData (VER_FAMILY_GENERATOR, vPublic);
}

RippleAddress RippleAddress::createGeneratorPublic (const RippleAddress& naSeed)
{
    CKey            ckSeed (naSeed.getSeed ());
    RippleAddress   naNew;

    naNew.setGenerator (ckSeed.GetPubKey ());

    return naNew;
}

//
// Seed
//

uint128 RippleAddress::getSeed () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - getSeed");

    case VER_FAMILY_SEED:
        return uint128 (vchData);

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}

std::string RippleAddress::humanSeed1751 () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - humanSeed1751");

    case VER_FAMILY_SEED:
    {
        std::string strHuman;
        std::string strLittle;
        std::string strBig;
        uint128 uSeed   = getSeed ();

        strLittle.assign (uSeed.begin (), uSeed.end ());

        strBig.assign (strLittle.rbegin (), strLittle.rend ());

        RFC1751::getEnglishFromKey (strHuman, strBig);

        return strHuman;
    }

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}

std::string RippleAddress::humanSeed () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - humanSeed");

    case VER_FAMILY_SEED:
        return ToString ();

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}

int RippleAddress::setSeed1751 (const std::string& strHuman1751)
{
    std::string strKey;
    int         iResult = RFC1751::getKeyFromEnglish (strKey, strHuman1751);

    if (1 == iResult)
    {
        Blob    vchLittle (strKey.rbegin (), strKey.rend ());
        uint128     uSeed (vchLittle);

        setSeed (uSeed);
    }

    return iResult;
}

bool RippleAddress::setSeed (const std::string& strSeed)
{
    mIsValid = SetString (strSeed, VER_FAMILY_SEED, Base58::getRippleAlphabet ());

    return mIsValid;
}

bool RippleAddress::setSeedGeneric (const std::string& strText)
{
    RippleAddress   naTemp;
    bool            bResult = true;
    uint128         uSeed;

    if (strText.empty ()
            || naTemp.setAccountID (strText)
            || naTemp.setAccountPublic (strText)
            || naTemp.setAccountPrivate (strText)
            || naTemp.setNodePublic (strText)
            || naTemp.setNodePrivate (strText))
    {
        bResult = false;
    }
    else if (strText.length () == 32 && uSeed.SetHex (strText, true))
    {
        setSeed (uSeed);
    }
    else if (setSeed (strText))
    {
        // Log::out() << "Recognized seed.";
    }
    else if (1 == setSeed1751 (strText))
    {
        // Log::out() << "Recognized 1751 seed.";
    }
    else
    {
        // Log::out() << "Creating seed from pass phrase.";
        setSeed (CKey::PassPhraseToKey (strText));
    }

    return bResult;
}

void RippleAddress::setSeed (uint128 hash128)
{
    mIsValid = true;

    SetData (VER_FAMILY_SEED, hash128);
}

void RippleAddress::setSeedRandom ()
{
    // XXX Maybe we should call MakeNewKey
    uint128 key;

    RandomNumbers::getInstance ().fillBytes (key.begin (), key.size ());

    RippleAddress::setSeed (key);
}

RippleAddress RippleAddress::createSeedRandom ()
{
    RippleAddress   naNew;

    naNew.setSeedRandom ();

    return naNew;
}

RippleAddress RippleAddress::createSeedGeneric (const std::string& strText)
{
    RippleAddress   naNew;

    naNew.setSeedGeneric (strText);

    return naNew;
}

//------------------------------------------------------------------------------

class RippleAddress_test : public beast::unit_test::suite
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
        uint256 uHash   = Serializer::getSHA512Half (vucTextSrc);
        Blob vucTextSig;

        naNodePrivate.signNodePrivate (uHash, vucTextSig);
        expect (naNodePublic.verifyNodePublic (uHash, vucTextSig, ECDSA::strict), "Verify failed.");

        // Construct a public generator from the seed.
        RippleAddress   naGenerator     = RippleAddress::createGeneratorPublic (naSeed);

        expect (naGenerator.humanGenerator () == "fhuJKrhSDzV2SkjLn9qbwm5AaRmrxDPfFsHDCP6yfDZWcxDFz4mt", naGenerator.humanGenerator ());

        // Create account #0 public/private key pair.
        RippleAddress   naAccountPublic0    = RippleAddress::createAccountPublic (naGenerator, 0);
        RippleAddress   naAccountPrivate0   = RippleAddress::createAccountPrivate (naGenerator, naSeed, 0);

        expect (naAccountPublic0.humanAccountID () == "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", naAccountPublic0.humanAccountID ());
        expect (naAccountPublic0.humanAccountPublic () == "aBQG8RQAzjs1eTKFEAQXr2gS4utcDiEC9wmi7pfUPTi27VCahwgw", naAccountPublic0.humanAccountPublic ());
        expect (naAccountPrivate0.humanAccountPrivate () == "p9JfM6HHi64m6mvB6v5k7G2b1cXzGmYiCNJf6GHPKvFTWdeRVjh", naAccountPrivate0.humanAccountPrivate ());

        // Create account #1 public/private key pair.
        RippleAddress   naAccountPublic1    = RippleAddress::createAccountPublic (naGenerator, 1);
        RippleAddress   naAccountPrivate1   = RippleAddress::createAccountPrivate (naGenerator, naSeed, 1);

        expect (naAccountPublic1.humanAccountID () == "r4bYF7SLUMD7QgSLLpgJx38WJSY12ViRjP", naAccountPublic1.humanAccountID ());
        expect (naAccountPublic1.humanAccountPublic () == "aBPXpTfuLy1Bhk3HnGTTAqnovpKWQ23NpFMNkAF6F1Atg5vDyPrw", naAccountPublic1.humanAccountPublic ());
        expect (naAccountPrivate1.humanAccountPrivate () == "p9JEm822LMrzJii1k7TvdphfENTp6G5jr253Xa5rkzUWVr8ogQt", naAccountPrivate1.humanAccountPrivate ());

        // Check account signing.
        expect (naAccountPrivate0.accountPrivateSign (uHash, vucTextSig), "Signing failed.");
        expect (naAccountPublic0.accountPublicVerify (uHash, vucTextSig, ECDSA::strict), "Verify failed.");
        expect (!naAccountPublic1.accountPublicVerify (uHash, vucTextSig, ECDSA::not_strict), "Anti-verify failed.");
        expect (!naAccountPublic1.accountPublicVerify (uHash, vucTextSig, ECDSA::strict), "Anti-verify failed.");

        expect (naAccountPrivate1.accountPrivateSign (uHash, vucTextSig), "Signing failed.");
        expect (naAccountPublic1.accountPublicVerify (uHash, vucTextSig, ECDSA::strict), "Verify failed.");
        expect (!naAccountPublic0.accountPublicVerify (uHash, vucTextSig, ECDSA::not_strict), "Anti-verify failed.");
        expect (!naAccountPublic0.accountPublicVerify (uHash, vucTextSig, ECDSA::strict), "Anti-verify failed.");

        // Check account encryption.
        Blob vucTextCipher
            = naAccountPrivate0.accountPrivateEncrypt (naAccountPublic1, vucTextSrc);
        Blob vucTextRecovered
            = naAccountPrivate1.accountPrivateDecrypt (naAccountPublic0, vucTextCipher);

        expect (vucTextSrc == vucTextRecovered, "Encrypt-decrypt failed.");
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
        RipplePublicKey publicKey (deprecatedPublicKey);
        expect (publicKey.to_string() == deprecatedPublicKey.humanNodePublic(),
            publicKey.to_string());

        testcase ("RipplePrivateKey");
        RippleAddress deprecatedPrivateKey (RippleAddress::createNodePrivate (seed));
        expect (deprecatedPrivateKey.humanNodePrivate () ==
            "pnen77YEeUd4fFKG7iycBWcwKpTaeFRkW2WFostaATy1DSupwXe",
                deprecatedPrivateKey.humanNodePrivate ());
        RipplePrivateKey privateKey (deprecatedPrivateKey);
        expect (privateKey.to_string() == deprecatedPrivateKey.humanNodePrivate(),
            privateKey.to_string());

        testcase ("Generator");
        RippleAddress generator (RippleAddress::createGeneratorPublic (seed));
        expect (generator.humanGenerator () ==
            "fhuJKrhSDzV2SkjLn9qbwm5AaRmrxDPfFsHDCP6yfDZWcxDFz4mt",
                generator.humanGenerator ());

        testcase ("RippleAccountID");
        RippleAddress deprecatedAccountPublicKey (
            RippleAddress::createAccountPublic (generator, 0));
        expect (deprecatedAccountPublicKey.humanAccountID () ==
            "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
                deprecatedAccountPublicKey.humanAccountID ());
        RippleAccountID accountID (deprecatedAccountPublicKey);
        expect (accountID.to_string() ==
            deprecatedAccountPublicKey.humanAccountID(),
                accountID.to_string());

        testcase ("RippleAccountPublicKey");
        expect (deprecatedAccountPublicKey.humanAccountPublic () ==
            "aBQG8RQAzjs1eTKFEAQXr2gS4utcDiEC9wmi7pfUPTi27VCahwgw",
                deprecatedAccountPublicKey.humanAccountPublic ());

        testcase ("RippleAccountPrivateKey");
        RippleAddress deprecatedAccountPrivateKey (
            RippleAddress::createAccountPrivate (generator, seed, 0));
        expect (deprecatedAccountPrivateKey.humanAccountPrivate () ==
            "p9JfM6HHi64m6mvB6v5k7G2b1cXzGmYiCNJf6GHPKvFTWdeRVjh",
                deprecatedAccountPrivateKey.humanAccountPrivate ());
        RippleAccountPrivateKey accountPrivateKey (deprecatedAccountPrivateKey);
        expect (accountPrivateKey.to_string() ==
            deprecatedAccountPrivateKey.humanAccountPrivate(),
                privateKey.to_string());
    }
};

BEAST_DEFINE_TESTSUITE(RippleAddress,ripple_data,ripple);
BEAST_DEFINE_TESTSUITE(RippleIdentifier,ripple_data,ripple);

} // ripple
