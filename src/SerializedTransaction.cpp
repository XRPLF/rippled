
#include "SerializedTransaction.h"
#include "Application.h"

#include "Log.h"

SerializedTransaction::SerializedTransaction(TransactionType type) : mType(type)
{
	mFormat = getTxnFormat(type);
	if (mFormat == NULL) throw std::runtime_error("invalid transaction type");

	mMiddleTxn.giveObject(new STVariableLength("SigningPubKey"));
	mMiddleTxn.giveObject(new STAccount("SourceAccount"));
	mMiddleTxn.giveObject(new STUInt32("Sequence"));
	mMiddleTxn.giveObject(new STUInt16("Type", static_cast<uint16>(type)));
	mMiddleTxn.giveObject(new STAmount("Fee"));

	mInnerTxn = STObject(mFormat->elements, "InnerTransaction");
}

SerializedTransaction::SerializedTransaction(SerializerIterator& sit)
{
	int length = sit.getBytesLeft();
	if ((length < TransactionMinLen) || (length > TransactionMaxLen))
	{
		Log(lsERROR) << "Transaction has invalid length: " << length;
		throw std::runtime_error("Transaction length invalid");
	}

	mSignature.setValue(sit.getVL());

	mMiddleTxn.giveObject(new STVariableLength("SigningPubKey", sit.getVL()));

	STAccount sa("SourceAccount", sit.getVL());
	mSourceAccount = sa.getValueNCA();
	mMiddleTxn.giveObject(new STAccount(sa));

	mMiddleTxn.giveObject(new STUInt32("Sequence", sit.get32()));

	mType = static_cast<TransactionType>(sit.get16());
	mMiddleTxn.giveObject(new STUInt16("Type", static_cast<uint16>(mType)));
	mFormat = getTxnFormat(mType);
	if (!mFormat)
	{
		Log(lsERROR) << "Transaction has invalid type";
		throw std::runtime_error("Transaction has invalid type");
	}
	mMiddleTxn.giveObject(STAmount::deserialize(sit, "Fee"));

	mInnerTxn = STObject(mFormat->elements, sit, "InnerTransaction");
}

int SerializedTransaction::getLength() const
{
	return mSignature.getLength() + mMiddleTxn.getLength() + mInnerTxn.getLength();
}

std::string SerializedTransaction::getFullText() const
{
	std::string ret = "\"";
	ret += getTransactionID().GetHex();
	ret += "\" = {";
	ret += mSignature.getFullText();
	ret += mMiddleTxn.getFullText();
	ret += mInnerTxn.getFullText();
	ret += "}";
	return ret;
}

std::string SerializedTransaction::getText() const
{
	std::string ret = "{";
	ret += mSignature.getText();
	ret += mMiddleTxn.getText();
	ret += mInnerTxn.getText();
	ret += "}";
	return ret;
}

