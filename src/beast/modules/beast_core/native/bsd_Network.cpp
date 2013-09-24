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

void MACAddress::findAllAddresses (Array<MACAddress>& result)
{
    ifaddrs* addrs = nullptr;

    if (getifaddrs (&addrs) == 0)
    {
        for (const ifaddrs* cursor = addrs; cursor != nullptr; cursor = cursor->ifa_next)
        {
            sockaddr_storage* sto = (sockaddr_storage*) cursor->ifa_addr;
            if (sto->ss_family == AF_LINK)
            {
                const sockaddr_dl* const sadd = (const sockaddr_dl*) cursor->ifa_addr;

                #ifndef IFT_ETHER
                 #define IFT_ETHER 6
                #endif

                if (sadd->sdl_type == IFT_ETHER)
                    result.addIfNotAlreadyThere (MACAddress (((const uint8*) sadd->sdl_data) + sadd->sdl_nlen));
            }
        }

        freeifaddrs (addrs);
    }
}


bool Process::openEmailWithAttachments (const String& /* targetEmailAddress */,
                                        const String& /* emailSubject */,
                                        const String& /* bodyText */,
                                        const StringArray& /* filesToAttach */)
{
    bassertfalse;    // xxx todo

    return false;
}
