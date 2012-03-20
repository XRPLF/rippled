
#include "SerializedTransaction.h"

SerializedTransaction::SerializedTransaction(TransactionType type)
{
	mFormat=getFormat(type);
	if(mFormat==NULL) throw(std::runtime_error("invalid transaction type"));

	mMiddleTxn.giveObject(new STUInt32("Magic", TransactionMagic));
	mMiddleTxn.giveObject(new STVariableLength("Signature")); // signature
	mMiddleTxn.giveObject(new STUInt8("Type", static_cast<unsigned char>(type)));

	SOElement* elem=mFormat->elements;
	while(elem->e_id!=STI_DONE)
	{
		if( (elem->e_type==SOE_IFFLAG) || (elem->e_type==SOE_IFNFLAG) )
			mInnerTxn.giveObject(new STUObject(elem->e_name));
		else switch(elem->e_id)
		{
			case STI_UINT16:
				mInnerTxn.giveObject(new STUInt16(elem->e_name));
				break;
			case STI_UINT32:
				mInnerTxn.giveObject(new STUInt32(elem->e_name));
				break;
			case STI_UINT64:
				mInnerTxn.giveObject(new STUInt64(elem->e_name));
			case STI_HASH160:
			case STI_HASH256:
			case STI_VL:
				mInnerTxn.giveObject(new STVariableLength(elem->e_name));
				break;
			case STI_TL:
				mInnerTxn.giveObject(new STTaggedList(elem->e_name));
				break;
#if 0
			case STI_ACCOUNT: // CHECKME: Should an account be variable length?
				mInnerTxn.giveObject(new STVariableLength(elem->e_name));
				break;
#endif
			default: throw(std::runtime_error("invalid transaction element"));
		}
		elem++;
	}
}

SerializedTransaction::SerializedTransaction(SerializerIterator& sit, int length)
{
	if(length==0) length=sit.get32();
	if( (length<TransactionMinLen) || (length>TransactionMaxLen) )
		throw(std::runtime_error("Transaction length invalid"));

	mSignature.setValue(sit.getVL());

	if(sit.get32()!=TransactionMagic)
		throw(std::runtime_error("Transaction has invalid magic"));

	mMiddleTxn.giveObject(new STUInt32("Magic", TransactionMagic));
	mMiddleTxn.giveObject(new STVariableLength("Signature", sit.getVL()));
	mMiddleTxn.giveObject(new STUInt32("Type", sit.get32()));

	// WRITEME
}

int SerializedTransaction::getLength() const
{
	return mSignature.getLength() + mMiddleTxn.getLength() + mInnerTxn.getLength();
}

std::string SerializedTransaction::getFullText() const
{ // WRITEME: Add transaction ID
	std::string ret="{";
	ret+=mSignature.getFullText();
	ret+=mMiddleTxn.getFullText();
	ret+=mInnerTxn.getFullText();
	ret+="}";
	return ret;
}

std::string SerializedTransaction::getText() const
{
	std::string ret="{";
	ret+=mSignature.getText();
	ret+=mMiddleTxn.getText();
	ret+=mInnerTxn.getText();
	return ret;
}

void SerializedTransaction::add(Serializer& s) const
{
}
