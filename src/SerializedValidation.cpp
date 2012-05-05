
#include "SerializedValidation.h"

SOElement SerializedValidation::sValidationFormat[16] = {
	{ sfFlags,			"Flags",			STI_UINT32,		SOE_FLAGS,		0 },
	{ sfLedgerHash,		"LedgerHash",		STI_HASH256,	SOE_REQUIRED,	0 },
	{ sfSigningKey,		"SigningKey",		STI_VL,			SOE_REQUIRED,	0 },
	{ sfExtensions,		"Extensions",		STI_TL,			SOE_IFFLAG,		0x01000000 },
	{ sfInvalid,		NULL,				STI_DONE,		SOE_NEVER,		-1 },
};

const uint32 SerializedValidation::sFullFlag		= 0x00010000;
const uint32 SerializedValidation::sValidationMagic	= 0x4c575200; // "LGR"

SerializedValidation::SerializedValidation(SerializerIterator& sit, bool checkSignature)
	: STObject(sValidationFormat, sit), mSignature(sit, "Signature")
{
	if (!isValid()) throw std::runtime_error("Invalid validation");
}

SerializedValidation::SerializedValidation(const uint256& ledgerHash, CKey::pointer nodeKey, bool isFull)
	: STObject(sValidationFormat), mSignature("Signature")
{
	setValueFieldH256(sfLedgerHash, ledgerHash);
	setValueFieldVL(sfSigningKey, nodeKey->GetPubKey());
	if (!isFull) setFlag(sFullFlag);

	if (!nodeKey->Sign(getSigningHash(), mSignature.peekValue()))
		throw std::runtime_error("Unable to sign validation");
}

uint256 SerializedValidation::getSigningHash() const
{
	Serializer s;
	s.add32(sValidationMagic);
	add(s);
	return s.getSHA512Half();
}

uint256 SerializedValidation::getLedgerHash() const
{
	return getValueFieldH256(sfLedgerHash);
}

bool SerializedValidation::isValid() const
{
	try
	{
		CKey pubKey;
		return pubKey.SetPubKey(getValueFieldVL(sfSigningKey)) && 
			pubKey.Verify(getSigningHash(), mSignature.peekValue());
	}
	catch (...)
	{
		return false;
	}
}

NewcoinAddress SerializedValidation::getSignerPublic() const
{
	NewcoinAddress a;
	a.setNodePublic(getValueFieldVL(sfSigningKey));
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