std::vector<NewcoinAddress> SerializedTransaction::getAffectedAccounts() const
{
	std::vector<NewcoinAddress> accounts;
	accounts.push_back(mSourceAccount);

	for(boost::ptr_vector<SerializedType>::const_iterator it = mInnerTxn.peekData().begin(),
		end = mInnerTxn.peekData().end(); it != end ; ++it)
	{
		const STAccount* sa = dynamic_cast<const STAccount*>(&*it);
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

void SerializedTransaction::add(Serializer& s) const
{
	mSignature.add(s);
	mMiddleTxn.add(s);
	mInnerTxn.add(s);
}

bool SerializedTransaction::isEquivalent(const SerializedType& t) const
{ // Signatures are not compared
	const SerializedTransaction* v = dynamic_cast<const SerializedTransaction*>(&t);
	if (!v) return false;
	if (mType != v->mType) return false;
	if (mMiddleTxn != v->mMiddleTxn) return false;
	if (mInnerTxn != v->mInnerTxn) return false;
	return true;
}

uint256 SerializedTransaction::getSigningHash() const
{
	Serializer s;
	s.add32(TransactionMagic);
	mMiddleTxn.add(s);
	mInnerTxn.add(s);
	return s.getSHA512Half();
}

uint256 SerializedTransaction::getTransactionID() const
{ // perhaps we should cache this
	Serializer s;
	mSignature.add(s);
	mMiddleTxn.add(s);
	mInnerTxn.add(s);
	return s.getSHA512Half();
}

std::vector<unsigned char> SerializedTransaction::getSignature() const
{
	return mSignature.getValue();
}

const std::vector<unsigned char>& SerializedTransaction::peekSignature() const
{
	return mSignature.peekValue();
}

bool SerializedTransaction::sign(const NewcoinAddress& naAccountPrivate)
{
	return naAccountPrivate.accountPrivateSign(getSigningHash(), mSignature.peekValue());
}

bool SerializedTransaction::checkSign(const NewcoinAddress& naAccountPublic) const
{
	return naAccountPublic.accountPublicVerify(getSigningHash(), mSignature.getValue());
}

void SerializedTransaction::setSignature(const std::vector<unsigned char>& sig)
{
	mSignature.setValue(sig);
}

STAmount SerializedTransaction::getTransactionFee() const
{
	const STAmount* v = dynamic_cast<const STAmount*>(mMiddleTxn.peekAtPIndex(TransactionIFee));
	if (!v) throw std::runtime_error("corrupt transaction");
	return *v;
}

void SerializedTransaction::setTransactionFee(const STAmount& fee)
{
	STAmount* v = dynamic_cast<STAmount*>(mMiddleTxn.getPIndex(TransactionIFee));
	if (!v) throw std::runtime_error("corrupt transaction");
	v->setValue(fee);
}

uint32 SerializedTransaction::getSequence() const
{
	const STUInt32* v = dynamic_cast<const STUInt32*>(mMiddleTxn.peekAtPIndex(TransactionISequence));
	if (!v) throw std::runtime_error("corrupt transaction");
	return v->getValue();
}

void SerializedTransaction::setSequence(uint32 seq)
{
	STUInt32* v = dynamic_cast<STUInt32*>(mMiddleTxn.getPIndex(TransactionISequence));
	if (!v) throw std::runtime_error("corrupt transaction");
	v->setValue(seq);
}

std::vector<unsigned char> SerializedTransaction::getSigningPubKey() const
{
	const STVariableLength* v =
		dynamic_cast<const STVariableLength*>(mMiddleTxn.peekAtPIndex(TransactionISigningPubKey));
	if (!v) throw std::runtime_error("corrupt transaction");
	return v->getValue();
}

const std::vector<unsigned char>& SerializedTransaction::peekSigningPubKey() const
{
	const STVariableLength* v=
		dynamic_cast<const STVariableLength*>(mMiddleTxn.peekAtPIndex(TransactionISigningPubKey));
	if (!v) throw std::runtime_error("corrupt transaction");
	return v->peekValue();
}

std::vector<unsigned char>& SerializedTransaction::peekSigningPubKey()
{
	STVariableLength* v = dynamic_cast<STVariableLength*>(mMiddleTxn.getPIndex(TransactionISigningPubKey));
	if (!v) throw std::runtime_error("corrupt transaction");
	return v->peekValue();
}

const NewcoinAddress& SerializedTransaction::setSigningPubKey(const NewcoinAddress& naSignPubKey)
{
	mSignPubKey	= naSignPubKey;

	STVariableLength* v = dynamic_cast<STVariableLength*>(mMiddleTxn.getPIndex(TransactionISigningPubKey));
	if (!v) throw std::runtime_error("corrupt transaction");
	v->setValue(mSignPubKey.getAccountPublic());

	return mSignPubKey;
}

const NewcoinAddress& SerializedTransaction::setSourceAccount(const NewcoinAddress& naSource)
{
	mSourceAccount	= naSource;

	STAccount* v = dynamic_cast<STAccount*>(mMiddleTxn.getPIndex(TransactionISourceID));
	if (!v) throw std::runtime_error("corrupt transaction");
	v->setValueNCA(mSourceAccount);
	return mSourceAccount;
}

uint160 SerializedTransaction::getITFieldAccount(SOE_Field field) const
{
	uint160 r;
	const SerializedType* st = mInnerTxn.peekAtPField(field);
	if (!st) return r;

	const STAccount* ac = dynamic_cast<const STAccount*>(st);
	if (!ac) return r;
	ac->getValueH160(r);
	return r;
}

int SerializedTransaction::getITFieldIndex(SOE_Field field) const
{
	return mInnerTxn.getFieldIndex(field);
}

int SerializedTransaction::getITFieldCount() const
{
	return mInnerTxn.getCount();
}

bool SerializedTransaction::getITFieldPresent(SOE_Field field) const
{
	return mInnerTxn.isFieldPresent(field);
}

const SerializedType& SerializedTransaction::peekITField(SOE_Field field) const
{
	return mInnerTxn.peekAtField(field);
}

SerializedType& SerializedTransaction::getITField(SOE_Field field)
{
	return mInnerTxn.getField(field);
}

void SerializedTransaction::makeITFieldPresent(SOE_Field field)
{
	mInnerTxn.makeFieldPresent(field);
}

void SerializedTransaction::makeITFieldAbsent(SOE_Field field)
{
	return mInnerTxn.makeFieldAbsent(field);
}

Json::Value SerializedTransaction::getJson(int options) const
{
	Json::Value ret = Json::objectValue;
	ret["id"] = getTransactionID().GetHex();
	ret["signature"] = mSignature.getText();

	Json::Value middle = mMiddleTxn.getJson(options);
	middle["type"] = mFormat->t_name;
	ret["middle"] = middle;

	ret["inner"] = mInnerTxn.getJson(options);
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
