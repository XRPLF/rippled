/** @file
 *  @brief NodeStore binary deserialization: parses raw backend blobs into
 *      `NodeObject` instances.
 *
 *  `DecodedBlob` is the read-direction half of the NodeStore on-disk format
 *  (the write direction lives in `EncodedBlob`). The two classes together
 *  define the complete binary schema; any format change must be reflected in
 *  both. See `EncodedBlob.h` for the authoritative format description.
 */

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

/** Parse the raw backend value buffer into its constituent fields.
 *
 *  Validates the on-disk layout without performing any heap allocation.
 *  `m_objectData` is set to a non-owning pointer into `value`; the actual
 *  `Blob` copy is deferred to `createObject()`.
 *
 *  On-disk layout (mirrors `EncodedBlob`):
 *  - Bytes 0–7: Ignored prefix (historically stored the ledger index;
 *      always zero in current writes). Bytes 4–7 were likely never defined
 *      when the original field was widened from 4 to 8 bytes.
 *  - Byte 8: `NodeObjectType` discriminant cast via `safeCast`.
 *  - Bytes 9+: Raw serialized payload.
 *
 *  `success_` is set to `true` only when `valueBytes > 9` **and** the type
 *  byte matches one of the four recognised `NodeObjectType` values. An
 *  unrecognised byte — whether from a future format version or actual
 *  corruption — leaves `success_ = false`, making the decoder safely
 *  forward-incompatible without throwing.
 *
 *  @param key        Pointer to the 32-byte hash key (not validated here;
 *      ownership lies with the caller for the lifetime of this object).
 *  @param value      Pointer to the raw value buffer from the backend.
 *  @param valueBytes Total byte length of `value`. A value ≤ 9 produces a
 *      failed parse (`wasOk()` returns `false`).
 */
DecodedBlob::DecodedBlob(void const* key, void const* value, int valueBytes)
{
    /*  On-disk format:
        Bytes 0–7   Unused prefix (legacy ledger-index field; always zero today)
        Byte  8     NodeObjectType discriminant
        Bytes 9+    Serialized payload
    */

    success_ = false;
    key_ = key;
    objectType_ = NodeObjectType::Unknown;
    objectData_ = nullptr;
    // std::max guards against a negative length when valueBytes < 9.
    dataBytes_ = std::max(0, valueBytes - 9);

    // Bytes 0–7 are intentionally ignored; byte 8 carries the type.
    if (valueBytes > 8)
    {
        unsigned char const* byte = static_cast<unsigned char const*>(value);
        objectType_ = safeCast<NodeObjectType>(byte[8]);
    }

    if (valueBytes > 9)
    {
        objectData_ = static_cast<unsigned char const*>(value) + 9;

        // Only the four currently known types are accepted. Any unrecognised
        // byte (corruption or future format) leaves success_ = false so the
        // caller can discard the record via wasOk() without an exception.
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

/** Allocate and return a `NodeObject` from the previously parsed fields.
 *
 *  This is the only allocation point in the decode path. It copies the
 *  payload slice (`objectData_[0..dataBytes_)`) into an owning `Blob` and
 *  reconstructs the full key from the stored pointer.
 *
 *  @pre `wasOk()` must return `true`; calling this on a failed parse fires
 *      `XRPL_ASSERT` in debug builds. In release builds the guard returns a
 *      null `shared_ptr` instead of invoking undefined behaviour.
 *
 *  @return A fully constructed `NodeObject`, or `nullptr` if the parse had
 *      failed (release-build defensive path only — callers should always
 *      check `wasOk()` first).
 */
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
