#ifndef __FIELDNAMES__
#define __FIELDNAMES__

#include <boost/thread/mutex.hpp>

#define FIELD_CODE(type, index) ((static_cast<int>(type) << 16) | index)

enum SerializedTypeID
{
	// special types
	STI_UNKNOWN		= -2,
	STI_DONE		= -1,
	STI_NOTPRESENT	= 0,

#define TYPE(name, field, value) STI_##field = value,
#define FIELD(name, field, value)
#include "SerializeProto.h"
#undef TYPE
#undef FIELD

	// high level types
	STI_TRANSACTION = 100001,
	STI_LEDGERENTRY	= 100002,
	STI_VALIDATION	= 100003,
};

enum SOE_Flags
{
	SOE_END = -1,		// marks end of object
	SOE_REQUIRED = 0,	// required
	SOE_OPTIONAL = 1,	// optional
};

class SField
{
public:
	typedef const SField&	ref;
	typedef SField const *	ptr;

protected:
	static std::map<int, SField*>	codeToField;
	static boost::mutex				mapMutex;

public:

	const int				fieldCode;		// (type<<16)|index
	const SerializedTypeID	fieldType;		// STI_*
	const int				fieldValue;		// Code number for protocol
	const char*				fieldName;

	SField(int fc, SerializedTypeID tid, int fv, const char* fn) : 
		fieldCode(fc), fieldType(tid), fieldValue(fv), fieldName(fn)
	{ codeToField[fc] = this; }

	SField(int fc) : fieldCode(fc), fieldType(STI_UNKNOWN), fieldValue(0), fieldName(NULL) { ; }

	static SField::ref getField(int fieldCode);
	static SField::ref getField(SerializedTypeID type, int value) { return getField(FIELD_CODE(type, value)); }

	bool isGeneric() const		{ return fieldCode == 0; }
	bool isInvalid() const		{ return fieldCode == -1; }
	bool isKnown() const		{ return fieldType != STI_UNKNOWN; }
};

extern SField sfInvalid, sfGeneric;

#define FIELD(name, type, index) extern SField sf##name;
#define TYPE(name, type, index)
#include "SerializeProto.h"
#undef FIELD
#undef TYPE

#endif
