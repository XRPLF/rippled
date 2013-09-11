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

#ifndef BEAST_FILELOGGER_H_INCLUDED
#define BEAST_FILELOGGER_H_INCLUDED

//==============================================================================
/**
    A simple implementation of a Logger that writes to a file.

    @see Logger
*/
class BEAST_API FileLogger
    : public Logger
    , LeakChecked <FileLogger>
    , public Uncopyable
{
public:
    //==============================================================================
    /** Creates a FileLogger for a given file.

        @param fileToWriteTo    the file that to use - new messages will be appended
                                to the file. If the file doesn't exist, it will be created,
                                along with any parent directories that are needed.
        @param welcomeMessage   when opened, the logger will write a header to the log, along
                                with the current date and time, and this welcome message
        @param maxInitialFileSizeBytes  if this is zero or greater, then if the file already exists
                                but is larger than this number of bytes, then the start of the
                                file will be truncated to keep the size down. This prevents a log
                                file getting ridiculously large over time. The file will be truncated
                                at a new-line boundary. If this value is less than zero, no size limit
                                will be imposed; if it's zero, the file will always be deleted. Note that
                                the size is only checked once when this object is created - any logging
                                that is done later will be appended without any checking
    */
    FileLogger (const File& fileToWriteTo,
                const String& welcomeMessage,
                const int64 maxInitialFileSizeBytes = 128 * 1024);

    /** Destructor. */
    ~FileLogger();

    //==============================================================================
    /** Returns the file that this logger is writing to. */
    const File& getLogFile() const noexcept               { return logFile; }

    //==============================================================================
    /** Helper function to create a log file in the correct place for this platform.

        The method might return nullptr if the file can't be created for some reason.

        @param logFileSubDirectoryName      the name of the subdirectory to create inside the logs folder (as
                                            returned by getSystemLogFileFolder). It's best to use something
                                            like the name of your application here.
        @param logFileName                  the name of the file to create, e.g. "MyAppLog.txt".
        @param welcomeMessage               a message that will be written to the log when it's opened.
        @param maxInitialFileSizeBytes      (see the FileLogger constructor for more info on this)
    */
    static FileLogger* createDefaultAppLogger (const String& logFileSubDirectoryName,
                                               const String& logFileName,
                                               const String& welcomeMessage,
                                               const int64 maxInitialFileSizeBytes = 128 * 1024);

    /** Helper function to create a log file in the correct place for this platform.

        The filename used is based on the root and suffix strings provided, along with a
        time and date string, meaning that a new, empty log file will be always be created
        rather than appending to an exising one.

        The method might return nullptr if the file can't be created for some reason.

        @param logFileSubDirectoryName      the name of the subdirectory to create inside the logs folder (as
                                            returned by getSystemLogFileFolder). It's best to use something
                                            like the name of your application here.
        @param logFileNameRoot              the start of the filename to use, e.g. "MyAppLog_". This will have
                                            a timestamp and the logFileNameSuffix appended to it
        @param logFileNameSuffix            the file suffix to use, e.g. ".txt"
        @param welcomeMessage               a message that will be written to the log when it's opened.
    */
    static FileLogger* createDateStampedLogger (const String& logFileSubDirectoryName,
                                                const String& logFileNameRoot,
                                                const String& logFileNameSuffix,
                                                const String& welcomeMessage);

    //==============================================================================
    /** Returns an OS-specific folder where log-files should be stored.

        On Windows this will return a logger with a path such as:
        c:\\Documents and Settings\\username\\Application Data\\[logFileSubDirectoryName]\\[logFileName]

        On the Mac it'll create something like:
        ~/Library/Logs/[logFileSubDirectoryName]/[logFileName]

        @see createDefaultAppLogger
    */
    static File getSystemLogFileFolder();

    // (implementation of the Logger virtual method)
    void logMessage (const String&);

private:
    //==============================================================================
    File logFile;
    CriticalSection logLock;

    void trimFileSize (int64 maxFileSizeBytes) const;
};


#endif   // BEAST_FILELOGGER_H_INCLUDED
