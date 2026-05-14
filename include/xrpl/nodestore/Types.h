/** @file
 *  Shared vocabulary types for the xrpl::NodeStore subsystem.
 *
 *  This header sits at the base of the NodeStore include hierarchy and is
 *  pulled in by every other NodeStore interface header. It defines only the
 *  primitives that all participants — backends, the async database layer, and
 *  callers — must agree on: the operation status codes, the batch container
 *  alias, and the batch-size policy constants.
 */

#pragma once

#include <xrpl/nodestore/NodeObject.h>

#include <vector>

namespace xrpl::NodeStore {

/** Initial capacity hint for a `Batch` vector and the backpressure threshold
 *  in `BatchWriter::store`.
 *
 *  `BatchWriter` reserves this many slots on construction and re-reserves after
 *  each flush to avoid repeated allocations. `BatchWriter::store` also blocks
 *  when `writeSet_` reaches this size, providing backpressure against producers
 *  that outrun the flush thread. This value does not cap how many objects can
 *  ultimately be written in a single pass.
 */
static constexpr auto kBATCH_WRITE_PREALLOCATION_SIZE = 256;

/** Maximum number of objects flushed in a single batch write.
 *
 *  Once a batch accumulates this many objects it is handed off to the backend.
 *  Because a new batch begins accumulating while the previous one is being
 *  written to disk (double-buffer pattern), peak in-flight memory for pending
 *  objects can reach approximately twice this limit.
 */
static constexpr auto kBATCH_WRITE_LIMIT_SIZE = 65536;

/** Return codes from `Backend` fetch and store operations.
 *
 *  Values 0–99 are reserved for the standard codes defined here. Backend
 *  implementations that need additional error distinctions must use values
 *  starting at `CustomCode` (100) to avoid collisions.
 */
enum class Status {
    Ok = 0,           /**< Operation completed successfully. */
    NotFound = 1,     /**< Key is not present in the store. */
    DataCorrupt = 2,  /**< Stored blob failed integrity validation. */
    Unknown = 3,      /**< An unclassified error occurred. */
    BackendError = 4, /**< The underlying storage backend reported an error. */

    /** First value available for backend-defined extended error codes.
     *  Backend implementations may define their own codes as
     *  `static_cast<int>(Status::CustomCode) + N` without colliding with the
     *  standard range (0–99).
     */
    CustomCode = 100
};

/** A collection of `NodeObject`s to be written together in a single batch.
 *
 *  Using a named alias rather than spelling out the type at every call site
 *  means that a change to the container type or ownership model propagates
 *  from this single definition. The `shared_ptr` element type reflects that
 *  individual `NodeObject` instances may be concurrently referenced by
 *  in-memory caches and the write pipeline at the same time.
 *
 *  @see Backend::storeBatch
 */
using Batch = std::vector<std::shared_ptr<NodeObject>>;

}  // namespace xrpl::NodeStore
