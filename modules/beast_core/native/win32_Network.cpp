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

namespace beast
{

bool Process::openEmailWithAttachments (const String& targetEmailAddress,
                                        const String& emailSubject,
                                        const String& bodyText,
                                        const StringArray& filesToAttach)
{
    DynamicLibrary dll ("MAPI32.dll");
    BEAST_LOAD_WINAPI_FUNCTION (dll, MAPISendMail, mapiSendMail,
                               ULONG, (LHANDLE, ULONG, lpMapiMessage, ::FLAGS, ULONG))

    if (mapiSendMail == nullptr)
        return false;

    MapiMessage message = { 0 };
    message.lpszSubject = (LPSTR) emailSubject.toRawUTF8();
    message.lpszNoteText = (LPSTR) bodyText.toRawUTF8();

    MapiRecipDesc recip = { 0 };
    recip.ulRecipClass = MAPI_TO;
    String targetEmailAddress_ (targetEmailAddress);
    if (targetEmailAddress_.isEmpty())
        targetEmailAddress_ = " "; // (Windows Mail can't deal with a blank address)
    recip.lpszName = (LPSTR) targetEmailAddress_.toRawUTF8();
    message.nRecipCount = 1;
    message.lpRecips = &recip;

    HeapBlock <MapiFileDesc> files;
    files.calloc ((size_t) filesToAttach.size());

    message.nFileCount = (ULONG) filesToAttach.size();
    message.lpFiles = files;

    for (int i = 0; i < filesToAttach.size(); ++i)
    {
        files[i].nPosition = (ULONG) -1;
        files[i].lpszPathName = (LPSTR) filesToAttach[i].toRawUTF8();
    }

    return mapiSendMail (0, 0, &message, MAPI_DIALOG | MAPI_LOGON_UI, 0) == SUCCESS_SUCCESS;
}

}  // namespace beast
