//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================


DECLARE_INSTANCE (SerializedValidation);

SerializedValidation::SerializedValidation (SerializerIterator& sit, bool checkSignature)
    : STObject (getFormat (), sit, sfValidation)
    , mTrusted (false)
{
    mNodeID = RippleAddress::createNodePublic (getFieldVL (sfSigningPubKey)).getNodeID ();
    assert (mNodeID.isNonZero ());

    if  (checkSignature && !isValid ())
    {
        Log (lsTRACE) << "Invalid validation " << getJson (0);
        throw std::runtime_error ("Invalid validation");
    }
}

SerializedValidation::SerializedValidation (
    uint256 const& ledgerHash, uint32 signTime,
    const RippleAddress& raPub, bool isFull)
    : STObject (getFormat (), sfValidation)
    , mTrusted (false)
{
    // Does not sign
    setFieldH256 (sfLedgerHash, ledgerHash);
    setFieldU32 (sfSigningTime, signTime);

    setFieldVL (sfSigningPubKey, raPub.getNodePublic ());
    mNodeID = raPub.getNodeID ();
    assert (mNodeID.isNonZero ());

    if (!isFull)
        setFlag (sFullFlag);
}

void SerializedValidation::sign (const RippleAddress& raPriv)
{
    uint256 signingHash;
    sign (signingHash, raPriv);
}

void SerializedValidation::sign (uint256& signingHash, const RippleAddress& raPriv)
{
    signingHash = getSigningHash ();
    Blob signature;
    raPriv.signNodePrivate (signingHash, signature);
    setFieldVL (sfSignature, signature);
}

uint256 SerializedValidation::getSigningHash () const
{
    return STObject::getSigningHash (theConfig.SIGN_VALIDATION);
}

uint256 SerializedValidation::getLedgerHash () const
{
    return getFieldH256 (sfLedgerHash);
}

uint32 SerializedValidation::getSignTime () const
{
    return getFieldU32 (sfSigningTime);
}

uint32 SerializedValidation::getFlags () const
{
    return getFieldU32 (sfFlags);
}

bool SerializedValidation::isValid () const
{
    return isValid (getSigningHash ());
}

bool SerializedValidation::isValid (uint256 const& signingHash) const
{
    try
    {
        RippleAddress   raPublicKey = RippleAddress::createNodePublic (getFieldVL (sfSigningPubKey));
        return raPublicKey.isValid () && raPublicKey.verifyNodePublic (signingHash, getFieldVL (sfSignature));
    }
    catch (...)
    {
        Log (lsINFO) << "exception validating validation";
        return false;
    }
}

RippleAddress SerializedValidation::getSignerPublic () const
{
    RippleAddress a;
    a.setNodePublic (getFieldVL (sfSigningPubKey));
    return a;
}

bool SerializedValidation::isFull () const
{
    return (getFlags () & sFullFlag) != 0;
}

Blob SerializedValidation::getSignature () const
{
    return getFieldVL (sfSignature);
}

Blob SerializedValidation::getSigned () const
{
    Serializer s;
    add (s);
    return s.peekData ();
}

SOTemplate const& SerializedValidation::getFormat ()
{
    struct FormatHolder
    {
        SOTemplate format;

        FormatHolder ()
        {
            format.push_back (SOElement (sfFlags,           SOE_REQUIRED));
            format.push_back (SOElement (sfLedgerHash,      SOE_REQUIRED));
            format.push_back (SOElement (sfLedgerSequence,  SOE_OPTIONAL));
            format.push_back (SOElement (sfCloseTime,       SOE_OPTIONAL));
            format.push_back (SOElement (sfLoadFee,         SOE_OPTIONAL));
            format.push_back (SOElement (sfFeatures,        SOE_OPTIONAL));
            format.push_back (SOElement (sfBaseFee,         SOE_OPTIONAL));
            format.push_back (SOElement (sfReserveBase,     SOE_OPTIONAL));
            format.push_back (SOElement (sfReserveIncrement, SOE_OPTIONAL));
            format.push_back (SOElement (sfSigningTime,     SOE_REQUIRED));
            format.push_back (SOElement (sfSigningPubKey,   SOE_REQUIRED));
            format.push_back (SOElement (sfSignature,       SOE_OPTIONAL));
        }
    };

    static FormatHolder holder;

    return holder.format;
}

// vim:ts=4
