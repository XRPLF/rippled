#include <xrpl/nodestore/detail/DecodedBlob.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/nodestore/NodeObject.h>

#include <algorithm>
#include <memory>
#include <utility>

namespace xrpl::NodeStore {

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
    objectType_ = NodeObjectType::Unknown;
    objectData_ = nullptr;
    dataBytes_ = std::max(0, valueBytes - 9);

    // VFALCO NOTE What about bytes 4 through 7 inclusive?

    if (valueBytes > 8)
    {
        unsigned char const* byte = static_cast<unsigned char const*>(value);
        objectType_ = safeCast<NodeObjectType>(byte[8]);
    }

    if (valueBytes > 9)
    {
        objectData_ = static_cast<unsigned char const*>(value) + 9;

        switch (objectType_)
        {
            default:
                break;

            case NodeObjectType::Unknown:
            case NodeObjectType::Ledger:
            case NodeObjectType::AccountNode:
            case NodeObjectType::TransactionNode:
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

}  // namespace xrpl::NodeStore
