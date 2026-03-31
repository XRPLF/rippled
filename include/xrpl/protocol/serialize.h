#pragma once

#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>

namespace xrpl {

/** Serialize an object to a blob. */
template <class Object>
Blob
serializeBlob(Object const& o)
{
    Serializer s;
    o.add(s);
    return s.peekData();
}

/** Serialize an object to a hex string. */
inline std::string
serializeHex(STObject const& o)
{
    return strHex(serializeBlob(o));
}

}  // namespace xrpl
