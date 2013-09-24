//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#ifndef BEAST_JSON_H_INCLUDED
#define BEAST_JSON_H_INCLUDED

class InputStream;
class OutputStream;
class File;

//==============================================================================
/**
    Contains static methods for converting JSON-formatted text to and from var objects.

    The var class is structurally compatible with JSON-formatted data, so these
    functions allow you to parse JSON into a var object, and to convert a var
    object to JSON-formatted text.

    @see var
*/
class BEAST_API JSON
{
public:
    //==============================================================================
    /** Parses a string of JSON-formatted text, and returns a result code containing
        any parse errors.

        This will return the parsed structure in the parsedResult parameter, and will
        return a Result object to indicate whether parsing was successful, and if not,
        it will contain an error message.

        If you're not interested in the error message, you can use one of the other
        shortcut parse methods, which simply return a var::null if the parsing fails.
    */
    static Result parse (const String& text, var& parsedResult);

    /** Attempts to parse some JSON-formatted text, and returns the result as a var object.

        If the parsing fails, this simply returns var::null - if you need to find out more
        detail about the parse error, use the alternative parse() method which returns a Result.
    */
    static var parse (const String& text);

    /** Attempts to parse some JSON-formatted text from a file, and returns the result
        as a var object.

        Note that this is just a short-cut for reading the entire file into a string and
        parsing the result.

        If the parsing fails, this simply returns var::null - if you need to find out more
        detail about the parse error, use the alternative parse() method which returns a Result.
    */
    static var parse (const File& file);

    /** Attempts to parse some JSON-formatted text from a stream, and returns the result
        as a var object.

        Note that this is just a short-cut for reading the entire stream into a string and
        parsing the result.

        If the parsing fails, this simply returns var::null - if you need to find out more
        detail about the parse error, use the alternative parse() method which returns a Result.
    */
    static var parse (InputStream& input);

    //==============================================================================
    /** Returns a string which contains a JSON-formatted representation of the var object.
        If allOnOneLine is true, the result will be compacted into a single line of text
        with no carriage-returns. If false, it will be laid-out in a more human-readable format.
        @see writeToStream
    */
    static String toString (const var& objectToFormat,
                            bool allOnOneLine = false);

    /** Writes a JSON-formatted representation of the var object to the given stream.
        If allOnOneLine is true, the result will be compacted into a single line of text
        with no carriage-returns. If false, it will be laid-out in a more human-readable format.
        @see toString
    */
    static void writeToStream (OutputStream& output,
                               const var& objectToFormat,
                               bool allOnOneLine = false);

private:
    //==============================================================================
    JSON(); // This class can't be instantiated - just use its static methods.
};


#endif   // BEAST_JSON_H_INCLUDED
