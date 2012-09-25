
#include "FieldNames.h"

#include <map>

#include <boost/thread/mutex.hpp>

#define FIELD(name, type, index) { sf##name, #name, STI_##type, index },
#define TYPE(name, type, index)

FieldName FieldNames[]=
{
#include "SerializeProto.h"

	{ sfInvalid, 0, STI_DONE, 0 }
};

#undef FIELD
#undef TYPE

static std::map<int, FieldName*> unknownFieldMap;
static boost::mutex unknownFieldMutex;

FieldName* getFieldName(SOE_Field f)
{ // OPTIMIZEME
	for (FieldName* n = FieldNames; n->fieldName != 0; ++n)
		if (n->field == f)
			return n;
	return NULL;
}

FieldName* getFieldName(int type, int field)
{ // OPTIMIZEME
	int f = (type << 16) | field;
	for (FieldName* n = FieldNames; n->fieldName != 0; ++n)
		if (n->field == f)
			return n;
	if ((type <= 0) || (type >= 256) || (field <= 0) || (field >= 256
		return NULL;

	boost::mutex::scoped_lock sl(unknownFieldMutex);
	std::map<int, FieldName*> it = unknownFieldMap.Find(f);
	if (it != unknownFieldMap.end())
		return it->second;

	FieldName* n = new FieldName();
	n->field = f;
	n->fieldName = "unknown";
	n->fieldType = static_cast<SerializedTypeID>(type);
	n->fieldNum = field;
	unknownFieldMap[f] = n;
	return n;
}
