#pragma once

#include <cstddef>
#include <ios>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>

namespace beast {

// A collection of handy stream manipulators and
// functions to produce nice looking log output.

/** Left justifies a field at the specified width. */
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

/** Produce a section heading and fill the rest of the line with dashes. */
template <class CharT, class Traits, class Allocator>
std::basic_string<CharT, Traits, Allocator>
heading(std::basic_string<CharT, Traits, Allocator> title, int width = 80, CharT fill = CharT('-'))
{
    title.reserve(width);
    title.push_back(CharT(' '));
    title.resize(width, fill);
    return title;
}

/** Produce a dashed line separator, with a specified or default size. */
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

/** Creates a padded field with an optional fill character. */
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

template <typename T>
std::string
to_string(T const& t)
{
    std::stringstream ss;
    ss << t;
    return ss.str();
}

}  // namespace detail

/** Justifies a field at the specified width. */
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

template <typename T>
FieldT<char>
field(T const& t, int width = 8, int pad = 0, bool right = false)
{
    std::string const text(detail::to_string(t));
    return field(text, width, pad, right);
}

template <class CharT, class Traits, class Allocator>
FieldT<CharT, Traits, Allocator>
rField(std::basic_string<CharT, Traits, Allocator> const& text, int width = 8, int pad = 0)
{
    return FieldT<CharT, Traits, Allocator>(text, width, pad, true);
}

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

template <typename T>
FieldT<char>
rField(T const& t, int width = 8, int pad = 0)
{
    std::string const text(detail::to_string(t));
    return field(text, width, pad, true);
}
/** @} */

}  // namespace beast
