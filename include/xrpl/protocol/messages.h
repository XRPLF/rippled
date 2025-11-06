#ifndef XRPL_PROTOCOL_MESSAGES_H_INCLUDED
#define XRPL_PROTOCOL_MESSAGES_H_INCLUDED

// Some versions of protobuf generate code that will produce errors during
// compilation. See https://github.com/google/protobuf/issues/549 for more
// details. We work around this by undefining this macro.
//
// TODO: Remove this after the protoc we use is upgraded to not generate
//       code that conflicts with the TYPE_BOOL macro.

#ifdef TYPE_BOOL
#undef TYPE_BOOL
#endif

#include <xrpl/proto/xrpl.pb.h>

#endif
