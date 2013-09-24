//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TYPES_BLOB_H_INCLUDED
#define RIPPLE_TYPES_BLOB_H_INCLUDED

#include <vector>

namespace ripple {

/** Storage for linear binary data.
    Blocks of binary data appear often in various idioms and structures.
*/
typedef std::vector <unsigned char> Blob;

}

#endif
