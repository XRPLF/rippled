#ifndef __SERIALIZEDTRANSACTION__
#define __SERIALIZEDTRANSACTION__

#include <vector>

#include <boost/shared_ptr.hpp>

#include "SerializedObject.h"
#include "TransactionFormats.h"
#include "RippleAddress.h"
#include "InstanceCounter.h"

#define TXN_SQL_NEW			'N'
#define TXN_SQL_CONFLICT	'C'
#define TXN_SQL_HELD		'H'
#define TXN_SQL_VALIDATED	'V'
#define TXN_SQL_INCLUDED	'I'
#define TXN_SQL_UNKNOWN		'U'

DEFINE_INSTANCE(SerializedTransaction);

class SerializedTransaction : public STObject, private IS_INSTANCE(SerializedTransaction)
{
public:
	typedef boost::shared_ptr<SerializedTransaction>		pointer;
	typedef const boost::shared_ptr<SerializedTransaction>&	ref;

protected:
	TransactionType mType;
	const TransactionFormat* mFormat;

	SerializedTransaction* duplicate() const { return new SerializedTransaction(*this); }

	mutable bool mSigGood, mSigBad;

public:
	SerializedTransaction(SerializerIterator& sit);
	SerializedTransaction(TransactionType type);
	SerializedTransaction(const STObject &object);

	// STObject functions
	SerializedTypeID getSType() const { return STI_TRANSACTION; }
	std::string getFullText() const;
	std::string getText() const;

	// outer transaction functions / signature functions
	std::vector<unsigned char> getSignature() const;
	void setSignature(const std::vector<unsigned char>& s)	{ setFieldVL(sfTxnSignature, s); }
	uint256 getSigningHash() const;

	TransactionType getTxnType() const			{ return mType; }
	STAmount getTransactionFee() const			{ return getFieldAmount(sfFee); }
	void setTransactionFee(const STAmount& fee)	{ setFieldAmount(sfFee, fee); }

	RippleAddress getSourceAccount() const		{ return getFieldAccount(sfAccount); }
	std::vector<unsigned char> getSigningPubKey() const { return getFieldVL(sfSigningPubKey); }
	void setSigningPubKey(const RippleAddress& naSignPubKey);
	void setSourceAccount(const RippleAddress& naSource);
	std::string getTransactionType() const { return mFormat->t_name; }

	uint32 getSequence() const		{ return getFieldU32(sfSequence); }
	void setSequence(uint32 seq)	{ return setFieldU32(sfSequence, seq); }

	std::vector<RippleAddress> getMentionedAccounts() const;

	uint256 getTransactionID() const;

	virtual Json::Value getJson(int options, bool binary = false) const;

	void sign(const RippleAddress& naAccountPrivate);
	bool checkSign(const RippleAddress& naAccountPublic) const;
	bool checkSign() const;
	bool isKnownGood() const 	{ return mSigGood; }
	bool isKnownBad() const		{ return mSigBad; }
	void setGood() const		{ mSigGood = true; }
	void setBad() const			{ mSigBad = true; }

	// SQL Functions
	static std::string getSQLValueHeader();
	static std::string getSQLInsertHeader();
	static std::string getSQLInsertIgnoreHeader();
	static std::string getSQLInsertReplaceHeader();
	std::string getSQL(std::string& sql, uint32 inLedger, char status) const;
	std::string getSQL(uint32 inLedger, char status) const;
	std::string getSQL(Serializer rawTxn, uint32 inLedger, char status) const;

	// SQL Functions with metadata
	static std::string getMetaSQLValueHeader();
	static std::string getMetaSQLInsertHeader();
	static std::string getMetaSQLInsertReplaceHeader();
	std::string getMetaSQL(uint32 inLedger, const std::string& escapedMetaData) const;
	std::string getMetaSQL(Serializer rawTxn, uint32 inLedger, char status, const std::string& escapedMetaData) const;

};

#endif
// vim:ts=4
