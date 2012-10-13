
#include "SerializedTransaction.h"

#include <boost/foreach.hpp>
#include <boost/test/unit_test.hpp>

#include "Application.h"
#include "Log.h"
#include "HashPrefixes.h"

SerializedTransaction::SerializedTransaction(TransactionType type) : STObject(sfTransaction), mType(type)
{
	mFormat = TransactionFormat::getTxnFormat(type);
	if (mFormat == NULL)
		throw std::runtime_error("invalid transaction type");
	set(mFormat->elements);
	setFieldU16(sfTransactionType, mFormat->t_type);
}

SerializedTransaction::SerializedTransaction(const STObject& object) : STObject(object)
{
	mType = static_cast<TransactionType>(getFieldU16(sfTransactionType));
	mFormat = TransactionFormat::getTxnFormat(mType);
	if (!mFormat)
		throw std::runtime_error("invalid transaction type");
	if (!setType(mFormat->elements))
	{
		assert(false);
		throw std::runtime_error("transaction not valid");
	}
}

SerializedTransaction::SerializedTransaction(SerializerIterator& sit) : STObject(sfTransaction)
{
	int length = sit.getBytesLeft();
	if ((length < TransactionMinLen) || (length > TransactionMaxLen))
	{
		Log(lsERROR) << "Transaction has invalid length: " << length;
		throw std::runtime_error("Transaction length invalid");
	}

	set(sit);
	mType = static_cast<TransactionType>(getFieldU16(sfTransactionType));

	mFormat = TransactionFormat::getTxnFormat(mType);
	if (!mFormat)
		throw std::runtime_error("invalid transaction type");
	if (!setType(mFormat->elements))
	{
		assert(false);
		throw std::runtime_error("transaction not valid");
	}
}

std::string SerializedTransaction::getFullText() const
{
	std::string ret = "\"";
	ret += getTransactionID().GetHex();
	ret += "\" = {";
	ret += STObject::getFullText();
	ret += "}";
	return ret;
}

std::string SerializedTransaction::getText() const
{
	return STObject::getText();
}

std::vector<NewcoinAddress> SerializedTransaction::getAffectedAccounts() const
{
	std::vector<NewcoinAddress> accounts;

	BOOST_FOREACH(const SerializedType& it, peekData())
	{
		const STAccount* sa = dynamic_cast<const STAccount*>(&it);
		if (sa != NULL)
		{
			bool found = false;
			NewcoinAddress na = sa->getValueNCA();
			for (std::vector<NewcoinAddress>::iterator it = accounts.begin(), end = accounts.end();
				it != end; ++it)
			{
				if (*it == na)
				{
					found = true;
					break;
				}
			}
			if (!found)
				accounts.push_back(na);
		}
	}
	return accounts;
}

uint256 SerializedTransaction::getSigningHash() const
{
	return STObject::getSigningHash(sHP_TransactionSign);
}

uint256 SerializedTransaction::getTransactionID() const
{ // perhaps we should cache this
	return getHash(sHP_TransactionID);
}

std::vector<unsigned char> SerializedTransaction::getSignature() const
{
	try
	{
		return getFieldVL(sfTxnSignature);
	}
	catch (...)
	{
		return std::vector<unsigned char>();
	}
}

void SerializedTransaction::sign(const NewcoinAddress& naAccountPrivate)
{
	std::vector<unsigned char> signature;
	naAccountPrivate.accountPrivateSign(getSigningHash(), signature);
	setFieldVL(sfTxnSignature, signature);
}

bool SerializedTransaction::checkSign() const
{
	try
	{
		NewcoinAddress n;
		n.setAccountPublic(getFieldVL(sfSigningPubKey));
		return checkSign(n);
	}
	catch (...)
	{
		return false;
	}
}

bool SerializedTransaction::checkSign(const NewcoinAddress& naAccountPublic) const
{
	try
	{
		return naAccountPublic.accountPublicVerify(getSigningHash(), getFieldVL(sfTxnSignature));
	}
	catch (...)
	{
		return false;
	}
}

void SerializedTransaction::setSigningPubKey(const NewcoinAddress& naSignPubKey)
{
	setFieldVL(sfSigningPubKey, naSignPubKey.getAccountPublic());
}

void SerializedTransaction::setSourceAccount(const NewcoinAddress& naSource)
{
	setFieldAccount(sfAccount, naSource);
}

Json::Value SerializedTransaction::getJson(int options) const
{
	Json::Value ret = STObject::getJson(0);

	ret["hash"] = getTransactionID().GetHex();

	return ret;
}

std::string SerializedTransaction::getSQLValueHeader()
{
	return "(TransID, TransType, FromAcct, FromSeq, LedgerSeq, Status, RawTxn)";
}

std::string SerializedTransaction::getSQLInsertHeader()
{
	return "INSERT INTO Transactions " + getSQLValueHeader() + " VALUES ";
}

std::string SerializedTransaction::getSQL(uint32 inLedger, char status) const
{
	Serializer s;
	add(s);
	return getSQL(s, inLedger, status);
}

std::string SerializedTransaction::getSQL(Serializer rawTxn, uint32 inLedger, char status) const
{
	std::string rTxn;
	theApp->getTxnDB()->getDB()->escape(
		reinterpret_cast<const unsigned char *>(rawTxn.getDataPtr()), rawTxn.getLength(), rTxn);
	return str(boost::format("('%s', '%s', '%s', '%d', '%d', '%c', %s)")
		% getTransactionID().GetHex() % getTransactionType() % getSourceAccount().humanAccountID()
		% getSequence() % inLedger % status % rTxn);
}


BOOST_AUTO_TEST_SUITE(SerializedTransactionTS)

BOOST_AUTO_TEST_CASE( STrans_test )
{
	NewcoinAddress seed;
	seed.setSeedRandom();
	NewcoinAddress generator = NewcoinAddress::createGeneratorPublic(seed);
	NewcoinAddress  publicAcct = NewcoinAddress::createAccountPublic(generator, 1);
	NewcoinAddress  privateAcct = NewcoinAddress::createAccountPrivate(generator, seed, 1);

	SerializedTransaction j(ttCLAIM);
	j.setSourceAccount(publicAcct);
	j.setSigningPubKey(publicAcct);
	j.setFieldVL(sfPublicKey, publicAcct.getAccountPublic());
	j.sign(privateAcct);

	if (!j.checkSign()) BOOST_FAIL("Transaction fails signature test");

	Serializer rawTxn;
	j.add(rawTxn);
	SerializerIterator sit(rawTxn);
	SerializedTransaction copy(sit);
	if (copy != j)
	{
		Log(lsFATAL) << j.getJson(0);
		Log(lsFATAL) << copy.getJson(0);
		BOOST_FAIL("Transaction fails serialize/deserialize test");
	}
	Log(lsINFO) << "ORIG: " << j.getJson(0);
	std::auto_ptr<STObject> new_obj = STObject::parseJson(j.getJson(0), sfGeneric);
	if (new_obj.get() == NULL) BOOST_FAIL("Unable to build object from json");
	Log(lsINFO) << "BUILT " << new_obj->getJson(0);
}

BOOST_AUTO_TEST_SUITE_END();

// vim:ts=4
