#pragma once

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/nodestore/NodeObject.h>

#include <boost/align/align_up.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

namespace xrpl::NodeStore {

/** Serializes a `NodeObject` to the binary wire format expected by storage
 *  backends (NuDB, RocksDB).
 *
 *  This is the write-direction half of the NodeStore on-disk format, paired
 *  with `DecodedBlob`. Together they define the canonical binary schema for
 *  persisted node objects; any format change must be reflected in both classes.
 *
 *  On-disk layout (canonical reference):
 *  - Bytes 0–7: Eight zero bytes. Historically stored the ledger index;
 *      zeroed since that field was removed. Readers must not assume all
 *      zeros — older databases may contain non-zero values here.
 *  - Byte 8: `NodeObjectType` cast to a single `uint8_t`.
 *  - Bytes 9+: Raw serialized payload from `NodeObject::getData()`.
 *
 *  The 32-byte `uint256` hash is the storage key and is kept separate from
 *  the value payload. `getKey()` and `getData()` expose these two pieces as
 *  `void const*` pointers suitable for direct hand-off to NuDB or RocksDB
 *  slice APIs.
 *
 *  Instances are intended to be constructed immediately before a backend
 *  insert call and destroyed immediately after, keeping any heap-allocated
 *  overflow buffer alive for exactly as long as needed.
 *
 *  @note This class is non-copyable. `ptr_` is a `const` raw pointer whose
 *      ownership is conditional: it points into the inline `payload_` buffer
 *      when the serialized size fits within 1033 bytes (~94% of real objects),
 *      and into a heap buffer otherwise. Copying would require duplicating
 *      that conditional ownership, so no copy or move constructor is provided.
 *
 *  @see DecodedBlob for the read-direction counterpart.
 */

class EncodedBlob
{
    /** Storage key: the object's 32-byte `uint256` hash. */
    std::array<std::uint8_t, 32> key_{};

    /** Inline stack buffer covering the 9-byte header plus up to 1024 bytes
     *  of payload.
     *
     *  Sized at compile time via `boost::alignment::align_up` to the next
     *  `uint32_t`-aligned boundary, eliminating any trailing padding that a
     *  naive `9 + 1024` array would incur. When `size_` does not exceed this
     *  array's capacity, `ptr_` aliases `payload_.data()` and no heap
     *  allocation occurs.
     */
    std::array<std::uint8_t, boost::alignment::align_up(9 + 1024, alignof(std::uint32_t))>
        payload_{};

    /** Total byte length of the serialized value (header + payload). */
    std::uint32_t size_;

    /** Pointer to the serialized value buffer.
     *
     *  Set once at construction and never changed (`const`). Points into
     *  `payload_` when `size_ <= payload_.size()`, or into a heap allocation
     *  otherwise. The destructor uses `ptr_ != payload_.data()` to decide
     *  whether to `delete[]`.
     */
    std::uint8_t* const ptr_;

public:
    /** Serialize `obj` into the on-disk wire format.
     *
     *  Fills `key_` with the object's hash, writes the 9-byte header into
     *  `ptr_`, then copies the payload. If the total serialized size exceeds
     *  the inline `payload_` buffer capacity the constructor heap-allocates
     *  an exact-fit buffer; otherwise the inline buffer is used directly.
     *
     *  @param obj The node object to serialize. Must be non-null: a null
     *      `shared_ptr` fires `XRPL_ASSERT` in debug builds and throws
     *      `std::runtime_error` in all builds.
     *  @throws std::runtime_error if `obj` is null.
     */
    explicit EncodedBlob(std::shared_ptr<NodeObject> const& obj)
        : size_([&obj]() {
            XRPL_ASSERT(obj, "xrpl::NodeStore::EncodedBlob::EncodedBlob : non-null input");

            if (!obj)
                throw std::runtime_error("EncodedBlob: unseated std::shared_ptr used.");

            return obj->getData().size() + 9;
        }())
        , ptr_((size_ <= payload_.size()) ? payload_.data() : new std::uint8_t[size_])
    {
        std::fill_n(ptr_, 8, std::uint8_t{0});
        ptr_[8] = static_cast<std::uint8_t>(obj->getType());
        std::copy_n(obj->getData().data(), obj->getData().size(), ptr_ + 9);
        std::copy_n(obj->getHash().data(), obj->getHash().size(), key_.data());
    }

    /** Releases any heap-allocated overflow buffer.
     *
     *  If `ptr_` points outside `payload_` (i.e., a heap buffer was
     *  allocated because the serialized size exceeded 1033 bytes), the buffer
     *  is freed with `delete[]`. An `XRPL_ASSERT` verifies that the pointer
     *  and size fields are mutually consistent before the free, catching any
     *  state drift that would otherwise cause a double-free or memory leak.
     */
    ~EncodedBlob()
    {
        XRPL_ASSERT(
            ((ptr_ == payload_.data()) && (size_ <= payload_.size())) ||
                ((ptr_ != payload_.data()) && (size_ > payload_.size())),
            "xrpl::NodeStore::EncodedBlob::~EncodedBlob : valid payload "
            "pointer");

        if (ptr_ != payload_.data())
            delete[] ptr_;
    }

    /** Returns a pointer to the 32-byte storage key (the object's hash).
     *
     *  The pointer is valid for the lifetime of this `EncodedBlob` and may
     *  be passed directly to NuDB or RocksDB key-slice APIs.
     *
     *  @return `void const*` pointing to the 32-byte key buffer.
     */
    [[nodiscard]] void const*
    getKey() const noexcept
    {
        return static_cast<void const*>(key_.data());
    }

    /** Returns the total byte length of the serialized value buffer.
     *
     *  This is `obj->getData().size() + 9`: nine header bytes (eight
     *  zero-prefix bytes plus the type byte) followed by the raw payload.
     *
     *  @return Byte count of the buffer returned by `getData()`.
     */
    [[nodiscard]] std::size_t
    getSize() const noexcept
    {
        return size_;
    }

    /** Returns a pointer to the serialized value buffer.
     *
     *  The buffer layout is: eight zero bytes, one `NodeObjectType` byte,
     *  then the raw object payload. The pointer is valid for the lifetime of
     *  this `EncodedBlob` and may be passed directly to NuDB compression
     *  helpers or RocksDB value-slice APIs.
     *
     *  @return `void const*` pointing to `getSize()` bytes of serialized data.
     */
    [[nodiscard]] void const*
    getData() const noexcept
    {
        return static_cast<void const*>(ptr_);
    }
};

}  // namespace xrpl::NodeStore
