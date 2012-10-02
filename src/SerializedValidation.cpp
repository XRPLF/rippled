
#include "SerializedValidation.h"

#include "HashPrefixes.h"

SOElement SerializedValidation::sValidationFormat[] = {
	{ sfFlags,			SOE_REQUIRED },
	{ sfLedgerHash,		SOE_REQUIRED },
	{ sfLedgerSequence,	SOE_OPTIONAL },
	{ sfCloseTime,		SOE_OPTIONAL },
	{ sfLoadFee,		SOE_OPTIONAL },
	{ sfBaseFee,		SOE_OPTIONAL },
	{ sfSigningTime,	SOE_REQUIRED },
	{ sfSigningPubKey,	SOE_REQUIRED },
	{ sfInvalid,		SOE_END }
};

const uint32 SerializedValidation::sFullFlag		= 0x00010000;

SerializedValidation::SerializedValidation(SerializerIterator& sit, bool checkSignature)
	: STObject(sValidationFormat, sit, sfValidation), mSignature(sit, sfSignature), mTrusted(false)
{
	if  (checkSignature && !isValid()) throw std::runtime_error("Invalid validation");
}

SerializedValidation::SerializedValidation(const uint256& ledgerHash, uint32 signTime,
		const NewcoinAddress& naSeed, bool isFull)
	: STObject(sValidationFormat, sfValidation), mSignature(sfSignature), mTrusted(false)
{
	setFieldH256(sfLedgerHash, ledgerHash);
	setFieldU32(sfSigningTime, signTime);
	if (naSeed.isValid())
		setFieldVL(sfSigningPubKey, NewcoinAddress::createNodePublic(naSeed).getNodePublic());
	if (!isFull) setFlag(sFullFlag);

	NewcoinAddress::createNodePrivate(naSeed).signNodePrivate(getSigningHash(), mSignature.peekValue());
	// XXX Check if this can fail.
	// if (!NewcoinAddress::createNodePrivate(naSeed).signNodePrivate(getSigningHash(), mSignature.peekValue()))
	//	throw std::runtime_error("Unable to sign validation");
}

uint256 SerializedValidation::getSigningHash() const
{
	Serializer s;

	s.add32(sHP_Validation);
	add(s);

	return s.getSHA512Half();
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
		NewcoinAddress	naPublicKey	= NewcoinAddress::createNodePublic(getFieldVL(sfSigningPubKey));
		return naPublicKey.isValid() && naPublicKey.verifyNodePublic(signingHash, mSignature.peekValue());
	}
	catch (...)
	{
		return false;
	}
}

NewcoinAddress SerializedValidation::getSignerPublic() const
{
	NewcoinAddress a;
	a.setNodePublic(getFieldVL(sfSigningPubKey));
	return a;
}

bool SerializedValidation::isFull() const
{
	return (getFlags() & sFullFlag) != 0;
}

void SerializedValidation::addSigned(Serializer& s) const
{
	add(s);
	mSignature.add(s);
}

void SerializedValidation::addSignature(Serializer& s) const
{
	mSignature.add(s);
}

std::vector<unsigned char> SerializedValidation::getSigned() const
{
	Serializer s;
	addSigned(s);
	return s.peekData();
}

std::vector<unsigned char> SerializedValidation::getSignature() const
{
	return mSignature.peekValue();
}
// vim:ts=4
