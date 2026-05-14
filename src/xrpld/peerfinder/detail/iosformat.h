/**
 * @file
 * @brief Stream formatting utilities for PeerFinder diagnostic log output.
 *
 * Provides a small set of stream manipulators and formatting helpers used
 * throughout the PeerFinder subsystem (`Logic`, `Livecache`, `Bootcache`) to
 * produce consistently column-aligned log lines without scattering
 * `std::setw`/`std::left` boilerplate at every log site.
 *
 * The file has no XRPL-specific dependencies and lives in the `beast`
 * namespace, reflecting its origin as a reusable library primitive.
 */

#pragma once

#include <ostream>
#include <sstream>
#include <string>

namespace beast {

/**
 * @brief Stream manipulator that sets left-justification and a fixed field
 *        width on a `std::basic_ios` in a single expression.
 *
 * Unlike manipulators that write characters, `Leftw` modifies the stream's
 * sticky format state via `setf`/`width`. The width effect is consumed by the
 * very next field write (standard `std::ios` semantics), so it is idiomatic
 * to chain it directly before the value:
 *
 * @code
 * JLOG(journal_.debug()) << beast::Leftw(18) << "Livecache insert " << ep.address;
 * @endcode
 *
 * Every PeerFinder log prefix uses `Leftw(18)`, establishing a fixed 18-char
 * column (e.g., `"Livecache insert "`, `"Logic connect "`) before appending
 * variable-length address or count data, making log files scannable without a
 * full structured-logging framework.
 *
 * @note Targets `std::basic_ios`, not `std::ostream` — it fits between a
 *     JLOG expression and its first datum without emitting characters.
 */
struct Leftw
{
    explicit Leftw(int width) : width(width)
    {
    }
    int const width;
    template <class CharT, class Traits>
    friend std::basic_ios<CharT, Traits>&
    operator<<(std::basic_ios<CharT, Traits>& ios, Leftw const& p)
    {
        ios.setf(std::ios_base::left, std::ios_base::adjustfield);
        ios.width(p.width);
        return ios;
    }
};

/**
 * @brief Pad a title string out to a fixed column width with a fill character.
 *
 * Appends a space separator after @p title, then extends the string to
 * @p width characters using @p fill (default: dash). Useful for producing
 * section-break lines in multi-line diagnostic dumps:
 *
 * @code
 * os << beast::heading("Endpoints") << '\n';
 * // "Endpoints ------------------------------------------------------..."
 * @endcode
 *
 * `reserve()` is called upfront so the subsequent `push_back`/`resize`
 * sequence incurs at most one allocation.
 *
 * @param title  Section label; taken by value so the caller's string is not
 *     modified.
 * @param width  Total output length in characters (default: 80). Mirrors the
 *     default of `Divider` so the two can be mixed in box-formatted output.
 * @param fill   Padding character appended after the space separator
 *     (default: `'-'`).
 * @return The padded heading string, ready to stream.
 */
template <class CharT, class Traits, class Allocator>
std::basic_string<CharT, Traits, Allocator>
heading(std::basic_string<CharT, Traits, Allocator> title, int width = 80, CharT fill = CharT('-'))
{
    title.reserve(width);
    title.push_back(CharT(' '));
    title.resize(width, fill);
    return title;
}

/**
 * @brief Streamable solid-line separator for diagnostic output sections.
 *
 * Emits a string of @p width repeated @p fill characters directly to an
 * `std::ostream`. Unlike `heading()`, which returns a `std::string`,
 * `Divider` defers rendering until it is streamed, fitting naturally into
 * chained `operator<<` expressions:
 *
 * @code
 * os << beast::Divider() << '\n';   // 80 dashes
 * os << beast::Divider(40, '=') << '\n';
 * @endcode
 *
 * The default column width of 80 mirrors `heading()` so the two can be used
 * together to produce box-formatted diagnostic sections.
 */
struct Divider
{
    using CharT = char;
    explicit Divider(int width = 80, CharT fill = CharT('-')) : width(width), fill(fill)
    {
    }
    int const width;
    CharT const fill;
    template <class CharT, class Traits>
    friend std::basic_ostream<CharT, Traits>&
    operator<<(std::basic_ostream<CharT, Traits>& os, Divider const& d)
    {
        os << std::basic_string<CharT, Traits>(d.width, d.fill);
        return os;
    }
};

/**
 * @brief Streamable whitespace block for column spacing in tabular output.
 *
 * Emits a fixed block of @p fill characters totalling `width + pad`
 * characters. The constructor merges @p width and @p pad into a single
 * `width_` member so the stream operator emits exactly one string.
 *
 * Useful for visually indenting or spacing columns in tabular diagnostic
 * output when neither left/right justification nor text content is needed —
 * only a fixed-width gap.
 *
 * @param width  Base number of fill characters.
 * @param pad    Additional fill characters merged into `width` at construction
 *     (default: 0).
 * @param fill   Character used to fill the block (default: space).
 */
struct Fpad
{
    explicit Fpad(int width, int pad = 0, char fill = ' ') : width(width + pad), fill(fill)
    {
    }
    int const width;
    char const fill;
    template <class CharT, class Traits>
    friend std::basic_ostream<CharT, Traits>&
    operator<<(std::basic_ostream<CharT, Traits>& os, Fpad const& f)
    {
        os << std::basic_string<CharT, Traits>(f.width, f.fill);
        return os;
    }
};

//------------------------------------------------------------------------------

namespace detail {

/**
 * @brief Convert any streamable value to `std::string` via `std::stringstream`.
 *
 * Used internally by the generic `field()` and `rField()` overloads to accept
 * arbitrary value types (integers, addresses, etc.) without requiring callers
 * to pre-convert. Works for any type that defines `operator<<` for
 * `std::ostream`, at the cost of one heap allocation per call.
 *
 * @tparam T  Any type with a streaming `operator<<`.
 * @param t   Value to convert.
 * @return    String representation produced by streaming @p t.
 */
template <typename T>
std::string
to_string(T const& t)
{
    std::stringstream ss;
    ss << t;
    return ss.str();
}

}  // namespace detail

/**
 * @brief Streamable fixed-width text column with optional trailing pad and
 *        configurable justification.
 *
 * Holds the text content and layout parameters; actual characters are written
 * by `operator<<`. Text shorter than @p width is padded with spaces on the
 * left (right-justified) or the right (left-justified). An additional @p pad
 * space block is appended after the justified content, useful for column
 * gutters in tabular output.
 *
 * Unlike `Leftw`, which modifies stream state and is consumed by the next
 * field write, `FieldT` manages its own padding and is independent of stream
 * format state — both can coexist in the same expression.
 *
 * In practice all usages are narrow-`char` and are constructed via the
 * `field()` / `rField()` factory functions rather than directly.
 *
 * @tparam CharT      Character type.
 * @tparam Traits     Character traits (default: `std::char_traits<CharT>`).
 * @tparam Allocator  Allocator (default: `std::allocator<CharT>`).
 *
 * @see field(), rField()
 */
/** @{ */
template <
    class CharT,
    class Traits = std::char_traits<CharT>,
    class Allocator = std::allocator<CharT>>
class FieldT
{
public:
    using string_t = std::basic_string<CharT, Traits, Allocator>;
    FieldT(string_t const& text, int width, int pad, bool right)
        : text(text), width(width), pad(pad), right(right)
    {
    }
    string_t const text;
    int const width;
    int const pad;
    bool const right;
    template <class CharT2, class Traits2>
    friend std::basic_ostream<CharT2, Traits2>&
    operator<<(std::basic_ostream<CharT2, Traits2>& os, FieldT<CharT, Traits, Allocator> const& f)
    {
        std::size_t const length(f.text.length());
        if (f.right)
        {
            if (length < f.width)
                os << std::basic_string<CharT2, Traits2>(f.width - length, CharT2(' '));
            os << f.text;
        }
        else
        {
            os << f.text;
            if (length < f.width)
                os << std::basic_string<CharT2, Traits2>(f.width - length, CharT2(' '));
        }
        if (f.pad != 0)
            os << string_t(f.pad, CharT(' '));
        return os;
    }
};

/**
 * @brief Construct a left-justified `FieldT` from a `std::basic_string`.
 *
 * @param text   Text to display.
 * @param width  Minimum column width; text shorter than this is padded on the
 *     right (default: 8).
 * @param pad    Extra trailing spaces appended after the justified field
 *     (default: 0).
 * @param right  Set `true` for right-justification (default: `false`).
 * @return A `FieldT` ready to stream.
 */
template <class CharT, class Traits, class Allocator>
FieldT<CharT, Traits, Allocator>
field(
    std::basic_string<CharT, Traits, Allocator> const& text,
    int width = 8,
    int pad = 0,
    bool right = false)
{
    return FieldT<CharT, Traits, Allocator>(text, width, pad, right);
}

/**
 * @brief Construct a left-justified `FieldT` from a null-terminated string.
 *
 * @param text   Null-terminated character array.
 * @param width  Minimum column width (default: 8).
 * @param pad    Extra trailing spaces (default: 0).
 * @param right  Set `true` for right-justification (default: `false`).
 * @return A `FieldT` ready to stream.
 */
template <class CharT>
FieldT<CharT>
field(CharT const* text, int width = 8, int pad = 0, bool right = false)
{
    return FieldT<CharT, std::char_traits<CharT>, std::allocator<CharT>>(
        std::basic_string<CharT, std::char_traits<CharT>, std::allocator<CharT>>(text),
        width,
        pad,
        right);
}

/**
 * @brief Construct a left-justified `FieldT` from any streamable value.
 *
 * Converts @p t to a string via `detail::to_string()` (streams through
 * `std::stringstream`), then delegates to the string overload. Accepts
 * integers, addresses, or any type with `operator<<`.
 *
 * @tparam T     Any type with `operator<<` for `std::ostream`.
 * @param t      Value to display.
 * @param width  Minimum column width (default: 8).
 * @param pad    Extra trailing spaces (default: 0).
 * @param right  Set `true` for right-justification (default: `false`).
 * @return A `FieldT<char>` ready to stream.
 */
template <typename T>
FieldT<char>
field(T const& t, int width = 8, int pad = 0, bool right = false)
{
    std::string const text(detail::to_string(t));
    return field(text, width, pad, right);
}

/**
 * @brief Construct a right-justified `FieldT` from a `std::basic_string`.
 *
 * Named alias for `field(..., right=true)` that makes call sites more
 * readable than passing a boolean flag.
 *
 * @param text   Text to display.
 * @param width  Minimum column width; text shorter than this is padded on the
 *     left (default: 8).
 * @param pad    Extra trailing spaces appended after the justified field
 *     (default: 0).
 * @return A right-justified `FieldT` ready to stream.
 */
template <class CharT, class Traits, class Allocator>
FieldT<CharT, Traits, Allocator>
rField(std::basic_string<CharT, Traits, Allocator> const& text, int width = 8, int pad = 0)
{
    return FieldT<CharT, Traits, Allocator>(text, width, pad, true);
}

/**
 * @brief Construct a right-justified `FieldT` from a null-terminated string.
 *
 * @param text   Null-terminated character array.
 * @param width  Minimum column width (default: 8).
 * @param pad    Extra trailing spaces (default: 0).
 * @return A right-justified `FieldT` ready to stream.
 */
template <class CharT>
FieldT<CharT>
rField(CharT const* text, int width = 8, int pad = 0)
{
    return FieldT<CharT, std::char_traits<CharT>, std::allocator<CharT>>(
        std::basic_string<CharT, std::char_traits<CharT>, std::allocator<CharT>>(text),
        width,
        pad,
        true);
}

/**
 * @brief Construct a right-justified `FieldT` from any streamable value.
 *
 * Converts @p t to a string via `detail::to_string()`, then delegates to the
 * string overload. Equivalent to `field(t, width, pad, true)`.
 *
 * @tparam T     Any type with `operator<<` for `std::ostream`.
 * @param t      Value to display.
 * @param width  Minimum column width (default: 8).
 * @param pad    Extra trailing spaces (default: 0).
 * @return A right-justified `FieldT<char>` ready to stream.
 */
template <typename T>
FieldT<char>
rField(T const& t, int width = 8, int pad = 0)
{
    std::string const text(detail::to_string(t));
    return field(text, width, pad, true);
}
/** @} */

}  // namespace beast
