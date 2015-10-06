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

#ifndef RIPPLE_PROTOCOL_RIPPLEADDRESS_H_INCLUDED
#define RIPPLE_PROTOCOL_RIPPLEADDRESS_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <ripple/crypto/Base58Data.h>
#include <ripple/crypto/ECDSACanonical.h>
#include <ripple/crypto/KeyType.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/RipplePublicKey.h>
#include <ripple/protocol/tokens.h>
#include <ripple/protocol/UInt160.h>
#include <ripple/protocol/UintTypes.h>

namespace ripple {

//
// Used to hold addresses and parse and produce human formats.
//
// XXX This needs to be reworked to store data in uint160 and uint256.

class RippleAddress : private CBase58Data
{
private:
    bool    mIsValid;

public:
    RippleAddress ();

    void const*
    data() const noexcept
    {
        return vchData.data();
    }

    std::size_t
    size() const noexcept
    {
        return vchData.size();
    }

    // For public and private key, checks if they are legal.
    bool isValid () const
    {
        return mIsValid;
    }

    void clear ();
    bool isSet () const;

    /** Returns the public key.
        Precondition: version == TOKEN_NODE_PUBLIC
    */
    RipplePublicKey
    toPublicKey() const;

    //
    // Node Public - Also used for Validators
    //
    NodeID getNodeID () const;
    Blob const& getNodePublic () const;

    std::string humanNodePublic () const;

    bool setNodePublic (std::string const& strPublic);
    void setNodePublic (Blob const& vPublic);
    bool verifyNodePublic (uint256 const& hash, Blob const& vchSig,
                           ECDSA mustBeFullyCanonical) const;
    bool verifyNodePublic (uint256 const& hash, std::string const& strSig,
                           ECDSA mustBeFullyCanonical) const;

    static RippleAddress createNodePublic (RippleAddress const& naSeed);
    static RippleAddress createNodePublic (Blob const& vPublic);
    static RippleAddress createNodePublic (std::string const& strPublic);

    //
    // Node Private
    //
    Blob const& getNodePrivateData () const;
    uint256 getNodePrivate () const;

    std::string humanNodePrivate () const;

    bool setNodePrivate (std::string const& strPrivate);
    void setNodePrivate (Blob const& vPrivate);
    void setNodePrivate (uint256 hash256);
    void signNodePrivate (uint256 const& hash, Blob& vchSig) const;

    static RippleAddress createNodePrivate (RippleAddress const& naSeed);

    //
    // Accounts Public
    //
    Blob const& getAccountPublic () const;

    std::string humanAccountPublic () const;

    bool setAccountPublic (std::string const& strPublic);
    void setAccountPublic (Blob const& vPublic);
    void setAccountPublic (RippleAddress const& generator, int seq);

    bool accountPublicVerify (Blob const& message, Blob const& vucSig,
                              ECDSA mustBeFullyCanonical) const;

    static RippleAddress createAccountPublic (Blob const& vPublic)
    {
        RippleAddress naNew;
        naNew.setAccountPublic (vPublic);
        return naNew;
    }

    static std::string createHumanAccountPublic (Blob const& vPublic)
    {
        return createAccountPublic (vPublic).humanAccountPublic ();
    }

    // Create a deterministic public key from a public generator.
    static RippleAddress createAccountPublic (
        RippleAddress const& naGenerator, int iSeq);

    //
    // Accounts Private
    //
    uint256 getAccountPrivate () const;

    bool setAccountPrivate (std::string const& strPrivate);
    void setAccountPrivate (Blob const& vPrivate);
    void setAccountPrivate (uint256 hash256);
    void setAccountPrivate (RippleAddress const& naGenerator,
                            RippleAddress const& naSeed, int seq);

    Blob accountPrivateSign (Blob const& message) const;

    // Encrypt a message.
    Blob accountPrivateEncrypt (
        RippleAddress const& naPublicTo, Blob const& vucPlainText) const;

    // Decrypt a message.
    Blob accountPrivateDecrypt (
        RippleAddress const& naPublicFrom, Blob const& vucCipherText) const;

    static RippleAddress createAccountPrivate (
        RippleAddress const& generator, RippleAddress const& seed, int iSeq);

    static RippleAddress createAccountPrivate (Blob const& vPrivate)
    {
        RippleAddress   naNew;

        naNew.setAccountPrivate (vPrivate);

        return naNew;
    }

    //
    // Generators
    // Use to generate a master or regular family.
    //
    Blob const& getGenerator () const;

    std::string humanGenerator () const;

    bool setGenerator (std::string const& strGenerator);
    void setGenerator (Blob const& vPublic);
    // void setGenerator(RippleAddress const& seed);

    // Create generator for making public deterministic keys.
    static RippleAddress createGeneratorPublic (RippleAddress const& naSeed);

    //
    // Seeds
    // Clients must disallow reconizable entries from being seeds.
    uint128 getSeed () const;

    std::string humanSeed () const;
    std::string humanSeed1751 () const;

    bool setSeed (std::string const& strSeed);
    int setSeed1751 (std::string const& strHuman1751);
    bool setSeedGeneric (std::string const& strText);
    void setSeed (uint128 hash128);
    void setSeedRandom ();

    static RippleAddress createSeedRandom ();
    static RippleAddress createSeedGeneric (std::string const& strText);

    std::string ToString () const
        {return static_cast<CBase58Data const&>(*this).ToString();}

    template <class Hasher>
    friend
    void
    hash_append(Hasher& hasher, RippleAddress const& value)
    {
        using beast::hash_append;
        hash_append(hasher, static_cast<CBase58Data const&>(value));
    }

    friend
    bool
    operator==(RippleAddress const& lhs, RippleAddress const& rhs)
    {
        return static_cast<CBase58Data const&>(lhs) ==
               static_cast<CBase58Data const&>(rhs);
    }

    friend
    bool
    operator <(RippleAddress const& lhs, RippleAddress const& rhs)
    {
        return static_cast<CBase58Data const&>(lhs) <
               static_cast<CBase58Data const&>(rhs);
    }
};

//------------------------------------------------------------------------------

inline
bool
operator!=(RippleAddress const& lhs, RippleAddress const& rhs)
{
    return !(lhs == rhs);
}

inline
bool
operator >(RippleAddress const& lhs, RippleAddress const& rhs)
{
    return rhs < lhs;
}

inline
bool
operator<=(RippleAddress const& lhs, RippleAddress const& rhs)
{
    return !(rhs < lhs);
}

inline
bool
operator>=(RippleAddress const& lhs, RippleAddress const& rhs)
{
    return !(lhs < rhs);
}

struct KeyPair
{
    RippleAddress secretKey;
    RippleAddress publicKey;
};

uint256 keyFromSeed (uint128 const& seed);

RippleAddress getSeedFromRPC (Json::Value const& params);

KeyPair generateKeysFromSeed (KeyType keyType, RippleAddress const& seed);

// DEPRECATED
AccountID
calcAccountID (RippleAddress const& publicKey);

} // ripple

#endif
