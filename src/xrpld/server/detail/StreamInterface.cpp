#include <xrpld/overlay/detail/Handshake.h>

#include <xrpl/server/detail/StreamInterface.h>

namespace ripple {

std::optional<base_uint<256>>
ProductionStream::makeSharedValue(beast::Journal journal)
{
    // Delegate to the existing Handshake module
    return ripple::makeSharedValue(*stream_, journal);
}

}  // namespace ripple
