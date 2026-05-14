#pragma once

#include <xrpl/nodestore/NodeObject.h>

namespace xrpl::NodeStore {

/** Deserializes a raw backend key/value buffer into the components of a
 *  `NodeObject`.
 *
 *  This is the read-direction half of the NodeStore on-disk format, paired
 *  with `EncodedBlob`. Together they define the canonical binary schema for
 *  persisted node objects; any format change must be reflected in both classes.
 *
 *  On-disk layout (canonical reference):
 *  - Bytes 0–7: Unused prefix. Historically stored a ledger index; written
 *      as eight zero bytes today and silently ignored on read.
 *  - Byte 8: `NodeObjectType` discriminant (one-byte enum value).
 *  - Bytes 9+: Raw serialized object payload.
 *
 *  Validation is intentionally minimal and non-throwing: the constructor sets
 *  an internal success flag rather than raising an exception, allowing callers
 *  to handle corruption gracefully (see `wasOk()`). Not all corruption is
 *  detected — this is a fast sanity check, not a cryptographic integrity proof.
 *
 *  `DecodedBlob` holds non-owning pointers into the caller-supplied buffers;
 *  the backing storage must remain valid until `createObject()` is called or
 *  the `DecodedBlob` is destroyed.
 *
 *  @note This class defines the database format of a `NodeObject`.
 *  @see EncodedBlob for the write-direction counterpart.
 */
class DecodedBlob
{
public:
    /** Parse a raw backend buffer into its constituent NodeObject fields.
     *
     *  Validates the on-disk layout without performing any heap allocation.
     *  `key_` and `objectData_` are set to non-owning pointers into the
     *  caller-supplied buffers; the actual payload copy is deferred to
     *  `createObject()`. The caller must keep both buffers alive for the
     *  lifetime of this object.
     *
     *  Parsing succeeds (`wasOk()` returns `true`) only when `valueBytes > 9`
     *  and the type byte at offset 8 is one of the four recognised values:
     *  `hotUNKNOWN`, `hotLEDGER`, `hotACCOUNT_NODE`, or `hotTRANSACTION_NODE`.
     *  `hotDUMMY` (value 512) and any unrecognised byte leave the object in a
     *  failed state without throwing.
     *
     *  @param key        Pointer to the 32-byte hash that was used as the
     *      storage key; not validated or dereferenced here.
     *  @param value      Pointer to the raw value buffer retrieved from the
     *      backend.
     *  @param valueBytes Total byte length of `value`. Values of 9 or fewer
     *      bytes produce a failed parse.
     */
    DecodedBlob(void const* key, void const* value, int valueBytes);

    /** Returns `true` if the constructor successfully parsed a well-formed
     *  buffer with a recognised `NodeObjectType`.
     *
     *  Must be checked before calling `createObject()`. Calling `createObject()`
     *  on a failed `DecodedBlob` fires `XRPL_ASSERT` in debug builds.
     */
    [[nodiscard]] bool
    wasOk() const noexcept
    {
        return success_;
    }

    /** Allocate and return a `NodeObject` from the previously parsed fields.
     *
     *  Copies the payload slice into an owning `Blob` and reconstructs the
     *  full hash key from the stored pointer. This is the only heap allocation
     *  in the decode path. The returned `NodeObject` owns its data
     *  independently, so the caller may release the backend fetch buffer
     *  immediately after this call returns.
     *
     *  @pre `wasOk()` must return `true`. Calling this on a failed parse fires
     *      `XRPL_ASSERT` in debug builds; in release builds a null
     *      `shared_ptr` is returned as a defensive fallback.
     *  @return A fully constructed `NodeObject`, or `nullptr` if the parse had
     *      failed (release-build defensive path — callers must always check
     *      `wasOk()` first).
     */
    std::shared_ptr<NodeObject>
    createObject();

private:
    bool success_;

    void const* key_;
    NodeObjectType objectType_;
    unsigned char const* objectData_;
    int dataBytes_;
};

}  // namespace xrpl::NodeStore
