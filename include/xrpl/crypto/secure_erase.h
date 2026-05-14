/** @file
 *  Declares `xrpl::secureErase`, the canonical primitive for wiping
 *  sensitive key material from memory in a way that survives compiler
 *  dead-store elimination.
 */

#pragma once

#include <cstddef>

namespace xrpl {

/** Best-effort wipe of a memory region containing sensitive data.
 *
 *  Overwrites `bytes` bytes starting at `dest` using `OPENSSL_cleanse`,
 *  which employs volatile writes or memory barriers to prevent the compiler
 *  from eliminating the store as a dead write.  The function is defined in a
 *  separate translation unit (`secure_erase.cpp`) so the call is always
 *  opaque to the optimizer at the call site, reinforcing the effect.
 *
 *  Use this instead of `memset` whenever clearing key material, seeds, or
 *  derived intermediates.  The canonical pattern is to call it in destructors
 *  and immediately after copying raw key bytes into their final owner object.
 *
 *  @param dest  Pointer to the memory region to wipe.  Must not be null and
 *      must point to at least `bytes` bytes of writable memory.
 *  @param bytes Number of bytes to overwrite.
 *
 *  @note This is a best-effort mitigation, not a guarantee of complete
 *      erasure.  Register contents, CPU caches, and other micro-architectural
 *      state are outside its reach.  For a thorough discussion of the
 *      inherent limits see Colin Percival's analysis:
 *      http://www.daemonology.net/blog/2014-09-04-how-to-zero-a-buffer.html
 *      http://www.daemonology.net/blog/2014-09-06-zeroing-buffers-is-insufficient.html
 */
void
secureErase(void* dest, std::size_t bytes);

}  // namespace xrpl
