#ifndef XRPL_BASICS_BLOB_H_INCLUDED
#define XRPL_BASICS_BLOB_H_INCLUDED

#include <vector>

namespace ripple {

/** Storage for linear binary data.
    Blocks of binary data appear often in various idioms and structures.
*/
using Blob = std::vector<unsigned char>;

}  // namespace ripple

#endif
