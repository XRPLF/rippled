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

#ifndef INTERNET_FLAG_NEED_FILE
 #define INTERNET_FLAG_NEED_FILE 0x00000010
#endif

#ifndef INTERNET_OPTION_DISABLE_AUTODIAL
 #define INTERNET_OPTION_DISABLE_AUTODIAL 70
#endif

//==============================================================================
class WebInputStream
    : public InputStream
    , LeakChecked <WebInputStream>
{
public:
    WebInputStream (const String& address_, bool isPost_, const MemoryBlock& postData_,
                    URL::OpenStreamProgressCallback* progressCallback, void* progressCallbackContext,
                    const String& headers_, int timeOutMs_, StringPairArray* responseHeaders)
      : connection (0), request (0),
        address (address_), headers (headers_), postData (postData_), position (0),
        finished (false), isPost (isPost_), timeOutMs (timeOutMs_)
    {
        createConnection (progressCallback, progressCallbackContext);

        if (responseHeaders != nullptr && ! isError())
        {
            DWORD bufferSizeBytes = 4096;

            for (;;)
            {
                HeapBlock<char> buffer ((size_t) bufferSizeBytes);

                if (HttpQueryInfo (request, HTTP_QUERY_RAW_HEADERS_CRLF, buffer.getData(), &bufferSizeBytes, 0))
                {
                    StringArray headersArray;
                    headersArray.addLines (reinterpret_cast <const WCHAR*> (buffer.getData()));

                    for (int i = 0; i < headersArray.size(); ++i)
                    {
                        const String& header = headersArray[i];
                        const String key (header.upToFirstOccurrenceOf (": ", false, false));
                        const String value (header.fromFirstOccurrenceOf (": ", false, false));
                        const String previousValue ((*responseHeaders) [key]);

                        responseHeaders->set (key, previousValue.isEmpty() ? value : (previousValue + "," + value));
                    }

                    break;
                }

                if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                    break;
            }
        }
    }

    ~WebInputStream()
    {
        close();
    }

    //==============================================================================
    bool isError() const        { return request == 0; }
    bool isExhausted()          { return finished; }
    int64 getPosition()         { return position; }

    int64 getTotalLength()
    {
        if (! isError())
        {
            DWORD index = 0, result = 0, size = sizeof (result);

            if (HttpQueryInfo (request, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &result, &size, &index))
                return (int64) result;
        }

        return -1;
    }

    int read (void* buffer, int bytesToRead)
    {
        bassert (buffer != nullptr && bytesToRead >= 0);
        DWORD bytesRead = 0;

        if (! (finished || isError()))
        {
            InternetReadFile (request, buffer, (DWORD) bytesToRead, &bytesRead);
            position += bytesRead;

            if (bytesRead == 0)
                finished = true;
        }

        return (int) bytesRead;
    }

    bool setPosition (int64 wantedPos)
    {
        if (isError())
            return false;

        if (wantedPos != position)
        {
            finished = false;
            position = (int64) InternetSetFilePointer (request, (LONG) wantedPos, 0, FILE_BEGIN, 0);

            if (position == wantedPos)
                return true;

            if (wantedPos < position)
            {
                close();
                position = 0;
                createConnection (0, 0);
            }

            skipNextBytes (wantedPos - position);
        }

        return true;
    }

private:
    //==============================================================================
    HINTERNET connection, request;
    String address, headers;
    MemoryBlock postData;
    int64 position;
    bool finished;
    const bool isPost;
    int timeOutMs;

    void close()
    {
        if (request != 0)
        {
            InternetCloseHandle (request);
            request = 0;
        }

        if (connection != 0)
        {
            InternetCloseHandle (connection);
            connection = 0;
        }
    }

    void createConnection (URL::OpenStreamProgressCallback* progressCallback,
                           void* progressCallbackContext)
    {
        static HINTERNET sessionHandle = InternetOpen (_T("beast"), INTERNET_OPEN_TYPE_PRECONFIG, 0, 0, 0);

        close();

        if (sessionHandle != 0)
        {
            // break up the url..
            const int fileNumChars = 65536;
            const int serverNumChars = 2048;
            const int usernameNumChars = 1024;
            const int passwordNumChars = 1024;
            HeapBlock<TCHAR> file (fileNumChars), server (serverNumChars),
                             username (usernameNumChars), password (passwordNumChars);

            URL_COMPONENTS uc = { 0 };
            uc.dwStructSize = sizeof (uc);
            uc.lpszUrlPath = file;
            uc.dwUrlPathLength = fileNumChars;
            uc.lpszHostName = server;
            uc.dwHostNameLength = serverNumChars;
            uc.lpszUserName = username;
            uc.dwUserNameLength = usernameNumChars;
            uc.lpszPassword = password;
            uc.dwPasswordLength = passwordNumChars;

            if (InternetCrackUrl (address.toWideCharPointer(), 0, 0, &uc))
                openConnection (uc, sessionHandle, progressCallback, progressCallbackContext);
        }
    }

    void openConnection (URL_COMPONENTS& uc, HINTERNET sessionHandle,
                         URL::OpenStreamProgressCallback* progressCallback,
                         void* progressCallbackContext)
    {
        int disable = 1;
        InternetSetOption (sessionHandle, INTERNET_OPTION_DISABLE_AUTODIAL, &disable, sizeof (disable));

        if (timeOutMs == 0)
            timeOutMs = 30000;
        else if (timeOutMs < 0)
            timeOutMs = -1;

        applyTimeout (sessionHandle, INTERNET_OPTION_CONNECT_TIMEOUT);
        applyTimeout (sessionHandle, INTERNET_OPTION_RECEIVE_TIMEOUT);
        applyTimeout (sessionHandle, INTERNET_OPTION_SEND_TIMEOUT);
        applyTimeout (sessionHandle, INTERNET_OPTION_DATA_RECEIVE_TIMEOUT);
        applyTimeout (sessionHandle, INTERNET_OPTION_DATA_SEND_TIMEOUT);

        const bool isFtp = address.startsWithIgnoreCase ("ftp:");

        connection = InternetConnect (sessionHandle, uc.lpszHostName, uc.nPort,
                                      uc.lpszUserName, uc.lpszPassword,
                                      isFtp ? (DWORD) INTERNET_SERVICE_FTP
                                            : (DWORD) INTERNET_SERVICE_HTTP,
                                      0, 0);
        if (connection != 0)
        {
            if (isFtp)
                request = FtpOpenFile (connection, uc.lpszUrlPath, GENERIC_READ,
                                       FTP_TRANSFER_TYPE_BINARY | INTERNET_FLAG_NEED_FILE, 0);
            else
                openHTTPConnection (uc, progressCallback, progressCallbackContext);
        }
    }

    void applyTimeout (HINTERNET sessionHandle, const DWORD option)
    {
        InternetSetOption (sessionHandle, option, &timeOutMs, sizeof (timeOutMs));
    }

    void openHTTPConnection (URL_COMPONENTS& uc, URL::OpenStreamProgressCallback* progressCallback,
                             void* progressCallbackContext)
    {
        const TCHAR* mimeTypes[] = { _T("*/*"), nullptr };

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_COOKIES;

        if (address.startsWithIgnoreCase ("https:"))
            flags |= INTERNET_FLAG_SECURE;  // (this flag only seems necessary if the OS is running IE6 -
                                            //  IE7 seems to automatically work out when it's https)

        request = HttpOpenRequest (connection, isPost ? _T("POST") : _T("GET"),
                                   uc.lpszUrlPath, 0, 0, mimeTypes, flags, 0);

        if (request != 0)
        {
            INTERNET_BUFFERS buffers = { 0 };
            buffers.dwStructSize = sizeof (INTERNET_BUFFERS);
            buffers.lpcszHeader = headers.toWideCharPointer();
            buffers.dwHeadersLength = (DWORD) headers.length();
            buffers.dwBufferTotal = (DWORD) postData.getSize();

            if (HttpSendRequestEx (request, &buffers, 0, HSR_INITIATE, 0))
            {
                int bytesSent = 0;

                for (;;)
                {
                    const int bytesToDo = bmin (1024, (int) postData.getSize() - bytesSent);
                    DWORD bytesDone = 0;

                    if (bytesToDo > 0
                         && ! InternetWriteFile (request,
                                                 static_cast <const char*> (postData.getData()) + bytesSent,
                                                 (DWORD) bytesToDo, &bytesDone))
                    {
                        break;
                    }

                    if (bytesToDo == 0 || (int) bytesDone < bytesToDo)
                    {
                        if (HttpEndRequest (request, 0, 0, 0))
                            return;

                        break;
                    }

                    bytesSent += bytesDone;

                    if (progressCallback != nullptr
                          && ! progressCallback (progressCallbackContext, bytesSent, (int) postData.getSize()))
                        break;
                }
            }
        }

        close();
    }
};

InputStream* URL::createNativeStream (const String& address, bool isPost, const MemoryBlock& postData,
                                      OpenStreamProgressCallback* progressCallback, void* progressCallbackContext,
                                      const String& headers, const int timeOutMs, StringPairArray* responseHeaders)
{
    ScopedPointer <WebInputStream> wi (new WebInputStream (address, isPost, postData,
                                                           progressCallback, progressCallbackContext,
                                                           headers, timeOutMs, responseHeaders));

    return wi->isError() ? nullptr : wi.release();
}


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
