#ifndef __VALIDATION__
#define __VALIDATION__

#include "SerializedObject.h"

class SerializedValidation : public STObject
{
protected:
	STVariableLength mSignature;

public:
	typedef boost::shared_ptr<SerializedValidation> pointer;

	static SOElement	sValidationFormat[16];
	static const uint32	sFullFlag;

	// These throw if the object is not valid
	SerializedValidation(SerializerIterator& sit, bool checkSignature = true);
	SerializedValidation(const Serializer& s, bool checkSignature = true);

	SerializedValidation(const uint256& ledgerHash, CKey::pointer nodeKey, bool isFull);

	uint256			getLedgerHash()		const;
	NewcoinAddress  getSignerPublic()	const;
	bool			isValid()			const;
	bool			isFull()			const;
	CKey::pointer	getSigningKey()		const;
	uint256			getSigningHash()	const;

	void						addSigned(Serializer&)		const;
	void						addSignature(Serializer&)	const;
	std::vector<unsigned char>	getSigned()					const;
	std::vector<unsigned char>	getSignature()				const;
};

#endif
