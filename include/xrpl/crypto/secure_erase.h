#ifndef XRPL_CRYPTO_SECURE_ERASE_H_INCLUDED
#define XRPL_CRYPTO_SECURE_ERASE_H_INCLUDED

#include <cstddef>

namespace ripple {

/** Attempts to clear the given blob of memory.

    The underlying implementation of this function takes pains to
    attempt to outsmart the compiler from optimizing the clearing
    away. Please note that, despite that, remnants of content may
    remain floating around in memory as well as registers, caches
    and more.

    For a more in-depth discussion of the subject please see the
    below posts by Colin Percival:

    http://www.daemonology.net/blog/2014-09-04-how-to-zero-a-buffer.html
    http://www.daemonology.net/blog/2014-09-06-zeroing-buffers-is-insufficient.html
*/
void
secure_erase(void* dest, std::size_t bytes);

}  // namespace ripple

#endif
