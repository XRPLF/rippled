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

struct GetAdaptersInfoHelper
{
    bool callGetAdaptersInfo()
    {
        DynamicLibrary dll ("iphlpapi.dll");
        BEAST_LOAD_WINAPI_FUNCTION (dll, GetAdaptersInfo, getAdaptersInfo, DWORD, (PIP_ADAPTER_INFO, PULONG))

        if (getAdaptersInfo == nullptr)
            return false;

        adapterInfo.malloc (1);
        ULONG len = sizeof (IP_ADAPTER_INFO);

        if (getAdaptersInfo (adapterInfo, &len) == ERROR_BUFFER_OVERFLOW)
            adapterInfo.malloc (len, 1);

        return getAdaptersInfo (adapterInfo, &len) == NO_ERROR;
    }

    HeapBlock<IP_ADAPTER_INFO> adapterInfo;
};

namespace MACAddressHelpers
{
    void getViaGetAdaptersInfo (Array<MACAddress>& result)
    {
        GetAdaptersInfoHelper gah;

        if (gah.callGetAdaptersInfo())
        {
            for (PIP_ADAPTER_INFO adapter = gah.adapterInfo; adapter != nullptr; adapter = adapter->Next)
                if (adapter->AddressLength >= 6)
                    result.addIfNotAlreadyThere (MACAddress (adapter->Address));
        }
    }

    void getViaNetBios (Array<MACAddress>& result)
    {
        DynamicLibrary dll ("netapi32.dll");
        BEAST_LOAD_WINAPI_FUNCTION (dll, Netbios, NetbiosCall, UCHAR, (PNCB))

        if (NetbiosCall != 0)
        {
            LANA_ENUM enums = { 0 };

            {
                NCB ncb = { 0 };
                ncb.ncb_command = NCBENUM;
                ncb.ncb_buffer = (unsigned char*) &enums;
                ncb.ncb_length = sizeof (LANA_ENUM);
                NetbiosCall (&ncb);
            }

            for (int i = 0; i < enums.length; ++i)
            {
                NCB ncb2 = { 0 };
                ncb2.ncb_command = NCBRESET;
                ncb2.ncb_lana_num = enums.lana[i];

                if (NetbiosCall (&ncb2) == 0)
                {
                    NCB ncb = { 0 };
                    memcpy (ncb.ncb_callname, "*                   ", NCBNAMSZ);
                    ncb.ncb_command = NCBASTAT;
                    ncb.ncb_lana_num = enums.lana[i];

                    struct ASTAT
                    {
                        ADAPTER_STATUS adapt;
                        NAME_BUFFER    NameBuff [30];
                    };

                    ASTAT astat;
                    zerostruct (astat);
                    ncb.ncb_buffer = (unsigned char*) &astat;
                    ncb.ncb_length = sizeof (ASTAT);

                    if (NetbiosCall (&ncb) == 0 && astat.adapt.adapter_type == 0xfe)
                        result.addIfNotAlreadyThere (MACAddress (astat.adapt.adapter_address));
                }
            }
        }
    }
}

void MACAddress::findAllAddresses (Array<MACAddress>& result)
{
    MACAddressHelpers::getViaGetAdaptersInfo (result);
    MACAddressHelpers::getViaNetBios (result);
}

void IPAddress::findAllAddresses (Array<IPAddress>& result)
{
    result.addIfNotAlreadyThere (IPAddress::local());

    GetAdaptersInfoHelper gah;

    if (gah.callGetAdaptersInfo())
    {
        for (PIP_ADAPTER_INFO adapter = gah.adapterInfo; adapter != nullptr; adapter = adapter->Next)
        {
            IPAddress ip (adapter->IpAddressList.IpAddress.String);

            if (ip != IPAddress::any())
                result.addIfNotAlreadyThere (ip);
        }
    }
}

//==============================================================================
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
