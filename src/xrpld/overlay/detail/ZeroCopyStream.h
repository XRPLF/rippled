#pragma once

/** @file
 *  Zero-copy I/O adapters that bridge Boost.Asio buffer sequences and the
 *  Protocol Buffers serialization engine.
 *
 *  `ZeroCopyInputStream<Buffers>` wraps a `ConstBufferSequence` so that
 *  protobuf can parse directly from Asio receive buffers without copying.
 *  `ZeroCopyOutputStream<Streambuf>` wraps an Asio `streambuf` so that
 *  protobuf can serialize directly into the network send buffer.
 *
 *  These are the lowest-level I/O adapters in the overlay stack, sitting
 *  directly between raw Asio buffers and the protobuf layer through which
 *  all peer-to-peer message types flow.
 */

#include <xrpl/beast/utility/instrumentation.h>

#include <boost/asio/buffer.hpp>

#include <google/protobuf/io/zero_copy_stream.h>

namespace xrpl {

/** Protobuf `ZeroCopyInputStream` adapter over an Asio `ConstBufferSequence`.
 *
 *  Allows protobuf to parse messages directly from a scatter-gather buffer
 *  sequence without an intermediate copy. This is the primary read path for
 *  all inbound overlay messages: `parseMessageContent()` constructs one of
 *  these over the Asio receive buffers, calls `Skip()` to advance past the
 *  wire-frame header, then hands the stream to either
 *  `ParseFromZeroCopyStream` (uncompressed) or `decompress()` (LZ4).
 *
 *  The iterator pair `[first_, last_)` walks the buffer sequence. `pos_`
 *  tracks the sub-buffer read position, enabling `BackUp()` and `Skip()` at
 *  byte granularity without touching memory. If `buffers` is empty, `pos_`
 *  is initialized to a null zero-length buffer so that `Next()` immediately
 *  returns `false` without dereferencing the end iterator.
 *
 *  @tparam Buffers A type satisfying the Boost.Asio `ConstBufferSequence`
 *      requirements (e.g., the buffer sequence returned by a
 *      `boost::beast::multi_buffer::data()`).
 *  @see https://developers.google.com/protocol-buffers/docs/reference/cpp/google.protobuf.io.zero_copy_stream
 */
template <class Buffers>
class ZeroCopyInputStream : public ::google::protobuf::io::ZeroCopyInputStream
{
private:
    using iterator = typename Buffers::const_iterator;
    using const_buffer = boost::asio::const_buffer;

    /// Running total of bytes surfaced to the protobuf parser via `Next()`,
    /// adjusted symmetrically by `BackUp()` and `Skip()`.
    google::protobuf::int64 count_ = 0;
    /// One-past-the-end sentinel for the buffer sequence.
    iterator last_;
    /// Iterator to the buffer that `pos_` was derived from; rewound by `BackUp()`.
    iterator first_;
    /// Sub-buffer view of the next bytes `Next()` will return; updated by
    /// `BackUp()` and `Skip()` for byte-granularity positioning.
    const_buffer pos_;

public:
    /** Construct a stream over `buffers`.
     *
     *  If `buffers` is empty, the stream is immediately exhausted:
     *  `Next()` will return `false` on the first call.
     *
     *  @param buffers The buffer sequence to read from; must remain valid
     *      for the lifetime of this object.
     */
    explicit ZeroCopyInputStream(Buffers const& buffers);

    /** Advance to the next buffer segment and expose it to the caller.
     *
     *  Sets `*data` and `*size` to the start and byte-length of the next
     *  available segment, then advances the internal position past it.
     *  If the stream is already exhausted, sets `*size` to 0 and returns
     *  `false` without modifying the read position.
     *
     *  @param data Receives a pointer to the start of the next data chunk.
     *  @param size Receives the number of bytes in that chunk.
     *  @return `true` if data was surfaced; `false` if the stream is
     *      exhausted.
     */
    bool
    Next(void const** data, int* size) override;

    /** Return unconsumed bytes from the most recent `Next()` call.
     *
     *  Rewinds `first_` by one buffer and recomputes `pos_` as an offset
     *  into that buffer so the last `count` bytes are re-presented on the
     *  next `Next()` call. `ByteCount()` is decremented accordingly.
     *
     *  Callers must not back up more bytes than were returned by the most
     *  recent `Next()` call (a contract enforced by protobuf itself).
     *
     *  @param count Number of bytes to return; must be ≤ the size returned
     *      by the most recent `Next()`.
     */
    void
    BackUp(int count) override;

