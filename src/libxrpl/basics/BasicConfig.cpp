/** @file
 *  Implements `Section` and `BasicConfig`, the mid-layer of the XRPL
 *  configuration subsystem.  `Section` parses raw INI lines into structured
 *  key-value maps while preserving ordering and handling comment syntax.
 *  `BasicConfig` owns a named map of `Section` objects and exposes the query
 *  and mutation interface consumed by module-specific config readers.
 */

#include <xrpl/basics/BasicConfig.h>

#include <xrpl/basics/StringUtilities.h>

#include <boost/regex/v5/regbase.hpp>
#include <boost/regex/v5/regex.hpp>
#include <boost/regex/v5/regex_match.hpp>

#include <ostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl {

/** Construct a Section with the given name and empty storage. */
Section::Section(std::string name) : name_(std::move(name))
{
}

/** Store or replace a key/value pair in the lookup map.
 *
 *  Uses `insert_or_assign` so repeated calls always take the latest value
 *  without error — this is the path taken by both `append()` (file load)
 *  and `BasicConfig::overwrite()` (command-line injection).
 *
 *  @param key   Setting name.  Must be a valid identifier; enforcement is the
 *      caller's responsibility.
 *  @param value Setting value, after any comment stripping performed by
 *      `append()`.
 */
void
Section::set(std::string const& key, std::string const& value)
{
    lookup_.insert_or_assign(key, value);
}

/** Parse and ingest a batch of raw INI lines into this section.
 *
 *  Each line is processed in two passes:
 *
 *  1. **Comment stripping** — an inline `removeComment` lambda scans for `#`.
 *     A backslash-escaped `\#` has the backslash removed and scanning
 *     resumes; a bare `#` truncates the string at that point (trailing
 *     whitespace is also stripped) and sets `hadTrailingComments_`.
 *     A leading `#` zeroes the entire line (whole-line comment).
 *     Empty lines after stripping are skipped.
 *
 *  2. **Key-value matching** — the cleaned line is tested against the regex
 *     `^(\s*)([a-zA-Z][_a-zA-Z0-9]*)(\s*)=(\s*)(.*\S+)(\s*)`.
 *     Keys must start with a letter and consist only of alphanumerics and
 *     underscores; values must contain at least one non-whitespace character.
 *     Matching lines are stored in `lookup_` via `set()`.  Non-matching lines
 *     are pushed to `values_` — this is intentional: many config sections
 *     contain positional entries (peer IPs, validator public keys, file paths)
 *     that are not key-value pairs.
 *
 *  Every non-empty post-strip line (whether key-value or positional) is also
 *  pushed to `lines_` to preserve the full input record in order.
 *
 *  The `kRE1` regex object is `static const` and compiled exactly once
 *  per process lifetime; `boost::regex_constants::optimize` is passed to
 *  request DFA construction.
 *
 *  @param lines  Raw, unprocessed text lines from the INI tokenizer.
 *  @note  This method is not re-entrant on the same `Section` object, but
 *      multiple calls are safe — subsequent calls accumulate into the
 *      existing storage.
 */
void
Section::append(std::vector<std::string> const& lines)
{
    // Regex for <key> '=' <value> — compiled once for the process lifetime.
    static boost::regex const kRE1(
        "^"                        // start of line
        "(?:\\s*)"                 // whitespace (optional)
        "([a-zA-Z][_a-zA-Z0-9]*)"  // <key>
        "(?:\\s*)"                 // whitespace (optional)
        "(?:=)"                    // '='
        "(?:\\s*)"                 // whitespace (optional)
        "(.*\\S+)"                 // <value>
        "(?:\\s*)"                 // whitespace (optional)
        ,
        boost::regex_constants::optimize);

    lines_.reserve(lines_.size() + lines.size());
    for (auto line : lines)
    {
        auto removeComment = [](std::string& val) -> bool {
            bool removedTrailing = false;
            auto comment = val.find('#');
            while (comment != std::string::npos)
            {
                if (comment == 0)
                {
                    // Whole-line comment; in most cases the file reader has
                    // already filtered these, but handle defensively.
                    val = "";
                    break;
                }
                if (val.at(comment - 1) == '\\')
                {
                    // Escaped '#': remove the backslash and keep scanning
                    // from the same position (the '#' shifts left by one).
                    val.erase(comment - 1, 1);
                }
                else
                {
                    // Unescaped '#': truncate here and report truncation.
                    val = trimWhitespace(val.substr(0, comment));
                    removedTrailing = true;
                    break;
                }

                comment = val.find('#', comment);
            }
            return removedTrailing;
        };

        if (removeComment(line) && !line.empty())
            hadTrailingComments_ = true;

        if (line.empty())
            continue;

        boost::smatch match;
        if (boost::regex_match(line, match, kRE1))
        {
            set(match[1], match[2]);
        }
        else
        {
            values_.push_back(line);
        }

        lines_.push_back(std::move(line));
    }
}

/** Returns `true` if a key with the given name exists in the lookup map.
 *
 *  @param name  The setting key to look up.
 *  @return `true` if the key was present in a `key=value` line seen by
 *      `append()` or injected via `set()`.
 */
bool
Section::exists(std::string const& name) const
{
    return lookup_.contains(name);
}

/** Serialize all key-value pairs in the section as `key=value\n` lines.
 *
 *  Only entries in `lookup_` are emitted; positional `values_` entries and
 *  the original line ordering from `lines_` are not preserved.  Inline
 *  comments stripped during `append()` are also lost.  This means a
 *  round-trip through `operator<<` is lossy when `hadTrailingComments()` is
 *  true.
 *
 *  @param os       Output stream to write to.
 *  @param section  The `Section` whose key-value pairs are serialized.
 *  @return The same output stream.
 */
