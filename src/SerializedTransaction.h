#ifndef __SERIALIZEDTRANSACTION__
#define __SERIALIZEDTRANSACTION__

#include <vector>

#include <boost/shared_ptr.hpp>

#include "uint256.h"
#include "SerializedObject.h"
#include "TransactionFormats.h"
#include "NewcoinAddress.h"

#define TXN_SQL_NEW			'N'
#define TXN_SQL_CONFLICT	'C'
#define TXN_SQL_HELD		'H'
#define TXN_SQL_VALIDATED	'V'
#define TXN_SQL_INCLUDED	'I'
#define TXN_SQL_UNKNOWN		'U'

class SerializedTransaction : public STObject
{
public:
	typedef boost::shared_ptr<SerializedTransaction> pointer;

protected:
	TransactionType mType;
	const TransactionFormat* mFormat;

	SerializedTransaction* duplicate() const { return new SerializedTransaction(*this); }

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

	NewcoinAddress getSourceAccount() const		{ return getFieldAccount(sfAccount); }
	std::vector<unsigned char> getSigningPubKey() const { return getFieldVL(sfSigningPubKey); }
	void setSigningPubKey(const NewcoinAddress& naSignPubKey);
	void setSourceAccount(const NewcoinAddress& naSource);
	std::string getTransactionType() const { return mFormat->t_name; }

	uint32 getSequence() const		{ return getFieldU32(sfSequence); }
	void setSequence(uint32 seq)	{ return setFieldU32(sfSequence, seq); }

	std::vector<NewcoinAddress> getAffectedAccounts() const;

	uint256 getTransactionID() const;

	virtual Json::Value getJson(int options) const;

	void sign(const NewcoinAddress& naAccountPrivate);
	bool checkSign(const NewcoinAddress& naAccountPublic) const;
	bool checkSign() const;

	// SQL Functions
	static std::string getSQLValueHeader();
	static std::string getSQLInsertHeader();
	std::string getSQL(std::string& sql, uint32 inLedger, char status) const;
	std::string getSQL(uint32 inLedger, char status) const;
	std::string getSQL(Serializer rawTxn, uint32 inLedger, char status) const;

};

#endif
// vim:ts=4
