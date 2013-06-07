#ifndef __VALIDATION__
#define __VALIDATION__

#include "SerializedObject.h"

DEFINE_INSTANCE(SerializedValidation);

class SerializedValidation : public STObject, private IS_INSTANCE(SerializedValidation)
{
public:
	typedef boost::shared_ptr<SerializedValidation>			pointer;
	typedef const boost::shared_ptr<SerializedValidation>&	ref;

	static const uint32	sFullFlag;

	// These throw if the object is not valid
	SerializedValidation(SerializerIterator& sit, bool checkSignature = true);

	// Does not sign the validation
	SerializedValidation(const uint256& ledgerHash, uint32 signTime, const RippleAddress& raPub, bool isFull);

	uint256			getLedgerHash()		const;
	uint32			getSignTime()		const;
	uint32			getFlags()			const;
	RippleAddress	getSignerPublic()	const;
	uint160			getNodeID()			const	{ return mNodeID; }
	bool			isValid()			const;
	bool			isFull()			const;
	bool			isTrusted()			const	{ return mTrusted; }
	uint256			getSigningHash()	const;
	bool			isValid(const uint256&) const;

	void 						setTrusted()				{ mTrusted = true; }
	std::vector<unsigned char>	getSigned()					const;
	std::vector<unsigned char>	getSignature()				const;
	void sign(uint256& signingHash, const RippleAddress& raPrivate);
	void sign(const RippleAddress& raPrivate);

	// The validation this replaced
	const uint256& getPreviousHash()		{ return mPreviousHash; }
	bool isPreviousHash(const uint256& h)	{ return mPreviousHash == h; }
	void setPreviousHash(const uint256& h)	{ mPreviousHash = h; }

private:
	uint256	mPreviousHash;
	uint160 mNodeID;
	bool mTrusted;

	void setNode();
};

#endif
// vim:ts=4
