#ifndef XRPL_BASICS_ARCHIVE_H_INCLUDED
#define XRPL_BASICS_ARCHIVE_H_INCLUDED

#include <boost/filesystem.hpp>

namespace ripple {

/** Extract a tar archive compressed with lz4

    @param src the path of the archive to be extracted
    @param dst the directory to extract to

    @throws runtime_error
*/
void
extractTarLz4(
    boost::filesystem::path const& src,
    boost::filesystem::path const& dst);

}  // namespace ripple

#endif
