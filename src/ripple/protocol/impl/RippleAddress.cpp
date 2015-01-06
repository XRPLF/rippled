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
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/crypto/ECDSA.h>
#include <ripple/crypto/ECIES.h>
#include <ripple/crypto/GenerateDeterministicKey.h>
#include <ripple/crypto/RandomNumbers.h>
#include <ripple/crypto/RFC1751.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/RipplePublicKey.h>
#include <beast/unit_test/suite.h>
#include <openssl/ripemd.h>
#include <openssl/bn.h>
#include <openssl/pem.h>
#include <mutex>

namespace ripple {

static BIGNUM* GetSecretBN (const openssl::ec_key& keypair)
{
    // DEPRECATED
    return BN_dup (EC_KEY_get0_private_key ((EC_KEY*) keypair.get()));
}

// <-- seed
static uint128 PassPhraseToKey (std::string const& passPhrase)
{
    Serializer s;

    s.addRaw (passPhrase);
    // NIKB TODO this caling sequence is a bit ugly; this should be improved.
    uint256 hash256 = s.getSHA512Half ();
    uint128 ret (uint128::fromVoid (hash256.data()));

    s.secureErase ();

    return ret;
}

static Blob getPublicKey (openssl::ec_key const& key)
{
    Blob result (33);

    key.get_public_key (&result[0]);

    return result;
}

static bool verifySignature (Blob const& pubkey, uint256 const& hash, Blob const& sig, ECDSA fullyCanonical)
{
    if (! isCanonicalECDSASig (sig, fullyCanonical))
    {
        return false;
    }

    openssl::ec_key key = ECDSAPublicKey (pubkey);

    return key.valid() && ECDSAVerify (hash, sig, key);
}

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

//
// NodePublic
//

static uint160 Hash160 (Blob const& vch)
{
    uint256 hash1;
    SHA256 (vch.data (), vch.size (), hash1.data ());

    uint160 hash2;
    RIPEMD160 (hash1.data (), hash1.size (), hash2.data ());

    return hash2;
}

RippleAddress RippleAddress::createNodePublic (RippleAddress const& naSeed)
{
    RippleAddress   naNew;

    // YYY Should there be a GetPubKey() equiv that returns a uint256?
    naNew.setNodePublic (getPublicKey (GenerateRootDeterministicKey (naSeed.getSeed())));

    return naNew;
}

RippleAddress RippleAddress::createNodePublic (Blob const& vPublic)
{
    RippleAddress   naNew;

    naNew.setNodePublic (vPublic);

    return naNew;
}

RippleAddress RippleAddress::createNodePublic (std::string const& strPublic)
{
    RippleAddress   naNew;

    naNew.setNodePublic (strPublic);

    return naNew;
}

RipplePublicKey
RippleAddress::toPublicKey() const
{
    assert (nVersion == VER_NODE_PUBLIC);
    return RipplePublicKey (vchData.begin(), vchData.end());
}

NodeID RippleAddress::getNodeID () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - getNodeID");

    case VER_NODE_PUBLIC: {
        // Note, we are encoding the left.
        NodeID node;
        node.copyFrom(Hash160 (vchData));
        return node;
    }

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

bool RippleAddress::setNodePublic (std::string const& strPublic)
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
    return verifySignature (getNodePublic(), hash, vchSig, fullyCanonical);
}

bool RippleAddress::verifyNodePublic (uint256 const& hash, std::string const& strSig, ECDSA fullyCanonical) const
{
    Blob vchSig (strSig.begin (), strSig.end ());

    return verifyNodePublic (hash, vchSig, fullyCanonical);
}

//
// NodePrivate
//

RippleAddress RippleAddress::createNodePrivate (RippleAddress const& naSeed)
{
    RippleAddress   naNew;

    naNew.setNodePrivate (GenerateRootDeterministicKey (naSeed.getSeed()).get_private_key());

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

bool RippleAddress::setNodePrivate (std::string const& strPrivate)
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
    openssl::ec_key key = ECDSAPrivateKey (getNodePrivate());

    vchSig = ECDSASign (hash, key);

    if (vchSig.empty())
        throw std::runtime_error ("Signing failed.");
}

//
// AccountID
//

