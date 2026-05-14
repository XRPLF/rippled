#pragma once

#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>

#include <optional>

namespace xrpl {

/** Read the entire contents of a file into a string.
 *
 *  Resolves `sourcePath` to its canonical (absolute, symlink-free) form before
 *  opening it, which prevents TOCTOU races between path resolution and the open.
 *  When `maxSize` is supplied and the file exceeds that byte count, `ec` is set
 *  to `boost::system::errc::file_too_large` and an empty string is returned
 *  without reading any data.
 *
 *  All errors — non-existent path, permission denial, size exceeded, open
 *  failure, and mid-read I/O error — are reported through `ec`. The function
 *  never throws.
 *
 *  @param ec Output error code; set on any failure, left unchanged on success.
 *  @param sourcePath Path to the file to read; must exist and be resolvable.
 *  @param maxSize Optional upper bound on file size in bytes. If the file is
 *      larger, `ec` is set to `errc::file_too_large` and `{}` is returned.
 *  @return The full file contents on success, or an empty string on any error.
 *  @note EOF during the single-pass read is not an error; only `bad()` (hardware
 *      or stream-corruption failure) triggers an error code after the read.
 */
std::string
getFileContents(
    boost::system::error_code& ec,
    boost::filesystem::path const& sourcePath,
    std::optional<std::size_t> maxSize = std::nullopt);

/** Write a string to a file, creating or truncating it as necessary.
 *
 *  Opens `destPath` with `std::ios::out | std::ios::trunc`, so any existing
 *  content is discarded and the file is created if it does not yet exist.
 *  This is a full replacement, not an atomic rename-and-swap; callers that
 *  require crash-safe writes must implement that at a higher level.
 *
 *  All errors — open failure and mid-write I/O error — are reported through
 *  `ec`. The function never throws.
 *
 *  @param ec Output error code; set on any failure, left unchanged on success.
 *  @param destPath Path to the destination file; parent directory must exist.
 *  @param contents Data to write; written in a single `<<` operation.
 *  @note Unlike `getFileContents`, this function does not call `canonical()`
 *      because the destination file may not yet exist.
 */
void
writeFileContents(
    boost::system::error_code& ec,
    boost::filesystem::path const& destPath,
    std::string const& contents);

}  // namespace xrpl
