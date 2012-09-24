
#include "FieldNames.h"

#define FIELD(name, type, index) { sf##name, #name, STI_##type, index },
#define TYPE(type, index)

FieldName FieldNames[]=
{
#include "SerializeProto.h"
};

#undef FIELD
#undef TYPE
