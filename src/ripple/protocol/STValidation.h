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

#ifndef RIPPLE_PROTOCOL_STVALIDATION_H_INCLUDED
#define RIPPLE_PROTOCOL_STVALIDATION_H_INCLUDED

#include <ripple/basics/FeeUnits.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/SecretKey.h>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace ripple {

// Validation flags

// This is a full (as opposed to a partial) validation
constexpr std::uint32_t vfFullValidation = 0x00000001;

// The signature is fully canonical
constexpr std::uint32_t vfFullyCanonicalSig = 0x80000000;

class STValidation final : public STObject, public CountedObject<STValidation>
{
    bool mTrusted = false;

    // Determines the validity of the signature in this validation; unseated
    // optional if we haven't yet checked it, a boolean otherwise.
    mutable std::optional<bool> valid_;

    // The public key associated with the key used to sign this validation
    PublicKey const signingPubKey_;

    // The ID of the validator that issued this validation. For validators
    // that use manifests this will be derived from the master public key.
    NodeID const nodeID_;

    NetClock::time_point seenTime_ = {};

public:
    /** Construct a STValidation from a peer from serialized data.

        @param sit Iterator over serialized data
        @param lookupNodeID Invocable with signature
                               NodeID(PublicKey const&)
                            used to find the Node ID based on the public key
                            that signed the validation. For manifest based
                            validators, this should be the NodeID of the master
                            public key.
        @param checkSignature Whether to verify the data was signed properly

        @note Throws if the object is not valid
    */
    template <class LookupNodeID>
    STValidation(
        SerialIter& sit,
        LookupNodeID&& lookupNodeID,
        bool checkSignature);

    /** Construct, sign and trust a new STValidation issued by this node.

        @param signTime When the validation is signed
        @param publicKey The current signing public key
        @param secretKey The current signing secret key
        @param nodeID ID corresponding to node's public master key
        @param f callback function to "fill" the validation with necessary data
    */
    template <typename F>
    STValidation(
        NetClock::time_point signTime,
        PublicKey const& pk,
        SecretKey const& sk,
        NodeID const& nodeID,
        F&& f);

    // Hash of the validated ledger
    uint256
    getLedgerHash() const;

    // Hash of consensus transaction set used to generate ledger
    uint256
    getConsensusHash() const;

    NetClock::time_point
    getSignTime() const;

    NetClock::time_point
    getSeenTime() const noexcept;

    PublicKey const&
    getSignerPublic() const noexcept;

    NodeID const&
    getNodeID() const noexcept;

    bool
    isValid() const noexcept;

    bool
    isFull() const noexcept;

    bool
    isTrusted() const noexcept;

    uint256
    getSigningHash() const;

    void
    setTrusted();

    void
    setUntrusted();

    void
    setSeen(NetClock::time_point s);

    Blob
    getSerialized() const;

    Blob
    getSignature() const;

private:
    static SOTemplate const&
    validationFormat();

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

template <class LookupNodeID>
STValidation::STValidation(
    SerialIter& sit,
    LookupNodeID&& lookupNodeID,
    bool checkSignature)
    : STObject(validationFormat(), sit, sfValidation)
    , signingPubKey_([this]() {
        auto const spk = getFieldVL(sfSigningPubKey);

        if (publicKeyType(makeSlice(spk)) != KeyType::secp256k1)
            Throw<std::runtime_error>("Invalid public key in validation");

        return PublicKey{makeSlice(spk)};
    }())
    , nodeID_(lookupNodeID(signingPubKey_))
{
    if (checkSignature && !isValid())
    {
        JLOG(debugLog().error()) << "Invalid signature in validation: "
                                 << getJson(JsonOptions::none);
        Throw<std::runtime_error>("Invalid signature in validation");
    }

    assert(nodeID_.isNonZero());
}

/** Construct, sign and trust a new STValidation issued by this node.

    @param signTime When the validation is signed
    @param publicKey The current signing public key
    @param secretKey The current signing secret key
    @param nodeID ID corresponding to node's public master key
    @param f callback function to "fill" the validation with necessary data
*/
template <typename F>
STValidation::STValidation(
    NetClock::time_point signTime,
    PublicKey const& pk,
    SecretKey const& sk,
    NodeID const& nodeID,
    F&& f)
    : STObject(validationFormat(), sfValidation)
    , signingPubKey_(pk)
    , nodeID_(nodeID)
    , seenTime_(signTime)
{
    assert(nodeID_.isNonZero());

    // First, set our own public key:
    if (publicKeyType(pk) != KeyType::secp256k1)
        LogicError("We can only use secp256k1 keys for signing validations");

    setFieldVL(sfSigningPubKey, pk.slice());
    setFieldU32(sfSigningTime, signTime.time_since_epoch().count());

    // Perform additional initialization
    f(*this);

    // Finally, sign the validation and mark it as trusted:
    setFlag(vfFullyCanonicalSig);
    setFieldVL(sfSignature, signDigest(pk, sk, getSigningHash()));
    setTrusted();

    // Check to ensure that all required fields are present.
    for (auto const& e : validationFormat())
    {
        if (e.style() == soeREQUIRED && !isFieldPresent(e.sField()))
            LogicError(
                "Required field '" + e.sField().getName() +
                "' missing from validation.");
    }

    // We just signed this, so it should be valid.
    valid_ = true;
}

inline PublicKey const&
STValidation::getSignerPublic() const noexcept
{
    return signingPubKey_;
}

inline NodeID const&
STValidation::getNodeID() const noexcept
{
    return nodeID_;
}

inline bool
STValidation::isTrusted() const noexcept
{
    return mTrusted;
}

inline void
STValidation::setTrusted()
{
    mTrusted = true;
}

inline void
STValidation::setUntrusted()
{
    mTrusted = false;
}

inline void
STValidation::setSeen(NetClock::time_point s)
{
    seenTime_ = s;
}

}  // namespace ripple

#endif
