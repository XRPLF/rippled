#pragma once

#include <xrpl/nodestore/NodeObject.h>

#include <vector>

namespace xrpl::NodeStore {

// This is only used to pre-allocate the array for
// batch objects and does not affect the amount written.
//
static constexpr auto kBatchWritePreallocationSize = 256;

// This sets a limit on the maximum number of writes
// in a batch. Actual usage can be twice this since
// we have a new batch growing as we write the old.
//
static constexpr auto kBatchWriteLimitSize = 65536;

/** Return codes from Backend operations. */
enum class Status {
    Ok = 0,
    NotFound = 1,
    DataCorrupt = 2,
    Unknown = 3,
    BackendError = 4,

    CustomCode = 100
};

/** A batch of NodeObjects to write at once. */
using Batch = std::vector<std::shared_ptr<NodeObject>>;

}  // namespace xrpl::NodeStore
