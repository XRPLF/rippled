#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/nodestore/detail/DecodedBlob.h>

#include <algorithm>

namespace xrpl {
namespace NodeStore {

DecodedBlob::DecodedBlob(void const* key, void const* value, int valueBytes)
{
    /*  Data format:

        Bytes

        0...7       Unused
        8           char            One of NodeObjectType
        9...end                     The body of the object data
    */

    success_ = false;
    key_ = key;
    objectType_ = hotUNKNOWN;
    objectData_ = nullptr;
    dataBytes_ = std::max(0, valueBytes - 9);

    // VFALCO NOTE What about bytes 4 through 7 inclusive?

    if (valueBytes > 8)
    {
        unsigned char const* byte = static_cast<unsigned char const*>(value);
        objectType_ = safe_cast<NodeObjectType>(byte[8]);
    }

    if (valueBytes > 9)
    {
        objectData_ = static_cast<unsigned char const*>(value) + 9;

        switch (objectType_)
        {
            default:
                break;

            case hotUNKNOWN:
            case hotLEDGER:
            case hotACCOUNT_NODE:
            case hotTRANSACTION_NODE:
                success_ = true;
                break;
        }
    }
}

std::shared_ptr<NodeObject>
DecodedBlob::createObject()
{
    XRPL_ASSERT(success_, "xrpl::NodeStore::DecodedBlob::createObject : valid object type");

    std::shared_ptr<NodeObject> object;

    if (success_)
    {
        Blob data(objectData_, objectData_ + dataBytes_);

        object = NodeObject::createObject(objectType_, std::move(data), uint256::fromVoid(key_));
    }

    return object;
}

}  // namespace NodeStore
}  // namespace xrpl
