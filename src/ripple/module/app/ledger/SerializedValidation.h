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

#ifndef RIPPLE_SERIALIZEDVALIDATION_H
#define RIPPLE_SERIALIZEDVALIDATION_H

namespace ripple {

// Validation flags
const std::uint32_t vfFullyCanonicalSig    = 0x80000000; // signature is fully canonical

class SerializedValidation
    : public STObject
    , public CountedObject <SerializedValidation>
{
public:
    static char const* getCountedObjectName () { return "SerializedValidation"; }

    typedef std::shared_ptr<SerializedValidation>         pointer;
    typedef const std::shared_ptr<SerializedValidation>&  ref;

    enum
    {
        kFullFlag = 0x1
    };

    // These throw if the object is not valid
    SerializedValidation (SerializerIterator & sit, bool checkSignature = true);

    // Does not sign the validation
    SerializedValidation (uint256 const & ledgerHash, std::uint32_t signTime,
                          const RippleAddress & raPub, bool isFull);

    uint256         getLedgerHash ()     const;
    std::uint32_t   getSignTime ()       const;
    std::uint32_t   getFlags ()          const;
    RippleAddress   getSignerPublic ()   const;
    uint160         getNodeID ()         const
    {
        return mNodeID;
    }
    bool            isValid ()           const;
    bool            isFull ()            const;
    bool            isTrusted ()         const
    {
        return mTrusted;
    }
    uint256         getSigningHash ()    const;
    bool            isValid (uint256 const& ) const;

    void                        setTrusted ()
    {
        mTrusted = true;
    }
    Blob    getSigned ()                 const;
    Blob    getSignature ()              const;
    void sign (uint256 & signingHash, const RippleAddress & raPrivate);
    void sign (const RippleAddress & raPrivate);

    // The validation this replaced
    uint256 const& getPreviousHash ()
    {
        return mPreviousHash;
    }
    bool isPreviousHash (uint256 const & h) const
    {
        return mPreviousHash == h;
    }
    void setPreviousHash (uint256 const & h)
    {
        mPreviousHash = h;
    }

private:
    static SOTemplate const& getFormat ();

    void setNode ();

    uint256 mPreviousHash;
    uint160 mNodeID;
    bool mTrusted;
};

} // ripple

#endif
