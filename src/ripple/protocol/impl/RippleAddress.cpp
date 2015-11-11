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
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/crypto/ECDSA.h>
#include <ripple/crypto/ECIES.h>
#include <ripple/crypto/GenerateDeterministicKey.h>
#include <ripple/crypto/RandomNumbers.h>
#include <ripple/crypto/RFC1751.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/RipplePublicKey.h>
#include <ripple/protocol/types.h>
#include <beast/unit_test/suite.h>
#include <ed25519-donna/ed25519.h>
#include <openssl/ripemd.h>
#include <openssl/pem.h>
#include <algorithm>
#include <mutex>

namespace ripple {

static
bool isCanonicalEd25519Signature (std::uint8_t const* signature)
{
    using std::uint8_t;

    // Big-endian `l`, the Ed25519 subgroup order
    char const* const order = "\x10\x00\x00\x00\x00\x00\x00\x00"
                              "\x00\x00\x00\x00\x00\x00\x00\x00"
                              "\x14\xDE\xF9\xDE\xA2\xF7\x9C\xD6"
                              "\x58\x12\x63\x1A\x5C\xF5\xD3\xED";

    uint8_t const* const l = reinterpret_cast<uint8_t const*> (order);

    // Take the second half of signature and byte-reverse it to big-endian.
    uint8_t const* S_le = signature + 32;
    uint8_t S[32];
    std::reverse_copy (S_le, S_le + 32, S);

    return std::lexicographical_compare (S, S + 32, l, l + 32);
}

// <-- seed
static
uint128 PassPhraseToKey (std::string const& passPhrase)
{
    return uint128::fromVoid(sha512Half_s(
        makeSlice(passPhrase)).data());
}

static
bool verifySignature (Blob const& pubkey, uint256 const& hash, Blob const& sig,
                      ECDSA fullyCanonical)
{
    if (! isCanonicalECDSASig (sig, fullyCanonical))
    {
        return false;
    }

    return ECDSAVerify (hash, sig, &pubkey[0], pubkey.size());
}

RippleAddress::RippleAddress ()
    : mIsValid (false)
{
    nVersion = TOKEN_NONE;
}

void RippleAddress::clear ()
{
    nVersion = TOKEN_NONE;
    vchData.clear ();
    mIsValid = false;
}

bool RippleAddress::isSet () const
{
    return nVersion != TOKEN_NONE;
}

//
// NodePublic
//

static
uint160 Hash160 (Blob const& vch)
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
    naNew.setNodePublic (generateRootDeterministicPublicKey (naSeed.getSeed()));

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
    assert (nVersion == TOKEN_NODE_PUBLIC);
    return RipplePublicKey (vchData.begin(), vchData.end());
}

NodeID RippleAddress::getNodeID () const
{
    switch (nVersion)
    {
    case TOKEN_NONE:
        Throw<std::runtime_error> ("unset source - getNodeID");

    case TOKEN_NODE_PUBLIC:
    {
        // Note, we are encoding the left.
        NodeID node;
        node.copyFrom(Hash160 (vchData));
        return node;
    }

    default:
        Throw<std::runtime_error> ("bad source: " + std::to_string(nVersion));
    }
    return {}; // Silence compiler warning.
}

Blob const& RippleAddress::getNodePublic () const
{
    switch (nVersion)
    {
    case TOKEN_NONE:
        Throw<std::runtime_error> ("unset source - getNodePublic");

    case TOKEN_NODE_PUBLIC:
        return vchData;

    default:
        Throw<std::runtime_error>("bad source: " + std::to_string(nVersion));
    }
    return vchData; // Silence compiler warning.
}

std::string RippleAddress::humanNodePublic () const
{
    switch (nVersion)
    {
    case TOKEN_NONE:
        Throw<std::runtime_error> ("unset source - humanNodePublic");

    case TOKEN_NODE_PUBLIC:
        return ToString ();

    default:
        Throw<std::runtime_error>("bad source: " + std::to_string(nVersion));
    }
    return {}; // Silence compiler warning.
}

bool RippleAddress::setNodePublic (std::string const& strPublic)
{
    mIsValid = SetString (
        strPublic, TOKEN_NODE_PUBLIC, Base58::getRippleAlphabet ());

    return mIsValid;
}

void RippleAddress::setNodePublic (Blob const& vPublic)
{
    mIsValid        = true;

    SetData (TOKEN_NODE_PUBLIC, vPublic);
}

bool RippleAddress::verifyNodePublic (
    uint256 const& hash, Blob const& vchSig, ECDSA fullyCanonical) const
{
    return verifySignature (getNodePublic(), hash, vchSig, fullyCanonical);
}

