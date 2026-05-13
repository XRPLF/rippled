/** @file
 *  Implements `extractTarLz4`, the decompression primitive used when
 *  bootstrapping an XRPL node from a pre-built ledger database snapshot.
 */

#include <xrpl/basics/Archive.h>

#include <xrpl/basics/contract.h>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include <archive.h>
#include <archive_entry.h>

#include <cstddef>
#include <memory>
#include <stdexcept>

namespace xrpl {

/** Decompress and extract a `.tar.lz4` archive into a destination directory.
 *
 *  Uses the libarchive two-handle idiom: a read handle (`ar`) for
 *  decompression and TAR parsing, and a disk-write handle (`aw`) for
 *  filesystem output. Both handles are managed by RAII `unique_ptr` with
 *  custom deleters so they are released on any exit path, including throws.
 *
 *  The reader is narrowed explicitly to TAR + LZ4 rather than using
 *  auto-detect, so passing a different archive format fails early with a
 *  clear error. The writer restores timestamps, permissions, ACLs, and file
 *  flags (`ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL
 *  | ARCHIVE_EXTRACT_FFLAGS`) and resolves user/group names via the local
 *  system's user database.
 *
 *  Every stored entry pathname is prefixed with `dst` before writing, so all
 *  output lands under the caller-supplied destination regardless of the paths
 *  recorded in the archive.
 *
 *  @param src Path to the `.tar.lz4` source file; must be a regular file.
 *  @param dst Destination directory under which all entries are extracted.
 *  @throws std::runtime_error if `src` is not a regular file, if any
 *      libarchive call fails, or if a filesystem write error occurs. On
 *      error mid-extraction the destination is left in a partial state;
 *      callers that require atomicity must clean up themselves.
 *  @note The pathname rewrite prepends `dst` but does not strip `..`
 *      components from stored entry names. Archives from untrusted sources
 *      could use paths such as `../../etc/cron.d/evil` to escape `dst`.
 *      This is acceptable for the trusted first-party snapshots this
 *      function was designed for, but callers should be aware of the
 *      limitation before using it against untrusted input.
 */
void
extractTarLz4(boost::filesystem::path const& src, boost::filesystem::path const& dst)
{
    if (!is_regular_file(src))
        Throw<std::runtime_error>("Invalid source file");

    using archive_ptr = std::unique_ptr<struct archive, void (*)(struct archive*)>;
    archive_ptr const ar{archive_read_new(), [](struct archive* a) { archive_read_free(a); }};
    if (!ar)
        Throw<std::runtime_error>("Failed to allocate archive");

    if (archive_read_support_format_tar(ar.get()) < ARCHIVE_OK)
        Throw<std::runtime_error>(archive_error_string(ar.get()));

    if (archive_read_support_filter_lz4(ar.get()) < ARCHIVE_OK)
        Throw<std::runtime_error>(archive_error_string(ar.get()));

    // 10 240-byte block size matches libarchive's own example code.
    if (archive_read_open_filename(ar.get(), src.string().c_str(), 10240) < ARCHIVE_OK)
    {
        Throw<std::runtime_error>(archive_error_string(ar.get()));
    }

    archive_ptr const aw{
        archive_write_disk_new(), [](struct archive* a) { archive_write_free(a); }};
    if (!aw)
        Throw<std::runtime_error>("Failed to allocate archive");

    if (archive_write_disk_set_options(
            aw.get(),
            ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL |
                ARCHIVE_EXTRACT_FFLAGS) < ARCHIVE_OK)
    {
        Throw<std::runtime_error>(archive_error_string(aw.get()));
    }

    if (archive_write_disk_set_standard_lookup(aw.get()) < ARCHIVE_OK)
        Throw<std::runtime_error>(archive_error_string(aw.get()));

    int result = 0;
    struct archive_entry* entry = nullptr;
    while (true)
    {
        result = archive_read_next_header(ar.get(), &entry);
        if (result == ARCHIVE_EOF)
            break;
        if (result < ARCHIVE_OK)
            Throw<std::runtime_error>(archive_error_string(ar.get()));

        archive_entry_set_pathname(entry, (dst / archive_entry_pathname(entry)).string().c_str());
        if (archive_write_header(aw.get(), entry) < ARCHIVE_OK)
            Throw<std::runtime_error>(archive_error_string(aw.get()));

        if (archive_entry_size(entry) > 0)
        {
            void const* buf = nullptr;
            size_t sz = 0;
            la_int64_t offset = 0;
            while (true)
            {
                result = archive_read_data_block(ar.get(), &buf, &sz, &offset);
                if (result == ARCHIVE_EOF)
                    break;
                if (result < ARCHIVE_OK)
                    Throw<std::runtime_error>(archive_error_string(ar.get()));

                if (archive_write_data_block(aw.get(), buf, sz, offset) < ARCHIVE_OK)
                {
                    Throw<std::runtime_error>(archive_error_string(aw.get()));
                }
            }
        }

        if (archive_write_finish_entry(aw.get()) < ARCHIVE_OK)
            Throw<std::runtime_error>(archive_error_string(aw.get()));
    }
}

}  // namespace xrpl