    /** Skip forward `count` bytes without surfacing data.
     *
     *  Advances `pos_` within the current buffer when the skip falls short
     *  of its end, or consumes whole buffers and advances `first_` until
     *  the skip count is exhausted. Used by `parseMessageContent()` to
     *  advance past the wire-frame header before handing the stream to the
     *  protobuf decoder.
     *
     *  @param count Number of bytes to skip; must be ≥ 0.
     *  @return `true` if the full skip succeeded; `false` if the stream
     *      was exhausted before `count` bytes could be skipped.
     */
    bool
    Skip(int count) override;

    /** Total bytes surfaced to the protobuf parser since construction.
     *
     *  Accounts for bytes returned by `Next()`, reduced by `BackUp()`, and
     *  increased by `Skip()`. Satisfies the `ZeroCopyInputStream` contract.
     *
     *  @return Cumulative byte count.
     */
    [[nodiscard]] google::protobuf::int64
    ByteCount() const override
    {
        return count_;
    }
};

//------------------------------------------------------------------------------

template <class Buffers>
ZeroCopyInputStream<Buffers>::ZeroCopyInputStream(Buffers const& buffers)
    : last_(buffers.end())
    , first_(buffers.begin())
    , pos_((first_ != last_) ? *first_ : const_buffer(nullptr, 0))
{
}

template <class Buffers>
bool
ZeroCopyInputStream<Buffers>::Next(void const** data, int* size)
{
    *data = pos_.data();
    *size = boost::asio::buffer_size(pos_);
    if (first_ == last_)
        return false;
    count_ += *size;
    pos_ = (++first_ != last_) ? *first_ : const_buffer(nullptr, 0);
    return true;
}

template <class Buffers>
void
ZeroCopyInputStream<Buffers>::BackUp(int count)
{
    --first_;
    pos_ = *first_ + (boost::asio::buffer_size(*first_) - count);
    count_ -= count;
}

template <class Buffers>
bool
ZeroCopyInputStream<Buffers>::Skip(int count)
{
    if (first_ == last_)
        return false;
    while (count > 0)
    {
        auto const size = boost::asio::buffer_size(pos_);
        if (count < size)
        {
            pos_ = pos_ + count;
            count_ += count;
            return true;
        }
        count_ += size;
        if (++first_ == last_)
            return false;
        count -= size;
        pos_ = *first_;
    }
    return true;
}

//------------------------------------------------------------------------------

/** Protobuf `ZeroCopyOutputStream` adapter over an Asio `streambuf`.
 *
 *  Allows protobuf to serialize messages directly into an Asio `streambuf`'s
 *  write area without an intermediate copy. Follows the streambuf
 *  prepare/commit protocol: `Next()` calls `streambuf_.prepare(blockSize_)`
 *  to reserve write space, and defers `streambuf_.commit()` until the next
 *  `Next()` call or destruction. This batches commits rather than issuing one
 *  per protobuf write, reducing overhead on messages that span multiple blocks.
 *
 *  **Destructor is critical**: protobuf does not guarantee a terminal
 *  `BackUp()` or second `Next()` call after serialization finishes, so the
 *  destructor unconditionally flushes any outstanding `commit_` bytes.
 *  Failing to let this object go out of scope before reading from the
 *  streambuf silently drops the tail of the serialized message.
 *
 *  @tparam Streambuf A type matching the public interface of
 *      `boost::asio::streambuf` (i.e., providing `prepare()`, `commit()`,
 *      and `mutable_buffers_type`).
 */
template <class Streambuf>
class ZeroCopyOutputStream : public ::google::protobuf::io::ZeroCopyOutputStream
{
private:
    using buffers_type = typename Streambuf::mutable_buffers_type;
    using iterator = typename buffers_type::const_iterator;
    using mutable_buffer = boost::asio::mutable_buffer;

