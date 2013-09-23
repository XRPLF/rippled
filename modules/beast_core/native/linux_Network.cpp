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
    const int s = socket (AF_INET, SOCK_DGRAM, 0);
    if (s != -1)
    {
        char buf [1024];
        struct ifconf ifc;
        ifc.ifc_len = sizeof (buf);
        ifc.ifc_buf = buf;
        ioctl (s, SIOCGIFCONF, &ifc);

        for (unsigned int i = 0; i < ifc.ifc_len / sizeof (struct ifreq); ++i)
        {
            struct ifreq ifr;
            strcpy (ifr.ifr_name, ifc.ifc_req[i].ifr_name);

            if (ioctl (s, SIOCGIFFLAGS, &ifr) == 0
                 && (ifr.ifr_flags & IFF_LOOPBACK) == 0
                 && ioctl (s, SIOCGIFHWADDR, &ifr) == 0)
            {
                result.addIfNotAlreadyThere (MACAddress ((const uint8*) ifr.ifr_hwaddr.sa_data));
            }
        }

        close (s);
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
