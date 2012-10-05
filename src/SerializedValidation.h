#ifndef __VALIDATION__
#define __VALIDATION__

#include "SerializedObject.h"
#include "NewcoinAddress.h"

class SerializedValidation : public STObject
{
protected:
	uint256	mPreviousHash;
	bool mTrusted;

	void setNode();

public:
	typedef boost::shared_ptr<SerializedValidation>			pointer;
	typedef const boost::shared_ptr<SerializedValidation>&	ref;

	static const uint32	sFullFlag;

	// These throw if the object is not valid
	SerializedValidation(SerializerIterator& sit, bool checkSignature = true);
	SerializedValidation(const uint256& ledgerHash, uint32 signTime, const NewcoinAddress& naSeed, bool isFull);

	uint256			getLedgerHash()		const;
	uint32			getSignTime()		const;
	uint32			getFlags()			const;
	NewcoinAddress  getSignerPublic()	const;
	bool			isValid()			const;
	bool			isFull()			const;
	bool			isTrusted()			const	{ return mTrusted; }
	uint256			getSigningHash()	const;
	bool			isValid(const uint256&) const;

	void 						setTrusted()				{ mTrusted = true; }
	std::vector<unsigned char>	getSigned()					const;
	std::vector<unsigned char>	getSignature()				const;

	// The validation this replaced
	const uint256& getPreviousHash()		{ return mPreviousHash; }
	bool isPreviousHash(const uint256& h)	{ return mPreviousHash == h; }
	void setPreviousHash(const uint256& h)	{ mPreviousHash = h; }
};

#endif
// vim:ts=4
