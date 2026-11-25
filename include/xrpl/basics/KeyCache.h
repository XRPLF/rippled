#pragma once

#include <xrpl/basics/TaggedCache.h>

namespace xrpl {

/**
 * TaggedCache in key-only mode, holding no value per key.
 *
 * @tparam Key the key type to remember.
 */
template <class Key>
using KeyCache = TaggedCache<Key, int, true>;

}  // namespace xrpl
