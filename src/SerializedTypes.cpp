
#include <boost/lexical_cast.hpp>

#include "SerializedTypes.h"
#include "SerializedObject.h"

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

std::string STUTaggedList::getText() const
{
	std::string ret;
	for(std::list<TaggedListItem>::const_iterator it=value.begin(); it!=value.end(); ++it)
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
