#pragma once

#include <filesystem>

namespace xrpl {

/** Extract a tar archive compressed with lz4

    @param src the path of the archive to be extracted
    @param dst the directory to extract to

    @throws runtime_error
*/
void
extractTarLz4(std::filesystem::path const& src, std::filesystem::path const& dst);

}  // namespace xrpl
