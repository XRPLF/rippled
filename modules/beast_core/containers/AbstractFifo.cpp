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

AbstractFifo::AbstractFifo (const int capacity) noexcept
    : bufferSize (capacity)
{
    bassert (bufferSize > 0);
}

AbstractFifo::~AbstractFifo() {}

int AbstractFifo::getTotalSize() const noexcept           { return bufferSize; }
int AbstractFifo::getFreeSpace() const noexcept           { return bufferSize - getNumReady(); }

int AbstractFifo::getNumReady() const noexcept
{
    const int vs = validStart.get();
    const int ve = validEnd.get();
    return ve >= vs ? (ve - vs) : (bufferSize - (vs - ve));
}

void AbstractFifo::reset() noexcept
{
    validEnd = 0;
    validStart = 0;
}

void AbstractFifo::setTotalSize (int newSize) noexcept
{
    bassert (newSize > 0);
    reset();
    bufferSize = newSize;
}

//==============================================================================
void AbstractFifo::prepareToWrite (int numToWrite, int& startIndex1, int& blockSize1, int& startIndex2, int& blockSize2) const noexcept
{
    const int vs = validStart.get();
    const int ve = validEnd.value;

    const int freeSpace = ve >= vs ? (bufferSize - (ve - vs)) : (vs - ve);
    numToWrite = bmin (numToWrite, freeSpace - 1);

    if (numToWrite <= 0)
    {
        startIndex1 = 0;
        startIndex2 = 0;
        blockSize1 = 0;
        blockSize2 = 0;
    }
    else
    {
        startIndex1 = ve;
        startIndex2 = 0;
        blockSize1 = bmin (bufferSize - ve, numToWrite);
        numToWrite -= blockSize1;
        blockSize2 = numToWrite <= 0 ? 0 : bmin (numToWrite, vs);
    }
}

void AbstractFifo::finishedWrite (int numWritten) noexcept
{
    bassert (numWritten >= 0 && numWritten < bufferSize);
    int newEnd = validEnd.value + numWritten;
    if (newEnd >= bufferSize)
        newEnd -= bufferSize;

    validEnd = newEnd;
}

void AbstractFifo::prepareToRead (int numWanted, int& startIndex1, int& blockSize1, int& startIndex2, int& blockSize2) const noexcept
{
    const int vs = validStart.value;
    const int ve = validEnd.get();

    const int numReady = ve >= vs ? (ve - vs) : (bufferSize - (vs - ve));
    numWanted = bmin (numWanted, numReady);

    if (numWanted <= 0)
    {
        startIndex1 = 0;
        startIndex2 = 0;
        blockSize1 = 0;
        blockSize2 = 0;
    }
    else
    {
        startIndex1 = vs;
        startIndex2 = 0;
        blockSize1 = bmin (bufferSize - vs, numWanted);
        numWanted -= blockSize1;
        blockSize2 = numWanted <= 0 ? 0 : bmin (numWanted, ve);
    }
}

void AbstractFifo::finishedRead (int numRead) noexcept
{
    bassert (numRead >= 0 && numRead <= bufferSize);

    int newStart = validStart.value + numRead;
    if (newStart >= bufferSize)
        newStart -= bufferSize;

    validStart = newStart;
}

//==============================================================================

class AbstractFifoTests  : public UnitTest
{
public:
    AbstractFifoTests() : UnitTest ("Abstract Fifo", "beast")
    {
    }

    class WriteThread  : public Thread
    {
    public:
        WriteThread (AbstractFifo& fifo_, int* buffer_)
            : Thread ("fifo writer"), fifo (fifo_), buffer (buffer_)
        {
            startThread();
        }

        ~WriteThread()
        {
            stopThread (5000);
        }

        void run()
        {
            int n = 0;
            Random r;

            while (! threadShouldExit())
            {
                int num = r.nextInt (2000) + 1;

                int start1, size1, start2, size2;
                fifo.prepareToWrite (num, start1, size1, start2, size2);

                bassert (size1 >= 0 && size2 >= 0);
                bassert (size1 == 0 || (start1 >= 0 && start1 < fifo.getTotalSize()));
                bassert (size2 == 0 || (start2 >= 0 && start2 < fifo.getTotalSize()));

                for (int i = 0; i < size1; ++i)
                    buffer [start1 + i] = n++;

                for (int i = 0; i < size2; ++i)
                    buffer [start2 + i] = n++;

                fifo.finishedWrite (size1 + size2);
            }
        }

    private:
        AbstractFifo& fifo;
        int* buffer;
    };

    void runTest()
    {
        beginTestCase ("AbstractFifo");

        int buffer [5000];
        AbstractFifo fifo (numElementsInArray (buffer));

        WriteThread writer (fifo, buffer);

        int n = 0;
        Random r;

        bool failed = false;

        for (int count = 100000; --count >= 0;)
        {
            int num = r.nextInt (6000) + 1;

            int start1, size1, start2, size2;
            fifo.prepareToRead (num, start1, size1, start2, size2);

            if (! (size1 >= 0 && size2 >= 0)
                    && (size1 == 0 || (start1 >= 0 && start1 < fifo.getTotalSize()))
                    && (size2 == 0 || (start2 >= 0 && start2 < fifo.getTotalSize())))
            {
                expect (false, "prepareToRead returned negative values");
                break;
            }

            for (int i = 0; i < size1; ++i)
                failed = (buffer [start1 + i] != n++) || failed;

            for (int i = 0; i < size2; ++i)
                failed = (buffer [start2 + i] != n++) || failed;

            if (failed)
            {
                break;
            }

            fifo.finishedRead (size1 + size2);
        }

        expect (! failed, "read values were incorrect");
    }
};

static AbstractFifoTests abstractFifoTests;
