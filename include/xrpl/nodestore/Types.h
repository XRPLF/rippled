#ifndef XRPL_NODESTORE_TYPES_H_INCLUDED
#define XRPL_NODESTORE_TYPES_H_INCLUDED

#include <xrpl/nodestore/NodeObject.h>

#include <vector>

namespace ripple {
namespace NodeStore {

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
    ok,
    notFound,
    dataCorrupt,
    unknown,
    backendError,

    customCode = 100
};

/** A batch of NodeObjects to write at once. */
using Batch = std::vector<std::shared_ptr<NodeObject>>;

}  // namespace NodeStore

}  // namespace ripple

#endif