std::ostream&
operator<<(std::ostream& os, Section const& section)
{
    for (auto const& [k, v] : section.lookup_)
        os << k << "=" << v << "\n";
    return os;
}

//------------------------------------------------------------------------------

/** Returns `true` if a section with the given name exists.
 *
 *  @param name  Section name to look up (case-sensitive).
 *  @return `true` if the section was created during `build()`, via the mutable
 *      `section()` overload, or via `overwrite()`.
 */
bool
BasicConfig::exists(std::string const& name) const
{
    return map_.contains(name);
}

/** Return or create the section with the given name (mutable overload).
 *
 *  Uses `map_.emplace` to auto-create the section on first access, making
 *  this overload appropriate for mutation paths such as `overwrite()`.
 *
 *  @param name  Section name (case-sensitive).
 *  @return Reference to the existing or newly created `Section`.
 */
Section&
BasicConfig::section(std::string const& name)
{
    return map_.emplace(name, name).first->second;
}

/** Return the section with the given name (const overload).
 *
 *  Implements the null-object pattern: when the requested section does not
 *  exist, a reference to an internal `static Section const kNONE("")` is
 *  returned instead of throwing or returning a pointer.  This allows the
 *  common call pattern `config["missing"].get<int>("key")` to safely yield
 *  `std::nullopt` without null checks by the caller.
 *
 *  @param name  Section name (case-sensitive).
 *  @return Reference to the matching section, or to the empty sentinel if
 *      no such section exists.
 */
Section const&
BasicConfig::section(std::string const& name) const
{
    static Section const kNONE("");
    auto const iter = map_.find(name);
    if (iter == map_.end())
        return kNONE;
    return iter->second;
}

/** Inject or replace a single key-value pair, bypassing INI parsing.
 *
 *  Creates the target section if it does not yet exist, then calls
 *  `Section::set()` directly — skipping the comment-stripping and regex
 *  machinery of `Section::append()`.  This is the intended path for
 *  command-line argument injection, where CLI-supplied values must
 *  unconditionally override file-based config regardless of format.
 *
 *  @param section  Name of the section to write into (created on demand).
 *  @param key      Setting name.
 *  @param value    Setting value; stored verbatim, no comment processing.
 */
void
BasicConfig::overwrite(std::string const& section, std::string const& key, std::string const& value)
{
    auto const result =
        map_.emplace(std::piecewise_construct, std::make_tuple(section), std::make_tuple(section));
    result.first->second.set(key, value);
}

/** Replace an existing section with a fresh empty one, discarding all content.
 *
 *  The `deprecated` prefix signals that wholesale erasure of a section is a
 *  design smell; callers of this method are candidates for refactoring.  If
 *  the section does not exist this is a no-op.
 *
 *  @param section  Name of the section to clear.
 */
void
BasicConfig::deprecatedClearSection(std::string const& section)
{
    auto i = map_.find(section);
    if (i != map_.end())
        i->second = Section(section);
}

/** Inject a raw (non-key-value) string into a section's first line slot.
 *
 *  Legacy sections hold a single bare value — e.g. a database path or a
 *  simple flag — rather than key-value pairs.  This setter creates the
 *  section on demand and writes the value via `Section::legacy(string)`,
 *  which sets or overwrites `lines_[0]`.  It is backward-compatibility
 *  scaffolding and not a general-purpose storage mechanism.
 *
 *  @param section  Name of the section to write into (created on demand).
 *  @param value    The raw string to store as the section's sole line.
 */
void
BasicConfig::legacy(std::string const& section, std::string value)
{
    map_.emplace(section, section).first->second.legacy(std::move(value));
}

/** Retrieve the legacy (single-line) value of a section.
 *
 *  Delegates to `Section::legacy()`, which throws `std::runtime_error` if
 *  the section has more than one line — the invariant being that a legacy
 *  section contains exactly one bare value.
 *
 *  @param sectionName  Name of the section to query.
 *  @return The single raw line stored in the section, or an empty string if
 *      the section does not exist or has no lines.
 *  @throws std::runtime_error if the section contains more than one line.
 */
std::string
BasicConfig::legacy(std::string const& sectionName) const
{
    return section(sectionName).legacy();
}

/** Populate `map_` from a tokenized INI file structure.
 *
 *  Iterates the `IniFileSections` map produced by `parseIniFile()` and, for
 *  each entry, emplaces a corresponding `Section` and calls
 *  `Section::append()` with the raw line vector.  This is the sole path by
 *  which file-sourced configuration enters `BasicConfig`.
 *
 *  Declared `protected` so that only the `Config` subclass (which manages the
 *  load sequence) may call it; external callers interact only through the
 *  query and mutation interface.
 *
 *  @param ifs  Tokenized INI data mapping section names to their raw lines,
 *      as returned by `parseIniFile()`.
 */
void
BasicConfig::build(IniFileSections const& ifs)
{
    for (auto const& entry : ifs)
    {
        auto const result = map_.emplace(
            std::piecewise_construct, std::make_tuple(entry.first), std::make_tuple(entry.first));
        result.first->second.append(entry.second);
    }
}

/** Serialize the entire configuration as INI-style text.
 *
 *  For each section, emits a `[section_name]` header followed by the
 *  `key=value\n` output of `operator<<(ostream&, Section const&)`.
 *  Positional `values_` entries and inline comments are not emitted, so the
 *  output is lossy for sections that used those features.
 *
 *  @param ss  Output stream to write to.
 *  @param c   The `BasicConfig` to serialize.
 *  @return The same output stream.
 */
std::ostream&
operator<<(std::ostream& ss, BasicConfig const& c)
{
    for (auto const& [k, v] : c.map_)
        ss << "[" << k << "]\n" << v;
    return ss;
}

}  // namespace xrpl
