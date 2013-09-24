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

class NamedPipe::Pimpl : LeakChecked <Pimpl>, public Uncopyable
{
public:
    Pimpl (const String& pipePath, bool createPipe)
       : pipeInName  (pipePath + "_in"),
         pipeOutName (pipePath + "_out"),
         pipeIn (-1), pipeOut (-1),
         createdPipe (createPipe),
         stopReadOperation (false)
    {
        signal (SIGPIPE, signalHandler);
        beast_siginterrupt (SIGPIPE, 1);
    }

    ~Pimpl()
    {
        if (pipeIn  != -1)  ::close (pipeIn);
        if (pipeOut != -1)  ::close (pipeOut);

        if (createdPipe)
        {
            unlink (pipeInName.toUTF8());
            unlink (pipeOutName.toUTF8());
        }
    }

    int read (char* destBuffer, int maxBytesToRead, int timeOutMilliseconds)
    {
        const uint32 timeoutEnd = getTimeoutEnd (timeOutMilliseconds);

        if (pipeIn == -1)
        {
            pipeIn = openPipe (createdPipe ? pipeInName : pipeOutName, O_RDWR | O_NONBLOCK, timeoutEnd);

            if (pipeIn == -1)
                return -1;
        }

        int bytesRead = 0;

        while (bytesRead < maxBytesToRead)
        {
            const int bytesThisTime = maxBytesToRead - bytesRead;
            const int numRead = (int) ::read (pipeIn, destBuffer, (size_t) bytesThisTime);

            if (numRead <= 0)
            {
                if (errno != EWOULDBLOCK || stopReadOperation || hasExpired (timeoutEnd))
                    return -1;

                const int maxWaitingTime = 30;
                waitForInput (pipeIn, timeoutEnd == 0 ? maxWaitingTime
                                                      : bmin (maxWaitingTime,
                                                              (int) (timeoutEnd - Time::getMillisecondCounter())));
                continue;
            }

            bytesRead += numRead;
            destBuffer += numRead;
        }

        return bytesRead;
    }

    int write (const char* sourceBuffer, int numBytesToWrite, int timeOutMilliseconds)
    {
        const uint32 timeoutEnd = getTimeoutEnd (timeOutMilliseconds);

        if (pipeOut == -1)
        {
            pipeOut = openPipe (createdPipe ? pipeOutName : pipeInName, O_WRONLY, timeoutEnd);

            if (pipeOut == -1)
                return -1;
        }

        int bytesWritten = 0;

        while (bytesWritten < numBytesToWrite && ! hasExpired (timeoutEnd))
        {
            const int bytesThisTime = numBytesToWrite - bytesWritten;
            const int numWritten = (int) ::write (pipeOut, sourceBuffer, (size_t) bytesThisTime);

            if (numWritten <= 0)
                return -1;

            bytesWritten += numWritten;
            sourceBuffer += numWritten;
        }

        return bytesWritten;
    }

    bool createFifos() const
    {
        return (mkfifo (pipeInName .toUTF8(), 0666) == 0 || errno == EEXIST)
            && (mkfifo (pipeOutName.toUTF8(), 0666) == 0 || errno == EEXIST);
    }

    const String pipeInName, pipeOutName;
    int pipeIn, pipeOut;

    const bool createdPipe;
    bool stopReadOperation;

private:
    static void signalHandler (int) {}

    static uint32 getTimeoutEnd (const int timeOutMilliseconds)
    {
        return timeOutMilliseconds >= 0 ? Time::getMillisecondCounter() + (uint32) timeOutMilliseconds : 0;
    }

    static bool hasExpired (const uint32 timeoutEnd)
    {
        return timeoutEnd != 0 && Time::getMillisecondCounter() >= timeoutEnd;
    }

    int openPipe (const String& name, int flags, const uint32 timeoutEnd)
    {
        for (;;)
        {
            const int p = ::open (name.toUTF8(), flags);

            if (p != -1 || hasExpired (timeoutEnd) || stopReadOperation)
                return p;

            Thread::sleep (2);
        }
    }

    static void waitForInput (const int handle, const int timeoutMsecs) noexcept
    {
        struct timeval timeout;
        timeout.tv_sec = timeoutMsecs / 1000;
        timeout.tv_usec = (timeoutMsecs % 1000) * 1000;

        fd_set rset;
        FD_ZERO (&rset);
        FD_SET (handle, &rset);

        select (handle + 1, &rset, nullptr, 0, &timeout);
    }
};

void NamedPipe::close()
{
    if (pimpl != nullptr)
    {
        pimpl->stopReadOperation = true;

        char buffer[1] = { 0 };
        ssize_t done = ::write (pimpl->pipeIn, buffer, 1);
        (void) done;

        ScopedWriteLock sl (lock);
        pimpl = nullptr;
    }
}

bool NamedPipe::openInternal (const String& pipeName, const bool createPipe)
{
   #if BEAST_IOS
    pimpl = new Pimpl (File::getSpecialLocation (File::tempDirectory)
                         .getChildFile (File::createLegalFileName (pipeName)).getFullPathName(), createPipe);
   #else
    pimpl = new Pimpl ("/tmp/" + File::createLegalFileName (pipeName), createPipe);
   #endif

    if (createPipe && ! pimpl->createFifos())
    {
        pimpl = nullptr;
        return false;
    }

    return true;
}

int NamedPipe::read (void* destBuffer, int maxBytesToRead, int timeOutMilliseconds)
{
    ScopedReadLock sl (lock);
    return pimpl != nullptr ? pimpl->read (static_cast <char*> (destBuffer), maxBytesToRead, timeOutMilliseconds) : -1;
}

int NamedPipe::write (const void* sourceBuffer, int numBytesToWrite, int timeOutMilliseconds)
{
    ScopedReadLock sl (lock);
    return pimpl != nullptr ? pimpl->write (static_cast <const char*> (sourceBuffer), numBytesToWrite, timeOutMilliseconds) : -1;
}
