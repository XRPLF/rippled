
#include "SerializedValidation.h"

#include "HashPrefixes.h"
#include "Log.h"

std::vector<SOElement::ptr> sValidationFormat;

static bool SVFInit()
{
	 sValidationFormat.push_back(new SOElement(sfFlags,				SOE_REQUIRED));
	 sValidationFormat.push_back(new SOElement(sfLedgerHash,		SOE_REQUIRED));
	 sValidationFormat.push_back(new SOElement(sfLedgerSequence,	SOE_OPTIONAL));
	 sValidationFormat.push_back(new SOElement(sfCloseTime,			SOE_OPTIONAL));
	 sValidationFormat.push_back(new SOElement(sfLoadFee,			SOE_OPTIONAL));
	 sValidationFormat.push_back(new SOElement(sfBaseFee,			SOE_OPTIONAL));
	 sValidationFormat.push_back(new SOElement(sfSigningTime,		SOE_REQUIRED));
	 sValidationFormat.push_back(new SOElement(sfSigningPubKey,		SOE_REQUIRED));
	 sValidationFormat.push_back(new SOElement(sfSignature,			SOE_OPTIONAL));
	 return true;
};

bool SVFinitComplete = SVFInit();

const uint32 SerializedValidation::sFullFlag		= 0x1;

SerializedValidation::SerializedValidation(SerializerIterator& sit, bool checkSignature)
	: STObject(sValidationFormat, sit, sfValidation), mTrusted(false)
{
	mNodeID = RippleAddress::createNodePublic(getFieldVL(sfSigningPubKey)).getNodeID();
	assert(mNodeID.isNonZero());
	if  (checkSignature && !isValid())
	{
		Log(lsTRACE) << "Invalid validation " << getJson(0);
		throw std::runtime_error("Invalid validation");
	}
}

SerializedValidation::SerializedValidation(const uint256& ledgerHash, uint32 signTime,
		const RippleAddress& naPub, const RippleAddress& naPriv, bool isFull, uint256& signingHash)
	: STObject(sValidationFormat, sfValidation), mTrusted(false)
{
	setFieldH256(sfLedgerHash, ledgerHash);
	setFieldU32(sfSigningTime, signTime);

	setFieldVL(sfSigningPubKey, naPub.getNodePublic());
	mNodeID = naPub.getNodeID();
	assert(mNodeID.isNonZero());

	if (!isFull)
		setFlag(sFullFlag);

	signingHash = getSigningHash();
	std::vector<unsigned char> signature;
	naPriv.signNodePrivate(signingHash, signature);
	setFieldVL(sfSignature, signature);
	// XXX Check if this can fail.
	// if (!RippleAddress::createNodePrivate(naSeed).signNodePrivate(getSigningHash(), mSignature.peekValue()))
	//	throw std::runtime_error("Unable to sign validation");
}

uint256 SerializedValidation::getSigningHash() const
{
	return STObject::getSigningHash(sHP_Validation);
}

uint256 SerializedValidation::getLedgerHash() const
{
	return getFieldH256(sfLedgerHash);
}

uint32 SerializedValidation::getSignTime() const
{
	return getFieldU32(sfSigningTime);
}

uint32 SerializedValidation::getFlags() const
{
	return getFieldU32(sfFlags);
}

bool SerializedValidation::isValid() const
{
	return isValid(getSigningHash());
}

bool SerializedValidation::isValid(const uint256& signingHash) const
{
	try
	{
		RippleAddress	naPublicKey	= RippleAddress::createNodePublic(getFieldVL(sfSigningPubKey));
		return naPublicKey.isValid() && naPublicKey.verifyNodePublic(signingHash, getFieldVL(sfSignature));
	}
	catch (...)
	{
		Log(lsINFO) << "exception validating validation";
		return false;
	}
}

RippleAddress SerializedValidation::getSignerPublic() const
{
	RippleAddress a;
	a.setNodePublic(getFieldVL(sfSigningPubKey));
	return a;
}

bool SerializedValidation::isFull() const
{
	return (getFlags() & sFullFlag) != 0;
}

std::vector<unsigned char> SerializedValidation::getSignature() const
{
	return getFieldVL(sfSignature);
}

std::vector<unsigned char> SerializedValidation::getSigned() const
{
	Serializer s;
	add(s);
	return s.peekData();
}

// vim:ts=4
