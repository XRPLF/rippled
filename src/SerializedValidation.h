#ifndef __VALIDATION__
#define __VALIDATION__

#include "SerializedObject.h"
#include "NewcoinAddress.h"

class SerializedValidation : public STObject
{
protected:
	STVariableLength mSignature;
	bool mTrusted;

	void setNode();

public:
	typedef boost::shared_ptr<SerializedValidation> pointer;

	static SOElement	sValidationFormat[16];
	static const uint32	sFullFlag;

	// These throw if the object is not valid
	SerializedValidation(SerializerIterator& sit, bool checkSignature = true);
	SerializedValidation(const Serializer& s, bool checkSignature = true);

	SerializedValidation(const uint256& ledgerHash, uint32 closeTime, const NewcoinAddress& naSeed, bool isFull);

	uint256			getLedgerHash()		const;
	uint32			getCloseTime()		const;
	uint32			getFlags()			const;
	NewcoinAddress  getSignerPublic()	const;
	bool			isValid()			const;
	bool			isFull()			const;
	bool			isTrusted()			const	{ return mTrusted; }
	uint256			getSigningHash()	const;
	bool			isValid(const uint256&) const;

	void 						setTrusted()				{ mTrusted = true; }
	void						addSigned(Serializer&)		const;
	void						addSignature(Serializer&)	const;
	std::vector<unsigned char>	getSigned()					const;
	std::vector<unsigned char>	getSignature()				const;
};

#endif
// vim:ts=4
