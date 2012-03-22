
#include "SerializedObject.h"

STObject::STObject(SOElement* elem, const char *name) : SerializedType(name)
{
	while(elem->e_id!=STI_DONE)
	{
		type.push_back(elem);
		if( (elem->e_type==SOE_IFFLAG) || (elem->e_type==SOE_IFNFLAG) )
			giveObject(new STObject(elem->e_name));
		else switch(elem->e_id)
		{
			case STI_UINT16:
				giveObject(new STUInt16(elem->e_name));
				break;
			case STI_UINT32:
				giveObject(new STUInt32(elem->e_name));
				break;
			case STI_UINT64:
				giveObject(new STUInt64(elem->e_name));
				break;
			case STI_HASH160:
				giveObject(new STHash160(elem->e_name));
				break;
			case STI_HASH256:
				giveObject(new STHash256(elem->e_name));
				break;
			case STI_VL:
				giveObject(new STVariableLength(elem->e_name));
				break;
			case STI_TL:
				giveObject(new STTaggedList(elem->e_name));
				break;
#if 0
			case STI_ACCOUNT: // CHECKME: Should an account be variable length?
				giveObject(new STVariableLength(elem->e_name));
				break;
#endif
			default: throw(std::runtime_error("invalid transaction element"));
		}
		elem++;
	}
}

STObject::STObject(SOElement* elem, SerializerIterator& sit, const char *name) : SerializedType(name)
{
	int flags=-1;
	while(elem->e_id!=STI_DONE)
	{
		type.push_back(elem);
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
			giveObject(new STUInt16(elem->e_name, flags));
			done=true;
		}
		if(!done)
		{
			switch(elem->e_id)
			{
				case STI_UINT16:
					giveObject(STUInt16::construct(sit, elem->e_name));
					break;
				case STI_UINT32:
					giveObject(STUInt32::construct(sit, elem->e_name));
					break;
				case STI_UINT64:
					giveObject(STUInt64::construct(sit, elem->e_name));
					break;
				case STI_HASH160:
					giveObject(STHash160::construct(sit, elem->e_name));
					break;
				case STI_HASH256:
					giveObject(STHash256::construct(sit, elem->e_name));
					break;
				case STI_VL:
					giveObject(STVariableLength::construct(sit, elem->e_name));
					break;
				case STI_TL:
					giveObject(STTaggedList::construct(sit, elem->e_name));
					break;
	#if 0
				case STI_ACCOUNT: // CHECKME: Should an account be variable length?
					giveObject(STVariableLength::construct(sit, elem->e_name));
					break;
	#endif
				default: throw(std::runtime_error("invalid transaction element"));
			}
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
	for(boost::ptr_vector<SerializedType>::const_iterator it=data.begin(), end=data.end(); it!=end; ++it)
		ret+=it->getFullText();
	ret+="}";
	return ret;
}

int STObject::getLength() const
{
	int ret=0;
	for(boost::ptr_vector<SerializedType>::const_iterator it=data.begin(), end=data.end(); it!=end; ++it)
		ret+=it->getLength();
	return ret;
}

void STObject::add(Serializer& s) const
{
	for(boost::ptr_vector<SerializedType>::const_iterator it=data.begin(), end=data.end(); it!=end; ++it)
		it->add(s);
}

std::string STObject::getText() const
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