bool RippleAddress::verifyNodePublic (
    uint256 const& hash, std::string const& strSig, ECDSA fullyCanonical) const
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

    naNew.setNodePrivate (generateRootDeterministicPrivateKey (naSeed.getSeed()));

    return naNew;
}

Blob const& RippleAddress::getNodePrivateData () const
{
    switch (nVersion)
    {
    case TOKEN_NONE:
        Throw<std::runtime_error> ("unset source - getNodePrivateData");

    case TOKEN_NODE_PRIVATE:
        return vchData;

    default:
        Throw<std::runtime_error> ("bad source: " + std::to_string(nVersion));
    }
    return vchData; // Silence compiler warning.
}

uint256 RippleAddress::getNodePrivate () const
{
    switch (nVersion)
    {
    case TOKEN_NONE:
        Throw<std::runtime_error> ("unset source = getNodePrivate");

    case TOKEN_NODE_PRIVATE:
        return uint256 (vchData);

    default:
        Throw<std::runtime_error> ("bad source: " + std::to_string(nVersion));
    }
    return {}; // Silence compiler warning.
}

std::string RippleAddress::humanNodePrivate () const
{
    switch (nVersion)
    {
    case TOKEN_NONE:
        Throw<std::runtime_error> ("unset source - humanNodePrivate");

    case TOKEN_NODE_PRIVATE:
        return ToString ();

    default:
        Throw<std::runtime_error> ("bad source: " + std::to_string(nVersion));
    }
    return {}; // Silence compiler warning.
}

bool RippleAddress::setNodePrivate (std::string const& strPrivate)
{
    mIsValid = SetString (
        strPrivate, TOKEN_NODE_PRIVATE, Base58::getRippleAlphabet ());

    return mIsValid;
}

void RippleAddress::setNodePrivate (Blob const& vPrivate)
{
    mIsValid = true;

    SetData (TOKEN_NODE_PRIVATE, vPrivate);
}

void RippleAddress::setNodePrivate (uint256 hash256)
{
    mIsValid = true;

    SetData (TOKEN_NODE_PRIVATE, hash256);
}

void RippleAddress::signNodePrivate (uint256 const& hash, Blob& vchSig) const
{
    vchSig = ECDSASign (hash, getNodePrivate());

    if (vchSig.empty())
        Throw<std::runtime_error> ("Signing failed.");
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
    case TOKEN_NONE:
        Throw<std::runtime_error> ("unset source - getAccountPublic");

    case TOKEN_ACCOUNT_ID:
        Throw<std::runtime_error> ("public not available from account id");

    case TOKEN_ACCOUNT_PUBLIC:
        return vchData;

    default:
        Throw<std::runtime_error> ("bad source: " + std::to_string(nVersion));
    }
    return vchData; // Silence compiler warning.
}

std::string RippleAddress::humanAccountPublic () const
{
    switch (nVersion)
    {
    case TOKEN_NONE:
        Throw<std::runtime_error> ("unset source - humanAccountPublic");

    case TOKEN_ACCOUNT_ID:
        Throw<std::runtime_error> ("public not available from account id");

    case TOKEN_ACCOUNT_PUBLIC:
        return ToString ();

    default:
        Throw<std::runtime_error> ("bad source: " + std::to_string(nVersion));
    }
    return {}; // Silence compiler warning.
}

bool RippleAddress::setAccountPublic (std::string const& strPublic)
{
    mIsValid = SetString (
        strPublic, TOKEN_ACCOUNT_PUBLIC, Base58::getRippleAlphabet ());

    return mIsValid;
}

void RippleAddress::setAccountPublic (Blob const& vPublic)
{
    mIsValid = true;

    SetData (TOKEN_ACCOUNT_PUBLIC, vPublic);
}

void RippleAddress::setAccountPublic (RippleAddress const& generator, int seq)
{
    setAccountPublic (generatePublicDeterministicKey (
        generator.getGenerator(), seq));
}

bool RippleAddress::accountPublicVerify (
    Blob const& message, Blob const& vucSig, ECDSA fullyCanonical) const
{
    if (vchData.size() == 33  &&  vchData[0] == 0xED)
    {
        if (vucSig.size() != 64)
        {
            return false;
        }

        uint8_t const* publicKey = &vchData[1];
        uint8_t const* signature = &vucSig[0];

        return !ed25519_sign_open (message.data(), message.size(),
                                   publicKey, signature)
                && isCanonicalEd25519Signature (signature);
    }

    return verifySignature (getAccountPublic(),
        sha512Half(makeSlice(message)), vucSig,
            fullyCanonical);
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
    case TOKEN_NONE:
        Throw<std::runtime_error> ("unset source - getAccountPrivate");

    case TOKEN_ACCOUNT_SECRET:
        return uint256::fromVoid (vchData.data() + (vchData.size() - 32));

    default:
        Throw<std::runtime_error> ("bad source: " + std::to_string(nVersion));
    }
    return {}; // Silence compiler warning.
}