Account RippleAddress::getAccountID () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - getAccountID");

    case VER_ACCOUNT_ID:
        return Account(vchData);

    case VER_ACCOUNT_PUBLIC: {
        // Note, we are encoding the left.
        // TODO(tom): decipher this comment.
        Account account;
        account.copyFrom (Hash160 (vchData));
        return account;
    }

    default:
        throw std::runtime_error (str (boost::format ("bad source: %d") % int (nVersion)));
    }
}

typedef std::mutex StaticLockType;
typedef std::lock_guard <StaticLockType> StaticScopedLockType;

static StaticLockType s_lock;
static hash_map <Blob, std::string> rncMapOld, rncMapNew;

void RippleAddress::clearCache ()
{
    StaticScopedLockType sl (s_lock);

    rncMapOld.clear ();
    rncMapNew.clear ();
}

std::string RippleAddress::humanAccountID () const
{
    switch (nVersion)
    {
    case VER_NONE:
        throw std::runtime_error ("unset source - humanAccountID");

    case VER_ACCOUNT_ID:
    {
        std::string ret;

        {
            StaticScopedLockType sl (s_lock);

            auto it = rncMapNew.find (vchData);

            if (it != rncMapNew.end ())
            {
                // Found in new map, nothing to do
                ret = it->second;
            }
            else
            {
                it = rncMapOld.find (vchData);

                if (it != rncMapOld.end ())
                {
                    ret = it->second;
                    rncMapOld.erase (it);
                }
                else
                    ret = ToString ();

                if (rncMapNew.size () >= 128000)
                {
                    rncMapOld = std::move (rncMapNew);
                    rncMapNew.clear ();
                    rncMapNew.reserve (128000);
                }

                rncMapNew[vchData] = ret;
            }
        }

        return ret;
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

bool RippleAddress::setAccountID (
    std::string const& strAccountID, Base58::Alphabet const& alphabet)
{
    if (strAccountID.empty ())
    {
        setAccountID (Account ());

        mIsValid    = true;
    }
    else
    {
        mIsValid = SetString (strAccountID, VER_ACCOUNT_ID, alphabet);
    }

    return mIsValid;
}

void RippleAddress::setAccountID (Account const& hash160)
{
    mIsValid        = true;
    SetData (VER_ACCOUNT_ID, hash160);
}

//
// AccountPublic
//

RippleAddress RippleAddress::createAccountPublic (
    RippleAddress const& generator, int iSeq)
{
    RippleAddress   naNew;

    naNew.setAccountPublic (generator, iSeq);

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

bool RippleAddress::setAccountPublic (std::string const& strPublic)
{
    mIsValid = SetString (strPublic, VER_ACCOUNT_PUBLIC, Base58::getRippleAlphabet ());

    return mIsValid;
}

void RippleAddress::setAccountPublic (Blob const& vPublic)
{
    mIsValid = true;

    SetData (VER_ACCOUNT_PUBLIC, vPublic);
}

void RippleAddress::setAccountPublic (RippleAddress const& generator, int seq)
{
    setAccountPublic (getPublicKey (GeneratePublicDeterministicKey (generator.getGenerator(), seq)));
}

bool RippleAddress::accountPublicVerify (
    uint256 const& uHash, Blob const& vucSig, ECDSA fullyCanonical) const
{
    return verifySignature (getAccountPublic(), uHash, vucSig, fullyCanonical);
}

RippleAddress RippleAddress::createAccountID (Account const& account)
{
    RippleAddress   na;
    na.setAccountID (account);

    return na;
}

//
// AccountPrivate
//

RippleAddress RippleAddress::createAccountPrivate (
    RippleAddress const& generator, RippleAddress const& naSeed, int iSeq)
{
    RippleAddress   naNew;

    naNew.setAccountPrivate (generator, naSeed, iSeq);

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
        throw std::runtime_error ("bad source: " + std::to_string(nVersion));
    }
}

bool RippleAddress::setAccountPrivate (std::string const& strPrivate)
{
    mIsValid = SetString (
        strPrivate, VER_ACCOUNT_PRIVATE, Base58::getRippleAlphabet ());

    return mIsValid;
}

void RippleAddress::setAccountPrivate (Blob const& vPrivate)
{
    mIsValid = true;
    SetData (VER_ACCOUNT_PRIVATE, vPrivate);
}

void RippleAddress::setAccountPrivate (uint256 hash256)
{
    mIsValid = true;
    SetData (VER_ACCOUNT_PRIVATE, hash256);
}

void RippleAddress::setAccountPrivate (
    RippleAddress const& generator, RippleAddress const& naSeed, int seq)
{
    openssl::ec_key publicKey = GenerateRootDeterministicKey (naSeed.getSeed());
    openssl::ec_key secretKey = GeneratePrivateDeterministicKey (generator.getGenerator(), GetSecretBN (publicKey), seq);

    setAccountPrivate (secretKey.get_private_key());
}

bool RippleAddress::accountPrivateSign (uint256 const& uHash, Blob& vucSig) const
{
    openssl::ec_key key = ECDSAPrivateKey (getAccountPrivate());

    if (!key.valid())
    {
        // Bad private key.
        WriteLog (lsWARNING, RippleAddress)
                << "accountPrivateSign: Bad private key.";

        return false;
    }

    vucSig = ECDSASign (uHash, key);
    const bool ok = !vucSig.empty();

    CondLog (!ok, lsWARNING, RippleAddress)
            << "accountPrivateSign: Signing failed.";

    return ok;
}

Blob RippleAddress::accountPrivateEncrypt (
    RippleAddress const& naPublicTo, Blob const& vucPlainText) const
{
    openssl::ec_key secretKey = ECDSAPrivateKey (getAccountPrivate());
    openssl::ec_key publicKey = ECDSAPublicKey (naPublicTo.getAccountPublic());

    Blob vucCipherText;

    if (! publicKey.valid())
    {
        WriteLog (lsWARNING, RippleAddress)
                << "accountPrivateEncrypt: Bad public key.";
    }

    if (! secretKey.valid())
    {
        WriteLog (lsWARNING, RippleAddress)
                << "accountPrivateEncrypt: Bad private key.";
    }

    {
        try
        {
            vucCipherText = encryptECIES (secretKey, publicKey, vucPlainText);
        }
        catch (...)
        {
        }
    }

    return vucCipherText;
}

Blob RippleAddress::accountPrivateDecrypt (
    RippleAddress const& naPublicFrom, Blob const& vucCipherText) const
{
    openssl::ec_key secretKey = ECDSAPrivateKey (getAccountPrivate());
    openssl::ec_key publicKey = ECDSAPublicKey (naPublicFrom.getAccountPublic());

    Blob    vucPlainText;

    if (! publicKey.valid())
    {
        WriteLog (lsWARNING, RippleAddress)
                << "accountPrivateDecrypt: Bad public key.";
    }

    if (! secretKey.valid())
    {
        WriteLog (lsWARNING, RippleAddress)
                << "accountPrivateDecrypt: Bad private key.";
    }

    {
        try
        {
            vucPlainText = decryptECIES (secretKey, publicKey, vucCipherText);
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
        throw std::runtime_error ("bad source: " + std::to_string(nVersion));
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
        throw std::runtime_error ("bad source: " + std::to_string(nVersion));
    }
}

void RippleAddress::setGenerator (Blob const& vPublic)
{
    mIsValid        = true;
    SetData (VER_FAMILY_GENERATOR, vPublic);
}

RippleAddress RippleAddress::createGeneratorPublic (RippleAddress const& naSeed)
{
    RippleAddress   naNew;
    naNew.setGenerator (getPublicKey (GenerateRootDeterministicKey (naSeed.getSeed())));
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
        throw std::runtime_error ("bad source: " + std::to_string(nVersion));
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

int RippleAddress::setSeed1751 (std::string const& strHuman1751)
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

bool RippleAddress::setSeed (std::string const& strSeed)
{
    mIsValid = SetString (strSeed, VER_FAMILY_SEED, Base58::getRippleAlphabet ());

    return mIsValid;
}

bool RippleAddress::setSeedGeneric (std::string const& strText)
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
        setSeed (PassPhraseToKey (strText));
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

    random_fill (key.begin (), key.size ());

    RippleAddress::setSeed (key);
}

RippleAddress RippleAddress::createSeedRandom ()
{
    RippleAddress   naNew;

    naNew.setSeedRandom ();

    return naNew;
}

RippleAddress RippleAddress::createSeedGeneric (std::string const& strText)
{
    RippleAddress   naNew;

    naNew.setSeedGeneric (strText);

    return naNew;
}

} // ripple
