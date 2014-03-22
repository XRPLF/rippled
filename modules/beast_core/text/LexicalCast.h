//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_LEXICALCAST_H_INCLUDED
#define BEAST_LEXICALCAST_H_INCLUDED

namespace beast
{

// Base class with utility functions
struct LexicalCastUtilities
{
    static unsigned char const s_digitTable [256];

    // strict string to integer parser
    template <class IntType, class InputIterator>
    static inline bool parseSigned (IntType& result, InputIterator begin, InputIterator end)
    {
        if (0 == std::distance (begin, end))
            return false;

        std::uint64_t accum = 0;
        InputIterator it = begin;

        // process sign
        bool negative = false;
        if ('+' == *it)
        {
            ++it;
        }
        else if ('-' == *it)
        {
            ++it;
            negative = true;
        }
        if (end == it)
            return false;

        // calc max of abs value
        std::uint64_t max;
        if (negative)
            max = static_cast <std::uint64_t> (
                -(static_cast <std::int64_t> (std::numeric_limits <IntType>::min ())));
        else
            max = std::numeric_limits <IntType>::max ();

        // process digits
        while (end != it)
        {
            std::uint64_t const digit = static_cast <IntType> (
                s_digitTable [static_cast <unsigned int> (*it++)]);

            if (0xFF == digit)
                return false;

            std::uint64_t const overflow = (max - digit) / 10;

            if (accum > overflow)
                return false;

            accum = (10 * accum) + digit;
        }

        if (negative)
        {
            result = -static_cast <IntType> (accum);
        }
        else
        {
            result = static_cast <IntType> (accum);
        }

        return true;
    }

    template <class IntType, class InputIterator>
    static inline bool parseUnsigned (IntType& result, InputIterator begin, InputIterator end)
    {
        if (0 == std::distance (begin, end))
            return false;

        std::uint64_t accum = 0;
        InputIterator it = begin;
        std::uint64_t const max = std::numeric_limits <IntType>::max ();

        // process digits
        while (end != it)
        {
            std::uint64_t const digit = static_cast <IntType> (
                s_digitTable [static_cast <unsigned int> (*it++)]);

            if (0xFF == digit)
                return false;

            std::uint64_t const overflow = (max - digit) / 10;

            if (accum > overflow)
                return false;

            accum = (10 * accum) + digit;
        }

        result = static_cast <IntType> (accum);

        return true;
    }
};

//------------------------------------------------------------------------------

/** This is thrown when a conversion is not possible.
    Only used in the throw variants of lexicalCast.
*/
struct BadLexicalCast : public std::bad_cast
{
};

// These specializatons get called by the non-member functions to do the work
template <class Out, class In>
struct LexicalCast;

// conversion to String
template <class In>
struct LexicalCast <String, In>
{
    bool operator() (String& out, int in) const            { out = String (in); return true; }
    bool operator() (String& out, unsigned int in) const   { out = String (in); return true; }
    bool operator() (String& out, short in) const          { out = String (in); return true; }
    bool operator() (String& out, unsigned short in) const { out = String (in); return true; }
    bool operator() (String& out, std::int64_t in) const          { out = String (in); return true; }
    bool operator() (String& out, std::uint64_t in) const         { out = String (in); return true; }
    bool operator() (String& out, float in) const          { out = String (in); return true; }
    bool operator() (String& out, double in) const         { out = String (in); return true; }
};

// Parse String to number
template <class Out>
struct LexicalCast <Out, String>
{
    bool operator() (int& out,            String const& in) const { std::string const& s (in.toStdString ()); return LexicalCastUtilities::parseSigned (out, s.begin (), s.end ()); }
    bool operator() (short& out,          String const& in) const { std::string const& s (in.toStdString ()); return LexicalCastUtilities::parseSigned (out, s.begin (), s.end ()); }
    bool operator() (std::int64_t& out,          String const& in) const { std::string const& s (in.toStdString ()); return LexicalCastUtilities::parseSigned (out, s.begin (), s.end ()); }
    bool operator() (unsigned int& out,   String const& in) const { std::string const& s (in.toStdString ()); return LexicalCastUtilities::parseUnsigned (out, s.begin (), s.end ()); }
    bool operator() (unsigned short& out, String const& in) const { std::string const& s (in.toStdString ()); return LexicalCastUtilities::parseUnsigned (out, s.begin (), s.end ()); }
    bool operator() (std::uint64_t& out,         String const& in) const { std::string const& s (in.toStdString ()); return LexicalCastUtilities::parseUnsigned (out, s.begin (), s.end ()); }
    bool operator() (float& out,          String const& in) const { bassertfalse; return false; /* UNIMPLEMENTED! */ }
    bool operator() (double& out,         String const& in) const { bassertfalse; return false; /* UNIMPLEMENTED! */ }

    bool operator () (bool& out, String const& in) const
    {
        // boost::lexical_cast is very strict, it
        // throws on anything but "1" or "0"
        //
        if (in == "1")
        {
            out = true;
            return true;
        }
        else if (in == "0")
        {
            out = false;
            return true;
        }

        return false;
    }
};

//------------------------------------------------------------------------------

// Conversion to std::string
template <class In>
struct LexicalCast <std::string, In>
{
    bool operator() (std::string& out, In in) const
    {
        String s;

        if (LexicalCast <String, In> () (s, in))
        {
            out = s.toStdString ();
            return true;
        }

        return false;
    }
};

// Conversion from std::string
template <class Out>
struct LexicalCast <Out, std::string>
{
    bool operator() (Out& out, std::string const& in) const
    {
        Out result;

        if (LexicalCast <Out, String> () (result, String (in.c_str ())))
        {
            out = result;
            return true;
        }

        return false;
    }
};

// Conversion from null terminated char const*
template <class Out>
struct LexicalCast <Out, char const*>
{
    bool operator() (Out& out, char const* in) const
    {
        Out result;

        if (LexicalCast <Out, String> () (result, String (in)))
        {
            out = result;
            return true;
        }

        return false;
    }
};

// Conversion from null terminated char*
// The string is not modified.
template <class Out>
struct LexicalCast <Out, char*>
{
    bool operator() (Out& out, char* in) const
    {
        Out result;

        if (LexicalCast <Out, char const*> () (result, in))
        {
            out = result;
            return true;
        }

        return false;
    }
};

//------------------------------------------------------------------------------

/** Intelligently convert from one type to another.
    @return `false` if there was a parsing or range error
*/
template <class Out, class In>
bool lexicalCastChecked (Out& out, In in)
{
    return LexicalCast <Out, In> () (out, in);
}

/** Convert from one type to another, throw on error

    An exception of type BadLexicalCast is thrown if the conversion fails.

    @return The new type.
*/
template <class Out, class In>
Out lexicalCastThrow (In in)
{
    Out out;

    if (lexicalCastChecked (out, in))
        return out;

    throw BadLexicalCast ();

    return Out ();
}

/** Convert from one type to another.

    @param defaultValue The value returned if parsing fails
    @return The new type.
*/
template <class Out, class In>
Out lexicalCast (In in, Out defaultValue = Out ())
{
    Out out;

    if (lexicalCastChecked (out, in))
        return out;

    return defaultValue;
}

} // beast

#endif