bool RippleAddress::setAccountPrivate (std::string const& strPrivate)
{
    mIsValid = SetString (
        strPrivate, TOKEN_ACCOUNT_SECRET, Base58::getRippleAlphabet ());

    return mIsValid;
}

void RippleAddress::setAccountPrivate (Blob const& vPrivate)
{
    mIsValid = true;
    SetData (TOKEN_ACCOUNT_SECRET, vPrivate);
}

void RippleAddress::setAccountPrivate (uint256 hash256)
{
    mIsValid = true;
    SetData (TOKEN_ACCOUNT_SECRET, hash256);
}

void RippleAddress::setAccountPrivate (
    RippleAddress const& generator, RippleAddress const& naSeed, int seq)
{
    uint256 secretKey = generatePrivateDeterministicKey (
        generator.getGenerator(), naSeed.getSeed(), seq);

    setAccountPrivate (secretKey);
}

Blob RippleAddress::accountPrivateSign (Blob const& message) const
{
    if (vchData.size() == 33  &&  vchData[0] == 0xED)
    {
        uint8_t const*      secretKey = &vchData[1];
        ed25519_public_key  publicKey;
        Blob                signature (sizeof (ed25519_signature));

        ed25519_publickey (secretKey, publicKey);

        ed25519_sign (
            message.data(), message.size(), secretKey, publicKey,
            &signature[0]);

        assert (isCanonicalEd25519Signature (signature.data()));

        return signature;
    }

    Blob result = ECDSASign(
        sha512Half(makeSlice(message)), getAccountPrivate());
    bool const ok = !result.empty();

    CondLog (!ok, lsWARNING, RippleAddress)
            << "accountPrivateSign: Signing failed.";

    return result;
}

Blob RippleAddress::accountPrivateEncrypt (
    RippleAddress const& naPublicTo, Blob const& vucPlainText) const
{
    uint256 secretKey = getAccountPrivate();
    Blob    publicKey = naPublicTo.getAccountPublic();

    Blob vucCipherText;

    {
        try
        {
            vucCipherText = encryptECIES (secretKey, publicKey, vucPlainText);
        }
        catch (std::exception const&)
        {
            // TODO: log this or explain why this is unimportant!
        }
    }

    return vucCipherText;
}

