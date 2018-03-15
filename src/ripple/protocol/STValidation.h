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

#include <ripple/basics/Log.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/SecretKey.h>
#include <cstdint>
#include <functional>
#include <memory>

namespace ripple {

// Validation flags
const std::uint32_t vfFullyCanonicalSig =
    0x80000000;  // signature is fully canonical

class STValidation final : public STObject, public CountedObject<STValidation>
{
public:
    static char const*
    getCountedObjectName()
    {
        return "STValidation";
    }

    using pointer = std::shared_ptr<STValidation>;
    using ref = const std::shared_ptr<STValidation>&;

    enum { kFullFlag = 0x1 };

    /** Construct a STValidation from a peer.

        Construct a STValidation from serialized data previously shared by a
        peer.

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
        bool checkSignature)
        : STObject(getFormat(), sit, sfValidation)
    {
        auto const spk = getFieldVL(sfSigningPubKey);

        if (publicKeyType(makeSlice(spk)) != KeyType::secp256k1)
        {
            JLOG (debugLog().error())
                << "Invalid public key in validation" << getJson (0);
            Throw<std::runtime_error> ("Invalid public key in validation");
        }

        if  (checkSignature && !isValid ())
        {
            JLOG (debugLog().error())
                << "Invalid signature in validation" << getJson (0);
            Throw<std::runtime_error> ("Invalid signature in validation");
        }

        mNodeID = lookupNodeID(PublicKey(makeSlice(spk)));
        assert(mNodeID.isNonZero());
    }

    /** Fees to set when issuing a new validation.

        Optional fees to include when issuing a new validation
    */
    struct FeeSettings
    {
        boost::optional<std::uint32_t> loadFee;
        boost::optional<std::uint64_t> baseFee;
        boost::optional<std::uint32_t> reserveBase;
        boost::optional<std::uint32_t> reserveIncrement;
    };

    /** Construct, sign and trust a new STValidation

        Constructs, signs and trusts new STValidation issued by this node.

        @param ledgerHash The hash of the validated ledger
        @param ledgerIndex The index (sequence number) of the ledger
                @param consensusHash The hash of the consensus transaction set
        @param signTime When the validation is signed
        @param publicKey The current signing public key
        @param secretKey The current signing secret key
        @param nodeID ID corresponding to node's public master key
        @param isFull Whether the validation is full or partial
        @param fee FeeSettings to include in the validation
        @param amendments If not empty, the amendments to include in this validation
        @param cookie If not none, the validation cookie to set

        @note The fee and amendment settings are only set if not boost::none.
              Typically, the amendments and fees are set for validations of flag
              ledgers only.
    */
    STValidation(
        uint256 const& ledgerHash,
        std::uint32_t ledgerIndex,
        uint256 const& consensusHash,
        NetClock::time_point signTime,
        PublicKey const& publicKey,
        SecretKey const& secretKey,
        NodeID const& nodeID,
        bool isFull,
        FeeSettings const& fees,
        std::vector<uint256> const& amendments,
        boost::optional<std::uint64_t> const cookie);

    STBase*
    copy(std::size_t n, void* buf) const override
    {
        return emplace(n, buf, *this);
    }

    STBase*
    move(std::size_t n, void* buf) override
    {
        return emplace(n, buf, std::move(*this));
    }

    // Hash of the validated ledger
    uint256
    getLedgerHash() const;

    // Hash of consensus transaction set used to generate ledger
    uint256
    getConsensusHash() const;

    NetClock::time_point
    getSignTime() const;

    NetClock::time_point
    getSeenTime() const;

    PublicKey
    getSignerPublic() const;

    NodeID
    getNodeID() const
    {
        return mNodeID;
    }

    bool
    isValid() const;

    bool
    isFull() const;

    bool
    isTrusted() const
    {
        return mTrusted;
    }

    uint256
    getSigningHash() const;

    bool
    isValid(uint256 const&) const;

    void
    setTrusted()
    {
        mTrusted = true;
    }

    void
    setUntrusted()
    {
        mTrusted = false;
    }

    void
    setSeen(NetClock::time_point s)
    {
        mSeen = s;
    }

    Blob
    getSerialized() const;

    Blob
    getSignature() const;

private:

    static SOTemplate const&
    getFormat();

    NodeID mNodeID;
    bool mTrusted = false;
    NetClock::time_point mSeen = {};
};

} // ripple

#endif
