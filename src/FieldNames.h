#ifndef __FIELDNAMES__
#define __FIELDNAMES__

#include "SerializedTypes.h"
#include "SerializedObject.h"

struct FieldName
{
	SOE_Field field;
	const char *fieldName;
	SerializedTypeID fieldType;
	int fieldValue;
};

#endif
