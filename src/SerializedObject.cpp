
#include "SerializedObject.h"

#include "boost/lexical_cast.hpp"

SerializedType* STObject::makeDefaultObject(SerializedTypeID id, const char *name)
{
	switch(id)
	{
		case STI_UINT16:
			return new STUInt16(name);

		case STI_UINT32:
			return new STUInt32(name);

		case STI_UINT64:
			return new STUInt64(name);

		case STI_HASH160:
			return new STHash160(name);

		case STI_HASH256:
			return new STHash256(name);

		case STI_VL:
			return new STVariableLength(name);

		case STI_TL:
			return new STTaggedList(name);

		case STI_ACCOUNT:
			return new STAccount(name);

		default:
			return NULL;
	}
}

SerializedType* STObject::makeDeserializedObject(SerializedTypeID id, const char *name, SerializerIterator& sit)
{
	switch(id)
	{
		case STI_UINT16:
			return STUInt16::construct(sit, name);

		case STI_UINT32:
			return STUInt32::construct(sit, name);

		case STI_UINT64:
			return STUInt64::construct(sit, name);

		case STI_HASH160:
			return STHash160::construct(sit, name);

		case STI_HASH256:
			return STHash256::construct(sit, name);

		case STI_VL:
			return STVariableLength::construct(sit, name);

		case STI_TL:
			return STTaggedList::construct(sit, name);

		case STI_ACCOUNT:
			return STAccount::construct(sit, name);

		default:
			return NULL;
	}
}

STObject::STObject(SOElement* elem, const char *name) : SerializedType(name), mFlagIdx(-1)
{
	while(elem->e_id!=STI_DONE)
	{
		if(elem->e_type==SOE_FLAGS) mFlagIdx=mType.size();
		mType.push_back(elem);
		if( (elem->e_type==SOE_IFFLAG) || (elem->e_type==SOE_IFNFLAG) )
			giveObject(new STObject(elem->e_name));
		else
		{
			SerializedType* t=makeDefaultObject(elem->e_id, elem->e_name);
			if(!t) throw(std::runtime_error("invalid transaction element"));
			giveObject(t);
		}
		elem++;
	}
}

STObject::STObject(SOElement* elem, SerializerIterator& sit, const char *name) : SerializedType(name), mFlagIdx(-1)
{
	int flags=-1;
	while(elem->e_id!=STI_DONE)
	{
		mType.push_back(elem);
		bool done=false;
		if(elem->e_type==SOE_IFFLAG)
		{
			assert(flags>=0);
			if((flags&elem->e_flags)==0) done=true;
		}
		else if(elem->e_type==SOE_IFNFLAG)
		{
			assert(flags>=0);
			if((flags&elem->e_flags)!=0) done=true;
		}
		else if(elem->e_type==SOE_FLAGS)
		{
			assert(elem->e_id==STI_UINT16);
			flags=sit.get16();
			mFlagIdx=giveObject(new STUInt16(elem->e_name, flags));
			done=true;
		}
		if(!done)
		{
			SerializedType* t=makeDeserializedObject(elem->e_id, elem->e_name, sit);
			if(!t) throw(std::runtime_error("invalid transaction element"));
			giveObject(t);
		}
		elem++;
	}
}

std::string STObject::getFullText() const
{
	std::string ret;
	if(name!=NULL)
	{
		ret=name;
		ret+=" = {";
	}
	else ret="{";
	for(boost::ptr_vector<SerializedType>::const_iterator it=mData.begin(), end=mData.end(); it!=end; ++it)
		ret+=it->getFullText();
	ret+="}";
	return ret;
}

int STObject::getLength() const
{
	int ret=0;
	for(boost::ptr_vector<SerializedType>::const_iterator it=mData.begin(), end=mData.end(); it!=end; ++it)
		ret+=it->getLength();
	return ret;
}

void STObject::add(Serializer& s) const
{
	for(boost::ptr_vector<SerializedType>::const_iterator it=mData.begin(), end=mData.end(); it!=end; ++it)
		it->add(s);
}

