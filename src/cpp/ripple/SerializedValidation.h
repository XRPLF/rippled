//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_SERIALIZEDVALIDATION_H
#define RIPPLE_SERIALIZEDVALIDATION_H

class SerializedValidation
    : public STObject
    , public CountedObject <SerializedValidation>
{
public:
    char const* getCountedObjectName () { return "SerializedValidation"; }

    typedef boost::shared_ptr<SerializedValidation>         pointer;
    typedef const boost::shared_ptr<SerializedValidation>&  ref;

    static const uint32 sFullFlag = 0x1;

    // These throw if the object is not valid
    SerializedValidation (SerializerIterator & sit, bool checkSignature = true);

    // Does not sign the validation
    SerializedValidation (uint256 const & ledgerHash, uint32 signTime, const RippleAddress & raPub, bool isFull);

    uint256         getLedgerHash ()     const;
    uint32          getSignTime ()       const;
    uint32          getFlags ()          const;
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

#endif
// vim:ts=4
