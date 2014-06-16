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

#ifndef RIPPLE_RIPPLEADDRESS_H
#define RIPPLE_RIPPLEADDRESS_H

#include <ripple/module/data/crypto/Base58Data.h>

#include <ripple/types/api/UInt160.h>
#include <ripple/types/api/RippleAccountID.h>
#include <ripple/types/api/RippleAccountPrivateKey.h>
#include <ripple/types/api/RippleAccountPublicKey.h>
#include <ripple/types/api/RipplePrivateKey.h>
#include <ripple/types/api/RipplePublicKey.h>
#include <ripple/types/api/RipplePublicKeyHash.h>
#include <ripple/sslutil/api/ECDSACanonical.h>

namespace ripple {

//
// Used to hold addresses and parse and produce human formats.
//
// XXX This needs to be reworked to store data in uint160 and uint256.  Conversion to CBase58Data should happen as needed.
class RippleAddress : public CBase58Data
{
private:
    typedef enum
    {
        VER_NONE                = 1,
        VER_NODE_PUBLIC         = 28,
        VER_NODE_PRIVATE        = 32,
        VER_ACCOUNT_ID          = 0,
        VER_ACCOUNT_PUBLIC      = 35,
        VER_ACCOUNT_PRIVATE     = 34,
        VER_FAMILY_GENERATOR    = 41,
        VER_FAMILY_SEED         = 33,
    } VersionEncoding;

    bool    mIsValid;

public:
    RippleAddress ();

    // For public and private key, checks if they are legal.
    bool isValid () const
    {
        return mIsValid;
    }

    void clear ();
    bool isSet () const;

    std::string humanAddressType () const;

    //
    // Node Public - Also used for Validators
    //
    uint160 getNodeID () const;
    Blob const& getNodePublic () const;

    std::string humanNodePublic () const;

    bool setNodePublic (const std::string& strPublic);
    void setNodePublic (Blob const& vPublic);
    bool verifyNodePublic (uint256 const& hash, Blob const& vchSig, ECDSA mustBeFullyCanonical) const;
    bool verifyNodePublic (uint256 const& hash, const std::string& strSig, ECDSA mustBeFullyCanonical) const;

    static RippleAddress createNodePublic (const RippleAddress& naSeed);
    static RippleAddress createNodePublic (Blob const& vPublic);
    static RippleAddress createNodePublic (const std::string& strPublic);

    //
    // Node Private
    //
    Blob const& getNodePrivateData () const;
    uint256 getNodePrivate () const;

    std::string humanNodePrivate () const;

    bool setNodePrivate (const std::string& strPrivate);
    void setNodePrivate (Blob const& vPrivate);
    void setNodePrivate (uint256 hash256);
    void signNodePrivate (uint256 const& hash, Blob& vchSig) const;

    static RippleAddress createNodePrivate (const RippleAddress& naSeed);

    //
    // Accounts IDs
    //
    uint160 getAccountID () const;

    std::string humanAccountID () const;

    bool setAccountID (const std::string& strAccountID, Base58::Alphabet const& alphabet = Base58::getRippleAlphabet());
    void setAccountID (const uint160& hash160In);

    static RippleAddress createAccountID (const std::string& strAccountID)
    {
        RippleAddress na;
        na.setAccountID (strAccountID);
        return na;
    }

    static RippleAddress createAccountID (const uint160& uiAccountID);

    static std::string createHumanAccountID (const uint160& uiAccountID)
    {
        return createAccountID (uiAccountID).humanAccountID ();
    }

    static std::string createHumanAccountID (Blob const& vPrivate)
    {
        return createAccountPrivate (vPrivate).humanAccountID ();
    }

    //
    // Accounts Public
    //
    Blob const& getAccountPublic () const;

    std::string humanAccountPublic () const;

    bool setAccountPublic (const std::string& strPublic);
    void setAccountPublic (Blob const& vPublic);
    void setAccountPublic (const RippleAddress& generator, int seq);

    bool accountPublicVerify (uint256 const& uHash, Blob const& vucSig, ECDSA mustBeFullyCanonical) const;

    static RippleAddress createAccountPublic (Blob const& vPublic)
    {
        RippleAddress   naNew;

        naNew.setAccountPublic (vPublic);

        return naNew;
    }