Blob RippleAddress::accountPrivateDecrypt (
    RippleAddress const& naPublicFrom, Blob const& vucCipherText) const
{
    uint256 secretKey = getAccountPrivate();
    Blob    publicKey = naPublicFrom.getAccountPublic();

    Blob    vucPlainText;

    {
        try
        {
            vucPlainText = decryptECIES (secretKey, publicKey, vucCipherText);
        }
        catch (std::exception const&)
        {
            // TODO: log this or explain why this is unimportant!
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
    case TOKEN_NONE:
        Throw<std::runtime_error> ("unset source - getGenerator");

    case TOKEN_FAMILY_GENERATOR:
        // Do nothing.
        return vchData;

    default:
        Throw<std::runtime_error> ("bad source: " + std::to_string(nVersion));
    }
    return vchData; // Silence compiler warning.
}

std::string RippleAddress::humanGenerator () const
{
    switch (nVersion)
    {
    case TOKEN_NONE:
        Throw<std::runtime_error> ("unset source - humanGenerator");

    case TOKEN_FAMILY_GENERATOR:
        return ToString ();

    default:
        Throw<std::runtime_error> ("bad source: " + std::to_string(nVersion));
    }
    return {}; // Silence compiler warning.
}

void RippleAddress::setGenerator (Blob const& vPublic)
{
    mIsValid        = true;
    SetData (TOKEN_FAMILY_GENERATOR, vPublic);
}

RippleAddress RippleAddress::createGeneratorPublic (RippleAddress const& naSeed)
{
    RippleAddress   naNew;
    naNew.setGenerator (generateRootDeterministicPublicKey (naSeed.getSeed()));
    return naNew;
}

//
// Seed
//

uint128 RippleAddress::getSeed () const
{
    switch (nVersion)
    {
    case TOKEN_NONE:
        Throw<std::runtime_error> ("unset source - getSeed");

    case TOKEN_FAMILY_SEED:
        return uint128 (vchData);

    default:
        Throw<std::runtime_error> ("bad source: " + std::to_string(nVersion));
    }
    return {}; // Silence compiler warning.
}

std::string RippleAddress::humanSeed1751 () const
{
    switch (nVersion)
    {
    case TOKEN_NONE:
        Throw<std::runtime_error> ("unset source - humanSeed1751");

    case TOKEN_FAMILY_SEED:
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
        Throw<std::runtime_error> ("bad source: " + std::to_string(nVersion));
    }
    return {}; // Silence compiler warning.
}

std::string RippleAddress::humanSeed () const
{
    switch (nVersion)
    {
    case TOKEN_NONE:
        Throw<std::runtime_error> ("unset source - humanSeed");

    case TOKEN_FAMILY_SEED:
        return ToString ();

    default:
        Throw<std::runtime_error> ("bad source: " + std::to_string(nVersion));
    }
    return {}; // Silence compiler warning.
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
    mIsValid = SetString (strSeed, TOKEN_FAMILY_SEED, Base58::getRippleAlphabet ());

    return mIsValid;
}

bool RippleAddress::setSeedGeneric (std::string const& strText)
{
    RippleAddress   naTemp;
    bool            bResult = true;
    uint128         uSeed;

    if (parseBase58<AccountID>(strText))
        return false;

    if (strText.empty ()
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

    SetData (TOKEN_FAMILY_SEED, hash128);
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

uint256 keyFromSeed (uint128 const& seed)
{
    return sha512Half_s(Slice(
        seed.data(), seed.size()));
}

RippleAddress getSeedFromRPC (Json::Value const& params)
{
    // This function is only called when `key_type` is present.
    assert (params.isMember (jss::key_type));

    bool const has_passphrase = params.isMember (jss::passphrase);
    bool const has_seed       = params.isMember (jss::seed);
    bool const has_seed_hex   = params.isMember (jss::seed_hex);

    int const n_secrets = has_passphrase + has_seed + has_seed_hex;

    if (n_secrets > 1)
    {
        // `passphrase`, `seed`, and `seed_hex` are mutually exclusive.
        return RippleAddress();
    }

    RippleAddress result;

    if (has_seed)
    {
        std::string const seed = params[jss::seed].asString();

        result.setSeed (seed);
    }
    else if (has_seed_hex)
    {
        uint128 seed;
        std::string const seed_hex = params[jss::seed_hex].asString();

        if (seed_hex.size() != 32  ||  !seed.SetHex (seed_hex, true))
        {
            return RippleAddress();
        }

        result.setSeed (seed);
    }
    else if (has_passphrase)
    {
        std::string const passphrase = params[jss::passphrase].asString();

        // Given `key_type`, `passphrase` is always the passphrase.
        uint128 const seed = PassPhraseToKey (passphrase);
        result.setSeed (seed);
    }

    return result;
}

KeyPair generateKeysFromSeed (KeyType type, RippleAddress const& seed)
{
    KeyPair result;

    if (! seed.isSet())
    {
        return result;
    }

    if (type == KeyType::secp256k1)
    {
        RippleAddress generator = RippleAddress::createGeneratorPublic (seed);
        result.secretKey.setAccountPrivate (generator, seed, 0);
        result.publicKey.setAccountPublic (generator, 0);
    }
    else if (type == KeyType::ed25519)
    {
        uint256 secretkey = keyFromSeed (seed.getSeed());

        Blob ed25519_key (33);
        ed25519_key[0] = 0xED;

        assert (secretkey.size() + 1 == ed25519_key.size());
        memcpy (&ed25519_key[1], secretkey.data(), secretkey.size());
        result.secretKey.setAccountPrivate (ed25519_key);

        ed25519_publickey (secretkey.data(), &ed25519_key[1]);
        result.publicKey.setAccountPublic (ed25519_key);

        secretkey.zero();  // security erase
    }
    else
    {
        assert (false);  // not reached
    }

    return result;
}

// DEPRECATED
AccountID
calcAccountID (RippleAddress const& publicKey)
{
    auto const& pk =
        publicKey.getAccountPublic();
    ripesha_hasher rsh;
    rsh(pk.data(), pk.size());
    auto const d = static_cast<
        ripesha_hasher::result_type>(rsh);
    AccountID id;
    static_assert(sizeof(d) == sizeof(id), "");
    std::memcpy(id.data(), d.data(), d.size());
    return id;
}

} // ripple
