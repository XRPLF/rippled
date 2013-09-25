//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_TYPES_H_INCLUDED
#define RIPPLE_NODESTORE_TYPES_H_INCLUDED

namespace NodeStore
{

enum
{
    // This is only used to pre-allocate the array for
    // batch objects and does not affect the amount written.
    //
    batchWritePreallocationSize = 128
};

/** Return codes from Backend operations. */
enum Status
{
    ok,
    notFound,
    dataCorrupt,
    unknown
};

/** A batch of NodeObjects to write at once. */
typedef std::vector <NodeObject::Ptr> Batch;

/** A list of key/value parameter pairs passed to the backend. */
typedef StringPairArray Parameters;

}

#endif
