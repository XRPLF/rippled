#ifndef __FIELDNAMES__
#define __FIELDNAMES__

enum SerializedTypeID
{
	// special types
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

enum SOE_Field
{
	sfInvalid = -1,
	sfGeneric = 0,

#define FIELD(name, type, index) sf##name = (STI_##type << 16) | index,
#define TYPE(name, type, index)
#include "SerializeProto.h"
#undef FIELD
#undef TYPE

	// test fields
	sfTest1=100000, sfTest2, sfTest3, sfTest4
};

struct FieldName
{
	SOE_Field field;
	const char *fieldName;
	SerializedTypeID fieldType;
	int fieldNum;
};

extern FieldName FieldNames[];
extern FieldName* getFieldName(SOE_Field);
extern FieldName* getFieldName(int type, int field);

#endif
