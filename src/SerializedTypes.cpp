
#include <boost/lexical_cast.hpp>

#include "SerializedTypes.h"
#include "SerializedObject.h"
#include "TransactionFormats.h"

std::string SerializedType::getFullText() const
{
	std::string ret;
	if(getType()!=STI_NOTPRESENT)
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

STUInt8* STUInt8::construct(SerializerIterator& u)
{
	return new STUInt8(u.get8());
}

std::string STUInt8::getText() const
{
	return boost::lexical_cast<std::string>(value);
}

STUInt16* STUInt16::construct(SerializerIterator& u)
{
	return new STUInt16(u.get16());
}

std::string STUInt16::getText() const
{
	return boost::lexical_cast<std::string>(value);
}

STUInt32* STUInt32::construct(SerializerIterator& u)
{
	return new STUInt32(u.get32());
}

std::string STUInt32::getText() const
{
	return boost::lexical_cast<std::string>(value);
}

STUInt64* STUInt64::construct(SerializerIterator& u)
{
	return new STUInt64(u.get64());
}

std::string STUInt64::getText() const
{
	return boost::lexical_cast<std::string>(value);
}

STUHash160* STUHash160::construct(SerializerIterator& u)
{
	return new STUHash160(u.get160());
}

std::string STUHash160::getText() const
{
	return value.GetHex();
}

STUHash256* STUHash256::construct(SerializerIterator& u)
{
	return new STUHash256(u.get256());
}

std::string STUHash256::getText() const
{
	return value.GetHex();
}

static std::string hex(const std::vector<unsigned char>& value)
{
	int dlen=value.size(), i=0;
	char psz[dlen*2 + 1];
	for(std::vector<unsigned char>::const_iterator it=value.begin(), end=value.end(); it!=end; ++it)
		sprintf(psz + 2*(i++), "%02X", *it);
	return std::string(psz, psz + value.size()*2);
}

std::string STUVariableLength::getText() const
{
	return hex(value);
}

STUVariableLength* STUVariableLength::construct(SerializerIterator& u)
{
	return new STUVariableLength(u.getVL());
}

int STUVariableLength::getLength() const
{
	return Serializer::encodeLengthLength(value.size()) + value.size();
}

std::string STUTaggedList::getText() const
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

STUTaggedList* STUTaggedList::construct(SerializerIterator& u)
{
	return new STUTaggedList(u.getTaggedList());
}

int STUTaggedList::getLength() const
{
	int ret=Serializer::getTaggedListLength(value);
	if(ret<0) throw(0);
	return ret;
}

std::string STUObject::getFullText() const
{
	std::string ret;
	if(name!=NULL)
	{
		ret=name;
		ret+=" = {";
	}
	else ret="{";
	for(boost::ptr_vector<SerializedType>::const_iterator it=data.begin(), end=data.end(); it!=end; ++it)
		ret+=it->getFullText();
	ret+="}";
	return ret;
}

int STUObject::getLength() const
{
	int ret=0;
	for(boost::ptr_vector<SerializedType>::const_iterator it=data.begin(), end=data.end(); it!=end; ++it)
		ret+=it->getLength();
	return ret;
}

void STUObject::add(Serializer& s) const
{
	for(boost::ptr_vector<SerializedType>::const_iterator it=data.begin(), end=data.end(); it!=end; ++it)
		it->add(s);
}

std::string STUObject::getText() const
{
	std::string ret="{";
	bool first=false;
	for(boost::ptr_vector<SerializedType>::const_iterator it=data.begin(), end=data.end(); it!=end; ++it)
	{
		if(!first)
		{
			ret+=", ";
			first=false;
		}
		ret+=it->getText();
	}
	ret+="}";
	return ret;
}
