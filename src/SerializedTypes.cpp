
#include <boost/lexical_cast.hpp>

#include "SerializedTypes.h"
#include "SerializedObject.h"
#include "TransactionFormats.h"
#include "NewcoinAddress.h"

std::string SerializedType::getFullText() const
{
	std::string ret;
	if(getSType()!=STI_NOTPRESENT)
	{
		if(name!=NULL)
		{
			ret=name;
			ret+=" = ";
		}
		ret+=getText();
	}
	return ret;
}

STUInt8* STUInt8::construct(SerializerIterator& u, const char *name)
{
	return new STUInt8(name, u.get8());
}

std::string STUInt8::getText() const
{
	return boost::lexical_cast<std::string>(value);
}

bool STUInt8::isEquivalent(const SerializedType& t) const
{
	const STUInt8* v=dynamic_cast<const STUInt8*>(&t);
	return v && (value==v->value);
}

STUInt16* STUInt16::construct(SerializerIterator& u, const char *name)
{
	return new STUInt16(name, u.get16());
}

std::string STUInt16::getText() const
{
	return boost::lexical_cast<std::string>(value);
}

bool STUInt16::isEquivalent(const SerializedType& t) const
{
	const STUInt16* v=dynamic_cast<const STUInt16*>(&t);
	return v && (value==v->value);
}

STUInt32* STUInt32::construct(SerializerIterator& u, const char *name)
 {
	return new STUInt32(name, u.get32());
}

std::string STUInt32::getText() const
{
	return boost::lexical_cast<std::string>(value);
}

bool STUInt32::isEquivalent(const SerializedType& t) const
{
	const STUInt32* v=dynamic_cast<const STUInt32*>(&t);
	return v && (value==v->value);
}

STUInt64* STUInt64::construct(SerializerIterator& u, const char *name)
{
	return new STUInt64(name, u.get64());
}

std::string STUInt64::getText() const
{
	return boost::lexical_cast<std::string>(value);
}

bool STUInt64::isEquivalent(const SerializedType& t) const
{
	const STUInt64* v=dynamic_cast<const STUInt64*>(&t);
	return v && (value==v->value);
}

STHash128* STHash128::construct(SerializerIterator& u, const char *name)
{
	return new STHash128(name, u.get128());
}

std::string STHash128::getText() const
{
	return value.GetHex();
}

bool STHash128::isEquivalent(const SerializedType& t) const
{
	const STHash128* v=dynamic_cast<const STHash128*>(&t);
	return v && (value==v->value);
}

STHash160* STHash160::construct(SerializerIterator& u, const char *name)
{
	return new STHash160(name, u.get160());
}

std::string STHash160::getText() const
{
	return value.GetHex();
}

bool STHash160::isEquivalent(const SerializedType& t) const
{
	const STHash160* v=dynamic_cast<const STHash160*>(&t);
	return v && (value==v->value);
}

STHash256* STHash256::construct(SerializerIterator& u, const char *name)
{
	return new STHash256(name, u.get256());
}

std::string STHash256::getText() const
{
	return value.GetHex();
}

bool STHash256::isEquivalent(const SerializedType& t) const
{
	const STHash256* v=dynamic_cast<const STHash256*>(&t);
	return v && (value==v->value);
}

static std::string hex(const std::vector<unsigned char>& value)
{
	int dlen=value.size(), i=0;
	char psz[dlen*2 + 1];
	for(std::vector<unsigned char>::const_iterator it=value.begin(), end=value.end(); it!=end; ++it)
		sprintf(psz + 2*(i++), "%02X", *it);
	return std::string(psz, psz + value.size()*2);
}

std::string STVariableLength::getText() const
{
	return hex(value);
}

STVariableLength* STVariableLength::construct(SerializerIterator& u, const char *name)
{
	return new STVariableLength(u.getVL());
}

int STVariableLength::getLength() const
{
	return Serializer::encodeLengthLength(value.size()) + value.size();
}

bool STVariableLength::isEquivalent(const SerializedType& t) const
{
	const STVariableLength* v=dynamic_cast<const STVariableLength*>(&t);
	return v && (value==v->value);
}

std::string STAccount::getText() const
{
	uint160 u;
	NewcoinAddress a;
	if(!getValueH160(u)) return STVariableLength::getText();
	a.setAccountID(u);
	return a.humanAccountPublic();
}

STAccount* STAccount::construct(SerializerIterator& u, const char *name)
{
	STAccount *ret=new STAccount(u.getVL());
	if(!ret->isValueH160())
	{
		delete ret;
		throw(std::runtime_error("invalid account in transaction"));
	}
	return ret;
}

bool STAccount::isValueH160() const
{
	return peekValue().size() == (160/8);
}

void STAccount::setValueH160(const uint160& v)
{
	peekValue().empty();
	peekValue().insert(peekValue().end(), v.begin(), v.end());
}

bool STAccount::getValueH160(uint160& v) const
{
	if(!isValueH160()) return false;
	memcpy(v.begin(), &(peekValue().front()), 32);
	return true;
}

std::string STTaggedList::getText() const
{
	std::string ret;
	for(std::vector<TaggedListItem>::const_iterator it=value.begin(); it!=value.end(); ++it)
	{
		ret+=boost::lexical_cast<std::string>(it->first);
		ret+=",";
		ret+=hex(it->second);
	}
	return ret;
}

STTaggedList* STTaggedList::construct(SerializerIterator& u, const char *name)
{
	return new STTaggedList(name, u.getTaggedList());
}

int STTaggedList::getLength() const
{
	int ret=Serializer::getTaggedListLength(value);
	if(ret<0) throw(std::overflow_error("bad TL length"));
	return ret;
}

bool STTaggedList::isEquivalent(const SerializedType& t) const
{
	const STTaggedList* v=dynamic_cast<const STTaggedList*>(&t);
	return v && (value==v->value);
}
