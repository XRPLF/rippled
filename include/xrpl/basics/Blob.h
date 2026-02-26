#pragma once

#include <vector>

namespace xrpl {

/** Storage for linear binary data.
    Blocks of binary data appear often in various idioms and structures.
*/
using Blob = std::vector<unsigned char>;

}  // namespace xrpl