    /// The streambuf being written into; held by reference, not owned.
    Streambuf& streambuf_;
    /// Allocation granularity: bytes reserved per `prepare()` call.
    std::size_t blockSize_;
    /// Running total of bytes durably committed to `streambuf_`.
    google::protobuf::int64 count_ = 0;
    /// Bytes promised in the most recent `Next()` but not yet committed;
    /// flushed on the next `Next()` call or in the destructor. Zero when
    /// no outstanding promise exists (guards against double-commit).
    std::size_t commit_ = 0;
    /// The mutable buffer sequence returned by the most recent `prepare()`.
    buffers_type buffers_;
    /// Iterator into `buffers_` pointing to the segment handed to the caller.
    iterator pos_;

public:
    /** Construct a stream that writes into `streambuf` in `blockSize`-byte chunks.
     *
     *  Immediately calls `streambuf.prepare(blockSize)` to reserve the first
     *  write block. The `blockSize` value decouples allocation granularity
     *  from serialization logic; callers typically pass a constant from
     *  `Tuning.h`.
     *
     *  @param streambuf The streambuf to write into; must outlive this object.
     *  @param blockSize Number of bytes to reserve per `prepare()` call.
     */
    explicit ZeroCopyOutputStream(Streambuf& streambuf, std::size_t blockSize);

    /** Flush any outstanding uncommitted bytes into the streambuf.
     *
     *  Calls `streambuf_.commit(commit_)` if `commit_ != 0`. This RAII flush
     *  is essential: protobuf does not guarantee a terminal `BackUp()` or
     *  second `Next()` after serialization, so the last block written would
     *  be silently lost without it. Callers must ensure this object goes out
     *  of scope before reading data from the underlying streambuf.
     */
    ~ZeroCopyOutputStream() override;

    /** Reserve write space and expose it to the protobuf serializer.
     *
     *  Commits any bytes outstanding from the previous `Next()` call, then
     *  advances to the next segment (calling `streambuf_.prepare(blockSize_)`
     *  if the current block is exhausted). Sets `*data` and `*size` to the
     *  start and length of the newly vended region and records `commit_` for
     *  the deferred-commit pattern.
     *
     *  Always returns `true`; the streambuf grows on demand.
     *
     *  @param data Receives a pointer to the start of the writable region.
     *  @param size Receives the number of bytes available in that region.
     *  @return `true` always (unbounded output stream).
     */
    bool
    Next(void** data, int* size) override;

    /** Return over-allocated bytes from the most recent `Next()` call.
     *
     *  Protobuf frequently requests more buffer than it uses. This method
     *  commits only `commit_ - count` bytes (the portion actually written)
     *  and zeroes `commit_` to prevent a double-commit in the destructor or
     *  the next `Next()` call.
     *
     *  @param count Number of unused bytes to return; must be ≤ `commit_`
     *      (enforced by `XRPL_ASSERT`).
     */
    void
    BackUp(int count) override;

    /** Total bytes durably committed to the streambuf since construction.
     *
     *  Does not include the currently outstanding `commit_` bytes — those
     *  are counted only after the next `Next()` call or destruction.
     *
     *  @return Cumulative committed byte count.
     */
    [[nodiscard]] google::protobuf::int64
    ByteCount() const override
    {
        return count_;
    }
};

//------------------------------------------------------------------------------

template <class Streambuf>
ZeroCopyOutputStream<Streambuf>::ZeroCopyOutputStream(Streambuf& streambuf, std::size_t blockSize)
    : streambuf_(streambuf)
    , blockSize_(blockSize)
    , buffers_(streambuf_.prepare(blockSize_))
    , pos_(buffers_.begin())
{
}

template <class Streambuf>
ZeroCopyOutputStream<Streambuf>::~ZeroCopyOutputStream()
{
    if (commit_ != 0)
        streambuf_.commit(commit_);
}

template <class Streambuf>
bool
ZeroCopyOutputStream<Streambuf>::Next(void** data, int* size)
{
    if (commit_ != 0)
    {
        streambuf_.commit(commit_);
        count_ += commit_;
    }

    if (pos_ == buffers_.end())
    {
        buffers_ = streambuf_.prepare(blockSize_);
        pos_ = buffers_.begin();
    }

    *data = *pos_.data();
    *size = boost::asio::buffer_size(*pos_);
    commit_ = *size;
    ++pos_;
    return true;
}

template <class Streambuf>
void
ZeroCopyOutputStream<Streambuf>::BackUp(int count)
{
    XRPL_ASSERT(count <= commit_, "xrpl::ZeroCopyOutputStream::BackUp : valid input");
    auto const n = commit_ - count;
    streambuf_.commit(n);
    count_ += n;
    commit_ = 0;
}

}  // namespace xrpl