std::string STObject::getText() const
{
	std::string ret="{";
	bool first=false;
	for(boost::ptr_vector<SerializedType>::const_iterator it=mData.begin(), end=mData.end(); it!=end; ++it)
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

int STObject::getFieldIndex(SOE_Field field) const
{
	int i=0;
	for(std::vector<SOElement*>::const_iterator it=mType.begin(), end=mType.end(); it!=end; ++it, ++i)
		if((*it)->e_field==field) return i;
	return -1;
}

const SerializedType& STObject::peekAtField(SOE_Field field) const
{
	int index=getFieldIndex(field);
	if(index==-1) throw std::runtime_error("Field not found");
	return peekAtIndex(index);
}

SerializedType& STObject::getField(SOE_Field field)
{
	int index=getFieldIndex(field);
	if(index==-1) throw std::runtime_error("Field not found");
	return getIndex(index);
}

const SerializedType* STObject::peekAtPField(SOE_Field field) const
{
	int index=getFieldIndex(field);
	if(index==-1) return NULL;
	return peekAtPIndex(index);
}

SerializedType* STObject::getPField(SOE_Field field)
{
	int index=getFieldIndex(field);
	if(index==-1) return NULL;
	return getPIndex(index);
}

bool STObject::isFieldPresent(SOE_Field field) const
{
	int index=getFieldIndex(field);
	if(index==-1) return false;
	return peekAtIndex(field).getType()==STI_OBJECT;
}

bool STObject::setFlag(int f)
{
	if(mFlagIdx<0) return false;
	STUInt16* t=dynamic_cast<STUInt16*>(getPIndex(mFlagIdx));
	assert(t);
	t->setValue(t->getValue() | f);
	return true;
}

bool STObject::clearFlag(int f)
{
	if(mFlagIdx<0) return false;
	STUInt16* t=dynamic_cast<STUInt16*>(getPIndex(mFlagIdx));
	assert(t);
	t->setValue(t->getValue() & ~f);
	return true;
}

int STObject::getFlag(void) const
{
	if(mFlagIdx<0) return 0;
	const STUInt16* t=dynamic_cast<const STUInt16*>(peekAtPIndex(mFlagIdx));
	assert(t);
	return t->getValue();
}

void STObject::makeFieldPresent(SOE_Field field)
{
	int index=getFieldIndex(field);
	if(index==-1) throw std::runtime_error("Field not found");
	if(peekAtIndex(field).getType()!=STI_OBJECT) return;
	mData.replace(index, makeDefaultObject(mType[index]->e_id, mType[index]->e_name));
	setFlag(mType[index]->e_flags);
}

void STObject::makeFieldAbsent(SOE_Field field)
{
	int index=getFieldIndex(field);
	if(index==-1) throw std::runtime_error("Field not found");
	if(peekAtIndex(field).getType()==STI_OBJECT) return;
	mData.replace(index, new STObject(mType[index]->e_name));
	clearFlag(mType[index]->e_flags);
}

std::string STObject::getFieldString(SOE_Field field) const
{
	const SerializedType* rf=peekAtPField(field);
	if(!rf) throw std::runtime_error("Field not found");
	return rf->getText();
}

unsigned char STObject::getValueFieldU8(SOE_Field field) const
{
	const SerializedType* rf=peekAtPField(field);
	if(!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id=rf->getType();
	if(id==STI_OBJECT) return 0; // optional field not present
	const STUInt8 *cf=dynamic_cast<const STUInt8 *>(rf);
	if(!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint16 STObject::getValueFieldU16(SOE_Field field) const
{
	const SerializedType* rf=peekAtPField(field);
	if(!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id=rf->getType();
	if(id==STI_OBJECT) return 0; // optional field not present
	const STUInt16 *cf=dynamic_cast<const STUInt16 *>(rf);
	if(!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint32 STObject::getValueFieldU32(SOE_Field field) const
{
	const SerializedType* rf=peekAtPField(field);
	if(!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id=rf->getType();
	if(id==STI_OBJECT) return 0; // optional field not present
	const STUInt32 *cf=dynamic_cast<const STUInt32 *>(rf);
	if(!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint64 STObject::getValueFieldU64(SOE_Field field) const
{
	const SerializedType* rf=peekAtPField(field);
	if(!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id=rf->getType();
	if(id==STI_OBJECT) return 0; // optional field not present
	const STUInt64 *cf=dynamic_cast<const STUInt64 *>(rf);
	if(!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint160 STObject::getValueFieldH160(SOE_Field field) const
{
	const SerializedType* rf=peekAtPField(field);
	if(!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id=rf->getType();
	if(id==STI_OBJECT) return uint160(); // optional field not present
	const STHash160 *cf=dynamic_cast<const STHash160 *>(rf);
	if(!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint256 STObject::getValueFieldH256(SOE_Field field) const
{
	const SerializedType* rf=peekAtPField(field);
	if(!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id=rf->getType();
	if(id==STI_OBJECT) return uint256(); // optional field not present
	const STHash256 *cf=dynamic_cast<const STHash256 *>(rf);
	if(!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

std::vector<unsigned char> STObject::getValueFieldVL(SOE_Field field) const
{
	const SerializedType* rf=peekAtPField(field);
	if(!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id=rf->getType();
	if(id==STI_OBJECT) return std::vector<unsigned char>(); // optional field not present
	const STVariableLength *cf=dynamic_cast<const STVariableLength *>(rf);
	if(!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

std::vector<TaggedListItem> STObject::getValueFieldTL(SOE_Field field) const
{
	const SerializedType* rf=peekAtPField(field);
	if(!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id=rf->getType();
	if(id==STI_OBJECT) return std::vector<TaggedListItem>(); // optional field not present
	const STTaggedList *cf=dynamic_cast<const STTaggedList *>(rf);
	if(!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

void STObject::setValueFieldU8(SOE_Field field, unsigned char v)
{
	SerializedType* rf=getPField(field);
	if(!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id=rf->getType();
	if(id==STI_OBJECT)
	{
		makeFieldPresent(field);
		rf=getPField(field);
		id=rf->getType();
	}
	STUInt8* cf=dynamic_cast<STUInt8*>(rf);
	if(!cf) throw(std::runtime_error("Wrong field type"));
	cf->setValue(v);
}

void STObject::setValueFieldU16(SOE_Field field, uint16 v)
{
	SerializedType* rf=getPField(field);
	if(!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id=rf->getType();
	if(id==STI_OBJECT)
	{
		makeFieldPresent(field);
		rf=getPField(field);
		id=rf->getType();
	}
	STUInt16* cf=dynamic_cast<STUInt16*>(rf);
	if(!cf) throw(std::runtime_error("Wrong field type"));
	cf->setValue(v);
}

void STObject::setValueFieldU32(SOE_Field field, uint32 v)
{
	SerializedType* rf=getPField(field);
	if(!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id=rf->getType();
	if(id==STI_OBJECT)
	{
		makeFieldPresent(field);
		rf=getPField(field);
		id=rf->getType();
	}
	STUInt32* cf=dynamic_cast<STUInt32*>(rf);
	if(!cf) throw(std::runtime_error("Wrong field type"));
	cf->setValue(v);
}

void STObject::setValueFieldU64(SOE_Field field, uint64 v)
{
	SerializedType* rf=getPField(field);
	if(!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id=rf->getType();
	if(id==STI_OBJECT)
	{
		makeFieldPresent(field);
		rf=getPField(field);
		id=rf->getType();
	}
	STUInt64* cf=dynamic_cast<STUInt64*>(rf);
	if(!cf) throw(std::runtime_error("Wrong field type"));
	cf->setValue(v);
}

void STObject::setValueFieldH160(SOE_Field field, const uint160& v)
{
	SerializedType* rf=getPField(field);
	if(!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id=rf->getType();
	if(id==STI_OBJECT)
	{
		makeFieldPresent(field);
		rf=getPField(field);
		id=rf->getType();
	}
	STHash160* cf=dynamic_cast<STHash160*>(rf);
	if(!cf) throw(std::runtime_error("Wrong field type"));
	cf->setValue(v);
}

void STObject::setValueFieldVL(SOE_Field field, const std::vector<unsigned char>& v)
{
	SerializedType* rf=getPField(field);
	if(!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id=rf->getType();
	if(id==STI_OBJECT)
	{
		makeFieldPresent(field);
		rf=getPField(field);
		id=rf->getType();
	}
	STVariableLength* cf=dynamic_cast<STVariableLength*>(rf);
	if(!cf) throw(std::runtime_error("Wrong field type"));
	cf->setValue(v);
}

void STObject::setValueFieldTL(SOE_Field field, const std::vector<TaggedListItem>& v)
{
	SerializedType* rf=getPField(field);
	if(!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id=rf->getType();
	if(id==STI_OBJECT)
	{
		makeFieldPresent(field);
		rf=getPField(field);
		id=rf->getType();
	}
	STTaggedList* cf=dynamic_cast<STTaggedList*>(rf);
	if(!cf) throw(std::runtime_error("Wrong field type"));
	cf->setValue(v);
}

Json::Value STObject::getJson(int options) const
{
	Json::Value ret(Json::objectValue);
	int index=1;
	for(boost::ptr_vector<SerializedType>::const_iterator it=mData.begin(), end=mData.end(); it!=end; ++it, ++index)
	{
		if(it->getType()!=STI_NOTPRESENT)
		{
			if(it->getName()==NULL)
				ret[boost::lexical_cast<std::string>(index)]=it->getText();
			else ret[it->getName()]=it->getText();
		}
	}
	return ret;
}
