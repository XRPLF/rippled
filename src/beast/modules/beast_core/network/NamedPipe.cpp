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

NamedPipe::NamedPipe()
{
}

NamedPipe::~NamedPipe()
{
    close();
}

bool NamedPipe::openExisting (const String& pipeName)
{
    close();

    ScopedWriteLock sl (lock);
    currentPipeName = pipeName;
    return openInternal (pipeName, false);
}

bool NamedPipe::isOpen() const
{
    return pimpl != nullptr;
}

bool NamedPipe::createNewPipe (const String& pipeName)
{
    close();

    ScopedWriteLock sl (lock);
    currentPipeName = pipeName;
    return openInternal (pipeName, true);
}

String NamedPipe::getName() const
{
    return currentPipeName;
}

// other methods for this class are implemented in the platform-specific files
