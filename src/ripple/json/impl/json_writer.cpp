//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/json/json_writer.h>
#include <cassert>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace Json
{

static bool isControlCharacter (char ch)
{
    return ch > 0 && ch <= 0x1F;
}

static bool containsControlCharacter ( const char* str )
{
    while ( *str )
    {
        if ( isControlCharacter ( * (str++) ) )
            return true;
    }

    return false;
}
static void uintToString ( unsigned int value,
                           char*& current )
{
    *--current = 0;

    do
    {
        *--current = (value % 10) + '0';
        value /= 10;
    }
    while ( value != 0 );
}

std::string valueToString ( Int value )
{
    char buffer[32];
    char* current = buffer + sizeof (buffer);
    bool isNegative = value < 0;

    if ( isNegative )
        value = -value;

    uintToString ( UInt (value), current );

    if ( isNegative )
        *--current = '-';

    assert ( current >= buffer );
    return current;
}


std::string valueToString ( UInt value )
{
    char buffer[32];
    char* current = buffer + sizeof (buffer);
    uintToString ( value, current );
    assert ( current >= buffer );
    return current;
}

std::string valueToString( double value )
{
    // Allocate a buffer that is more than large enough to store the 16 digits of
    // precision requested below.
    char buffer[32];
    // Print into the buffer. We need not request the alternative representation
    // that always has a decimal point because JSON doesn't distingish the
    // concepts of reals and integers.
#if defined(_MSC_VER) && defined(__STDC_SECURE_LIB__) // Use secure version with visual studio 2005 to avoid warning.
    sprintf_s(buffer, sizeof(buffer), "%.16g", value);
#else
    snprintf(buffer, sizeof(buffer), "%.16g", value);
#endif
    return buffer;
}

std::string valueToString ( bool value )
{
    return value ? "true" : "false";
}

std::string valueToQuotedString ( const char* value )
{
    // Not sure how to handle unicode...
    if (strpbrk (value, "\"\\\b\f\n\r\t") == nullptr && !containsControlCharacter ( value ))
        return std::string ("\"") + value + "\"";

    // We have to walk value and escape any special characters.
    // Appending to std::string is not efficient, but this should be rare.
    // (Note: forward slashes are *not* rare, but I am not escaping them.)
    unsigned maxsize = strlen (value) * 2 + 3; // allescaped+quotes+NULL
    std::string result;
    result.reserve (maxsize); // to avoid lots of mallocs
    result += "\"";

    for (const char* c = value; *c != 0; ++c)
    {
        switch (*c)
        {
        case '\"':
            result += "\\\"";
            break;

        case '\\':
            result += "\\\\";
            break;

        case '\b':
            result += "\\b";
            break;

        case '\f':
            result += "\\f";
            break;

        case '\n':
            result += "\\n";
            break;

        case '\r':
            result += "\\r";
            break;

        case '\t':
            result += "\\t";
            break;

            //case '/':
            // Even though \/ is considered a legal escape in JSON, a bare
            // slash is also legal, so I see no reason to escape it.
            // (I hope I am not misunderstanding something.
            // blep notes: actually escaping \/ may be useful in javascript to avoid </
            // sequence.
            // Should add a flag to allow this compatibility mode and prevent this
            // sequence from occurring.
        default:
            if ( isControlCharacter ( *c ) )
            {
                std::ostringstream oss;
                oss << "\\u" << std::hex << std::uppercase << std::setfill ('0') << std::setw (4) << static_cast<int> (*c);
                result += oss.str ();
            }
            else
            {
                result += *c;
            }

            break;
        }
    }

    result += "\"";
    return result;
}

// Class FastWriter
// //////////////////////////////////////////////////////////////////

FastWriter::FastWriter ()
{
}

std::string
FastWriter::write ( const Value& root )
{
    document_ = "";
    writeValue ( root );
    return std::move (document_);
}


void
FastWriter::writeValue ( const Value& value )
{
    switch ( value.type () )
    {
    case nullValue:
        document_ += "null";
        break;

    case intValue:
        document_ += valueToString ( value.asInt () );
        break;

    case uintValue:
        document_ += valueToString ( value.asUInt () );
        break;

    case realValue:
        document_ += valueToString ( value.asDouble () );
        break;

    case stringValue:
        document_ += valueToQuotedString ( value.asCString () );
        break;

    case booleanValue:
        document_ += valueToString ( value.asBool () );
        break;

    case arrayValue:
    {
        document_ += "[";
        int size = value.size ();

        for ( int index = 0; index < size; ++index )
        {
            if ( index > 0 )
                document_ += ",";

            writeValue ( value[index] );
        }

        document_ += "]";
    }
    break;

    case objectValue:
    {
        Value::Members members ( value.getMemberNames () );
        document_ += "{";

        for ( Value::Members::iterator it = members.begin ();
                it != members.end ();
                ++it )
        {
            std::string const& name = *it;

            if ( it != members.begin () )
                document_ += ",";

            document_ += valueToQuotedString ( name.c_str () );
            document_ += ":";
            writeValue ( value[name] );
        }

        document_ += "}";
    }
    break;
    }
}


// Class StyledWriter
// //////////////////////////////////////////////////////////////////

StyledWriter::StyledWriter ()
    : rightMargin_ ( 74 )
    , indentSize_ ( 3 )
{
}


std::string
StyledWriter::write ( const Value& root )
{
    document_ = "";
    addChildValues_ = false;
    indentString_ = "";
    writeValue ( root );
    document_ += "\n";
    return document_;
}


void
StyledWriter::writeValue ( const Value& value )
{
    switch ( value.type () )
    {
    case nullValue:
        pushValue ( "null" );
        break;

    case intValue:
        pushValue ( valueToString ( value.asInt () ) );
        break;

    case uintValue:
        pushValue ( valueToString ( value.asUInt () ) );
        break;

    case realValue:
        pushValue ( valueToString ( value.asDouble () ) );
        break;

    case stringValue:
        pushValue ( valueToQuotedString ( value.asCString () ) );
        break;

    case booleanValue:
        pushValue ( valueToString ( value.asBool () ) );
        break;

    case arrayValue:
        writeArrayValue ( value);
        break;

    case objectValue:
    {
        Value::Members members ( value.getMemberNames () );

        if ( members.empty () )
            pushValue ( "{}" );
        else
        {
            writeWithIndent ( "{" );
            indent ();
            Value::Members::iterator it = members.begin ();

            while ( true )
            {
                std::string const& name = *it;
                const Value& childValue = value[name];
                writeWithIndent ( valueToQuotedString ( name.c_str () ) );
                document_ += " : ";
                writeValue ( childValue );

                if ( ++it == members.end () )
                    break;

                document_ += ",";
            }

            unindent ();
            writeWithIndent ( "}" );
        }
    }
    break;
    }
}


void
StyledWriter::writeArrayValue ( const Value& value )
{
    unsigned size = value.size ();

    if ( size == 0 )
        pushValue ( "[]" );
    else
    {
        bool isArrayMultiLine = isMultineArray ( value );

        if ( isArrayMultiLine )
        {
            writeWithIndent ( "[" );
            indent ();
            bool hasChildValue = !childValues_.empty ();
            unsigned index = 0;

            while ( true )
            {
                const Value& childValue = value[index];

                if ( hasChildValue )
                    writeWithIndent ( childValues_[index] );
                else
                {
                    writeIndent ();
                    writeValue ( childValue );
                }

                if ( ++index == size )
                    break;

                document_ += ",";
            }

            unindent ();
            writeWithIndent ( "]" );
        }
        else // output on a single line
        {
            assert ( childValues_.size () == size );
            document_ += "[ ";

            for ( unsigned index = 0; index < size; ++index )
            {
                if ( index > 0 )
                    document_ += ", ";

                document_ += childValues_[index];
            }

            document_ += " ]";
        }
    }
}


bool
StyledWriter::isMultineArray ( const Value& value )
{
    int size = value.size ();
    bool isMultiLine = size * 3 >= rightMargin_ ;
    childValues_.clear ();

    for ( int index = 0; index < size  &&  !isMultiLine; ++index )
    {
        const Value& childValue = value[index];
        isMultiLine = isMultiLine  ||
                      ( (childValue.isArray()  ||  childValue.isObject())  &&
                        childValue.size () > 0 );
    }

    if ( !isMultiLine ) // check if line length > max line length
    {
        childValues_.reserve ( size );
        addChildValues_ = true;
        int lineLength = 4 + (size - 1) * 2; // '[ ' + ', '*n + ' ]'

        for ( int index = 0; index < size; ++index )
        {
            writeValue ( value[index] );
            lineLength += int ( childValues_[index].length () );
        }

        addChildValues_ = false;
        isMultiLine = isMultiLine  ||  lineLength >= rightMargin_;
    }

    return isMultiLine;
}


void
StyledWriter::pushValue ( std::string const& value )
{
    if ( addChildValues_ )
        childValues_.push_back ( value );
    else
        document_ += value;
}


void
StyledWriter::writeIndent ()
{
    if ( !document_.empty () )
    {
        char last = document_[document_.length () - 1];

        if ( last == ' ' )     // already indented
            return;

        if ( last != '\n' )    // Comments may add new-line
            document_ += '\n';
    }

    document_ += indentString_;
}


void
StyledWriter::writeWithIndent ( std::string const& value )
{
    writeIndent ();
    document_ += value;
}


void
StyledWriter::indent ()
{
    indentString_ += std::string ( indentSize_, ' ' );
}


void
StyledWriter::unindent ()
{
    assert ( int (indentString_.size ()) >= indentSize_ );
    indentString_.resize ( indentString_.size () - indentSize_ );
}

// Class StyledStreamWriter
// //////////////////////////////////////////////////////////////////

StyledStreamWriter::StyledStreamWriter ( std::string indentation )
    : document_ (nullptr)
    , rightMargin_ ( 74 )
    , indentation_ ( indentation )
{
}


void
StyledStreamWriter::write ( std::ostream& out, const Value& root )
{
    document_ = &out;
    addChildValues_ = false;
    indentString_ = "";
    writeValue ( root );
    *document_ << "\n";
    document_ = nullptr; // Forget the stream, for safety.
}


void
StyledStreamWriter::writeValue ( const Value& value )
{
    switch ( value.type () )
    {
    case nullValue:
        pushValue ( "null" );
        break;

    case intValue:
        pushValue ( valueToString ( value.asInt () ) );
        break;

    case uintValue:
        pushValue ( valueToString ( value.asUInt () ) );
        break;

    case realValue:
        pushValue ( valueToString ( value.asDouble () ) );
        break;

    case stringValue:
        pushValue ( valueToQuotedString ( value.asCString () ) );
        break;

    case booleanValue:
        pushValue ( valueToString ( value.asBool () ) );
        break;

    case arrayValue:
        writeArrayValue ( value);
        break;

    case objectValue:
    {
        Value::Members members ( value.getMemberNames () );

        if ( members.empty () )
            pushValue ( "{}" );
        else
        {
            writeWithIndent ( "{" );
            indent ();
            Value::Members::iterator it = members.begin ();

            while ( true )
            {
                std::string const& name = *it;
                const Value& childValue = value[name];
                writeWithIndent ( valueToQuotedString ( name.c_str () ) );
                *document_ << " : ";
                writeValue ( childValue );

                if ( ++it == members.end () )
                    break;

                *document_ << ",";
            }

            unindent ();
            writeWithIndent ( "}" );
        }
    }
    break;
    }
}


void
StyledStreamWriter::writeArrayValue ( const Value& value )
{
    unsigned size = value.size ();

    if ( size == 0 )
        pushValue ( "[]" );
    else
    {
        bool isArrayMultiLine = isMultineArray ( value );

        if ( isArrayMultiLine )
        {
            writeWithIndent ( "[" );
            indent ();
            bool hasChildValue = !childValues_.empty ();
            unsigned index = 0;

            while ( true )
            {
                const Value& childValue = value[index];

                if ( hasChildValue )
                    writeWithIndent ( childValues_[index] );
                else
                {
                    writeIndent ();
                    writeValue ( childValue );
                }

                if ( ++index == size )
                    break;

                *document_ << ",";
            }

            unindent ();
            writeWithIndent ( "]" );
        }
        else // output on a single line
        {
            assert ( childValues_.size () == size );
            *document_ << "[ ";

            for ( unsigned index = 0; index < size; ++index )
            {
                if ( index > 0 )
                    *document_ << ", ";

                *document_ << childValues_[index];
            }

            *document_ << " ]";
        }
    }
}


bool
StyledStreamWriter::isMultineArray ( const Value& value )
{
    int size = value.size ();
    bool isMultiLine = size * 3 >= rightMargin_ ;
    childValues_.clear ();

    for ( int index = 0; index < size  &&  !isMultiLine; ++index )
    {
        const Value& childValue = value[index];
        isMultiLine = isMultiLine  ||
                      ( (childValue.isArray()  ||  childValue.isObject())  &&
                        childValue.size () > 0 );
    }

    if ( !isMultiLine ) // check if line length > max line length
    {
        childValues_.reserve ( size );
        addChildValues_ = true;
        int lineLength = 4 + (size - 1) * 2; // '[ ' + ', '*n + ' ]'

        for ( int index = 0; index < size; ++index )
        {
            writeValue ( value[index] );
            lineLength += int ( childValues_[index].length () );
        }

        addChildValues_ = false;
        isMultiLine = isMultiLine  ||  lineLength >= rightMargin_;
    }

    return isMultiLine;
}


void
StyledStreamWriter::pushValue ( std::string const& value )
{
    if ( addChildValues_ )
        childValues_.push_back ( value );
    else
        *document_ << value;
}


void
StyledStreamWriter::writeIndent ()
{
    /*
      Some comments in this method would have been nice. ;-)

     if ( !document_.empty() )
     {
        char last = document_[document_.length()-1];
        if ( last == ' ' )     // already indented
           return;
        if ( last != '\n' )    // Comments may add new-line
           *document_ << '\n';
     }
    */
    *document_ << '\n' << indentString_;
}


void
StyledStreamWriter::writeWithIndent ( std::string const& value )
{
    writeIndent ();
    *document_ << value;
}


void
StyledStreamWriter::indent ()
{
    indentString_ += indentation_;
}


void
StyledStreamWriter::unindent ()
{
    assert ( indentString_.size () >= indentation_.size () );
    indentString_.resize ( indentString_.size () - indentation_.size () );
}


std::ostream& operator<< ( std::ostream& sout, const Value& root )
{
    Json::StyledStreamWriter writer;
    writer.write (sout, root);
    return sout;
}

} // namespace Json
