
#include "SerializedTransaction.h"

#include <boost/foreach.hpp>

#include "Application.h"
#include "Log.h"
#include "HashPrefixes.h"

SerializedTransaction::SerializedTransaction(TransactionType type) : STObject(sfTransaction), mType(type)
{
	mFormat = getTxnFormat(type);
	if (mFormat == NULL)
		throw std::runtime_error("invalid transaction type");
	set(mFormat->elements);
	setValueFieldU16(sfTransactionType, mFormat->t_type);
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
	mType = static_cast<TransactionType>(getValueFieldU16(sfTransactionType));

	mFormat = getTxnFormat(mType);
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
		return getValueFieldVL(sfTxnSignature);
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
	setValueFieldVL(sfTxnSignature, signature);
}

bool SerializedTransaction::checkSign(const NewcoinAddress& naAccountPublic) const
{
	try
	{
		return naAccountPublic.accountPublicVerify(getSigningHash(), getValueFieldVL(sfTxnSignature));
	}
	catch (...)
	{
		return false;
	}
}

void SerializedTransaction::setSigningPubKey(const NewcoinAddress& naSignPubKey)
{
	setValueFieldVL(sfSigningPubKey, naSignPubKey.getAccountPublic());
}

void SerializedTransaction::setSourceAccount(const NewcoinAddress& naSource)
{
	setValueFieldAccount(sfAccount, naSource);
}

Json::Value SerializedTransaction::getJson(int options) const
{
	Json::Value ret = STObject::getJson(0);
	ret["id"] = getTransactionID().GetHex();
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


// vim:ts=4
