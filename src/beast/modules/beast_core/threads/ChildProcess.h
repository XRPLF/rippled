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

#ifndef BEAST_CHILDPROCESS_H_INCLUDED
#define BEAST_CHILDPROCESS_H_INCLUDED


//==============================================================================
/**
    Launches and monitors a child process.

    This class lets you launch an executable, and read its output. You can also
    use it to check whether the child process has finished.
*/
class BEAST_API ChildProcess : LeakChecked <ChildProcess>, public Uncopyable
{
public:
    //==============================================================================
    /** Creates a process object.
        To actually launch the process, use start().
    */
    ChildProcess();

    /** Destructor.
        Note that deleting this object won't terminate the child process.
    */
    ~ChildProcess();

    /** Attempts to launch a child process command.

        The command should be the name of the executable file, followed by any arguments
        that are required.
        If the process has already been launched, this will launch it again. If a problem
        occurs, the method will return false.
    */
    bool start (const String& command);

    /** Attempts to launch a child process command.

        The first argument should be the name of the executable file, followed by any other
        arguments that are needed.
        If the process has already been launched, this will launch it again. If a problem
        occurs, the method will return false.
    */
    bool start (const StringArray& arguments);

    /** Returns true if the child process is alive. */
    bool isRunning() const;

    /** Attempts to read some output from the child process.
        This will attempt to read up to the given number of bytes of data from the
        process. It returns the number of bytes that were actually read.
    */
    int readProcessOutput (void* destBuffer, int numBytesToRead);

    /** Blocks until the process has finished, and then returns its complete output
        as a string.
    */
    String readAllProcessOutput();

    /** Blocks until the process is no longer running. */
    bool waitForProcessToFinish (int timeoutMs) const;

    /** Attempts to kill the child process.
        Returns true if it succeeded. Trying to read from the process after calling this may
        result in undefined behaviour.
    */
    bool kill();

private:
    //==============================================================================
    class ActiveProcess;
    friend class ScopedPointer<ActiveProcess>;
    ScopedPointer<ActiveProcess> activeProcess;
};


#endif   // BEAST_CHILDPROCESS_H_INCLUDED