    static std::string createHumanAccountPublic (Blob const& vPublic)
    {
        return createAccountPublic (vPublic).humanAccountPublic ();
    }

    // Create a deterministic public key from a public generator.
    static RippleAddress createAccountPublic (const RippleAddress& naGenerator, int iSeq);

    //
    // Accounts Private
    //
    uint256 getAccountPrivate () const;

    std::string humanAccountPrivate () const;

    bool setAccountPrivate (const std::string& strPrivate);
    void setAccountPrivate (Blob const& vPrivate);
    void setAccountPrivate (uint256 hash256);
    void setAccountPrivate (const RippleAddress& naGenerator, const RippleAddress& naSeed, int seq);

    bool accountPrivateSign (uint256 const& uHash, Blob& vucSig) const;

    // Encrypt a message.
    Blob accountPrivateEncrypt (const RippleAddress& naPublicTo, Blob const& vucPlainText) const;

    // Decrypt a message.
    Blob accountPrivateDecrypt (const RippleAddress& naPublicFrom, Blob const& vucCipherText) const;

    static RippleAddress createAccountPrivate (const RippleAddress& naGenerator, const RippleAddress& naSeed, int iSeq);

    static RippleAddress createAccountPrivate (Blob const& vPrivate)
    {
        RippleAddress   naNew;

        naNew.setAccountPrivate (vPrivate);

        return naNew;
    }

    static std::string createHumanAccountPrivate (Blob const& vPrivate)
    {
        return createAccountPrivate (vPrivate).humanAccountPrivate ();
    }

    //
    // Generators
    // Use to generate a master or regular family.
    //
    Blob const& getGenerator () const;

    std::string humanGenerator () const;

    bool setGenerator (const std::string& strGenerator);
    void setGenerator (Blob const& vPublic);
    // void setGenerator(const RippleAddress& seed);

    // Create generator for making public deterministic keys.
    static RippleAddress createGeneratorPublic (const RippleAddress& naSeed);

    //
    // Seeds
    // Clients must disallow reconizable entries from being seeds.
    uint128 getSeed () const;

    std::string humanSeed () const;
    std::string humanSeed1751 () const;

    bool setSeed (const std::string& strSeed);
    int setSeed1751 (const std::string& strHuman1751);
    bool setSeedGeneric (const std::string& strText);
    void setSeed (uint128 hash128);
    void setSeedRandom ();

    static RippleAddress createSeedRandom ();
    static RippleAddress createSeedGeneric (const std::string& strText);
};

//------------------------------------------------------------------------------

/** RipplePublicKey */
template <>
struct RipplePublicKeyTraits::assign <RippleAddress>
{
    void operator() (value_type& value, RippleAddress const& v) const
    {
        Blob const& b (v.getNodePublic ());
        construct (&b.front(), &b.back()+1, value);
    }
};

/** RipplePublicKeyHash */
template <>
struct RipplePublicKeyHashTraits::assign <RippleAddress>
{
    void operator() (value_type& value, RippleAddress const& v) const
    {
        uint160 const ui (v.getNodeID ());
        construct (ui.begin(), ui.end(), value);
    }
};

/** RipplePrivateKey */
template <>
struct RipplePrivateKeyTraits::assign <RippleAddress>
{
    void operator() (value_type& value, RippleAddress const& v) const
    {
        uint256 const ui (v.getNodePrivate ());
        construct (ui.begin(), ui.end(), value);
    }
};

/** RippleAccountID */
template <>
struct RippleAccountIDTraits::assign <RippleAddress>
{
    void operator() (value_type& value, RippleAddress const& v) const
    {
        uint160 const ui (v.getAccountID ());
        construct (ui.begin(), ui.end(), value);
    }
};

/** RippleAccountPublicKey */
template <>
struct RippleAccountPublicKeyTraits::assign <RippleAddress>
{
    void operator() (value_type& value, RippleAddress const& v) const
    {
        Blob const& b (v.getAccountPublic ());
        construct (&b.front(), &b.back()+1, value);
    }
};

/** RippleAccountPrivateKey */
template <>
struct RippleAccountPrivateKeyTraits::assign <RippleAddress>
{
    void operator() (value_type& value, RippleAddress const& v) const
    {
        uint256 const ui (v.getAccountPrivate ());
        construct (ui.begin(), ui.end(), value);
    }
};

} // ripple

#endif
