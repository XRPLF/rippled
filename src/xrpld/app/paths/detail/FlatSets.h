#ifndef XRPL_APP_PATHS_IMPL_FLAT_SETS_H_INCLUDED
#define XRPL_APP_PATHS_IMPL_FLAT_SETS_H_INCLUDED

#include <boost/container/flat_set.hpp>

namespace ripple {

/** Given two flat sets dst and src, compute dst = dst union src

    @param dst set to store the resulting union, and also a source of elements
   for the union
    @param src second source of elements for the union
 */
template <class T>
void
SetUnion(
    boost::container::flat_set<T>& dst,
    boost::container::flat_set<T> const& src)
{
    if (src.empty())
        return;

    dst.reserve(dst.size() + src.size());
    dst.insert(
        boost::container::ordered_unique_range_t{}, src.begin(), src.end());
}

}  // namespace ripple

#endif
