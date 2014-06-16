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

#ifndef BEAST_LOGGER_H_INCLUDED
#define BEAST_LOGGER_H_INCLUDED

#include <beast/strings/String.h>

namespace beast
{

//==============================================================================
/**
    Acts as an application-wide logging class.

    A subclass of Logger can be created and passed into the Logger::setCurrentLogger
    method and this will then be used by all calls to writeToLog.

    The logger class also contains methods for writing messages to the debugger's
    output stream.
*/
class Logger
{
public:
    //==============================================================================
    /** Destructor. */
    virtual ~Logger();

    //==============================================================================
    /** Sets the current logging class to use.

        Note that the object passed in will not be owned or deleted by the logger, so
        the caller must make sure that it is not deleted while still being used.
        A null pointer can be passed-in to disable any logging.
    */
    static void setCurrentLogger (Logger* newLogger) noexcept;

    /** Returns the current logger, or nullptr if none has been set. */
    static Logger* getCurrentLogger() noexcept;

    /** Writes a string to the current logger.

        This will pass the string to the logger's logMessage() method if a logger
        has been set.

        @see logMessage
    */
    static void writeToLog (const String& message);


    //==============================================================================
    /** Writes a message to the standard error stream.

        This can be called directly, or by using the DBG() macro in
        CompilerConfig.h (which will avoid calling the method in non-debug builds).
    */
    static void outputDebugString (const String& text);


protected:
    //==============================================================================
    Logger();

    /** This is overloaded by subclasses to implement custom logging behaviour.
        @see setCurrentLogger
    */
    virtual void logMessage (const String& message) = 0;

private:
    static Logger* currentLogger;
};

} // beast

#endif   // BEAST_LOGGER_H_INCLUDED
