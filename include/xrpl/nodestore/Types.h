#pragma once

#include <xrpl/nodestore/NodeObject.h>

#include <vector>

namespace xrpl::NodeStore {

enum {
    // This is only used to pre-allocate the array for
    // batch objects and does not affect the amount written.
    //
    batchWritePreallocationSize = 256,

    // This sets a limit on the maximum number of writes
    // in a batch. Actual usage can be twice this since
    // we have a new batch growing as we write the old.
    //
    batchWriteLimitSize = 65536
};

/** Return codes from Backend operations. */
enum Status {
    ok = 0,
    notFound = 1,
    dataCorrupt = 2,
    unknown = 3,
    backendError = 4,

    customCode = 100
};

/** A batch of NodeObjects to write at once. */
using Batch = std::vector<std::shared_ptr<NodeObject>>;

}  // namespace xrpl::NodeStore
