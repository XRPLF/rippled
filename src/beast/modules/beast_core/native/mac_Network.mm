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

#include "../network/URL.h"

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

//==============================================================================
bool Process::openEmailWithAttachments (const String& targetEmailAddress,
                                        const String& emailSubject,
                                        const String& bodyText,
                                        const StringArray& filesToAttach)
{
  #if BEAST_IOS
    //xxx probably need to use MFMailComposeViewController
    bassertfalse;
    return false;
  #else
    BEAST_AUTORELEASEPOOL
    {
        String script;
        script << "tell application \"Mail\"\r\n"
                  "set newMessage to make new outgoing message with properties {subject:\""
               << emailSubject.replace ("\"", "\\\"")
               << "\", content:\""
               << bodyText.replace ("\"", "\\\"")
               << "\" & return & return}\r\n"
                  "tell newMessage\r\n"
                  "set visible to true\r\n"
                  "set sender to \"sdfsdfsdfewf\"\r\n"
                  "make new to recipient at end of to recipients with properties {address:\""
               << targetEmailAddress
               << "\"}\r\n";

        for (int i = 0; i < filesToAttach.size(); ++i)
        {
            script << "tell content\r\n"
                      "make new attachment with properties {file name:\""
                   << filesToAttach[i].replace ("\"", "\\\"")
                   << "\"} at after the last paragraph\r\n"
                      "end tell\r\n";
        }

        script << "end tell\r\n"
                  "end tell\r\n";

        NSAppleScript* s = [[NSAppleScript alloc] initWithSource: beastStringToNS (script)];
        NSDictionary* error = nil;
        const bool ok = [s executeAndReturnError: &error] != nil;
        [s release];

        return ok;
    }
  #endif
}

//==============================================================================
class URLConnectionState
   : public Thread
   , LeakChecked <URLConnectionState>
   , public Uncopyable
{
public:
    URLConnectionState (NSURLRequest* req)
        : Thread ("http connection"),
          contentLength (-1),
          delegate (nil),
          request ([req retain]),
          connection (nil),
          data ([[NSMutableData data] retain]),
          headers (nil),
          initialised (false),
          hasFailed (false),
          hasFinished (false)
    {
        static DelegateClass cls;
        delegate = [cls.createInstance() init];
        DelegateClass::setState (delegate, this);
    }

    ~URLConnectionState()
    {
        stop();
        [connection release];
        [data release];
        [request release];
        [headers release];
        [delegate release];
    }

    bool start (URL::OpenStreamProgressCallback* callback, void* context)
    {
        startThread();

        while (isThreadRunning() && ! initialised)
        {
            if (callback != nullptr)
                callback (context, -1, (int) [[request HTTPBody] length]);

            Thread::sleep (1);
        }

        return connection != nil && ! hasFailed;
    }

    void stop()
    {
        [connection cancel];
        stopThread (10000);
    }

    int read (char* dest, int numBytes)
    {
        int numDone = 0;

        while (numBytes > 0)
        {
            const int available = bmin (numBytes, (int) [data length]);

            if (available > 0)
            {
                const ScopedLock sl (dataLock);
                [data getBytes: dest length: (NSUInteger) available];
                [data replaceBytesInRange: NSMakeRange (0, (NSUInteger) available) withBytes: nil length: 0];

                numDone += available;
                numBytes -= available;
                dest += available;
            }
            else
            {
                if (hasFailed || hasFinished)
                    break;

                Thread::sleep (1);
            }
        }

        return numDone;
    }

    void didReceiveResponse (NSURLResponse* response)
    {
        {
            const ScopedLock sl (dataLock);
            [data setLength: 0];
        }

        initialised = true;
        contentLength = [response expectedContentLength];

        [headers release];
        headers = nil;

        if ([response isKindOfClass: [NSHTTPURLResponse class]])
            headers = [[((NSHTTPURLResponse*) response) allHeaderFields] retain];
    }

    void didFailWithError (NSError* error)
    {
        DBG (nsStringToBeast ([error description])); (void) error;
        hasFailed = true;
        initialised = true;
        signalThreadShouldExit();
    }

    void didReceiveData (NSData* newData)
    {
        const ScopedLock sl (dataLock);
        [data appendData: newData];
        initialised = true;
    }

    void didSendBodyData (int /*totalBytesWritten*/, int /*totalBytesExpected*/)
    {
    }

    void finishedLoading()
    {
        hasFinished = true;
        initialised = true;
        signalThreadShouldExit();
    }

    void run() override
    {
        connection = [[NSURLConnection alloc] initWithRequest: request
                                                     delegate: delegate];
        while (! threadShouldExit())
        {
            BEAST_AUTORELEASEPOOL
            {
                [[NSRunLoop currentRunLoop] runUntilDate: [NSDate dateWithTimeIntervalSinceNow: 0.01]];
            }
        }
    }

    int64 contentLength;
    CriticalSection dataLock;
    NSObject* delegate;
    NSURLRequest* request;
    NSURLConnection* connection;
    NSMutableData* data;
    NSDictionary* headers;
    bool initialised, hasFailed, hasFinished;

private:
    //==============================================================================
    struct DelegateClass  : public ObjCClass <NSObject>
    {
        DelegateClass()  : ObjCClass <NSObject> ("BEASTAppDelegate_")
        {
            addIvar <URLConnectionState*> ("state");

            addMethod (@selector (connection:didReceiveResponse:), didReceiveResponse,         "v@:@@");
            addMethod (@selector (connection:didFailWithError:),   didFailWithError,           "v@:@@");
            addMethod (@selector (connection:didReceiveData:),     didReceiveData,             "v@:@@");
            addMethod (@selector (connection:didSendBodyData:totalBytesWritten:totalBytesExpectedToWrite:totalBytesExpectedToWrite:),
                                                                   connectionDidSendBodyData,  "v@:@iii");
            addMethod (@selector (connectionDidFinishLoading:),    connectionDidFinishLoading, "v@:@");

            registerClass();
        }

        static void setState (id self, URLConnectionState* state)  { object_setInstanceVariable (self, "state", state); }
        static URLConnectionState* getState (id self)              { return getIvar<URLConnectionState*> (self, "state"); }

    private:
        static void didReceiveResponse (id self, SEL, NSURLConnection*, NSURLResponse* response)
        {
            getState (self)->didReceiveResponse (response);
        }

        static void didFailWithError (id self, SEL, NSURLConnection*, NSError* error)
        {
            getState (self)->didFailWithError (error);
        }

        static void didReceiveData (id self, SEL, NSURLConnection*, NSData* newData)
        {
            getState (self)->didReceiveData (newData);
        }

        static void connectionDidSendBodyData (id self, SEL, NSURLConnection*, NSInteger, NSInteger totalBytesWritten, NSInteger totalBytesExpected)
        {
            getState (self)->didSendBodyData (totalBytesWritten, totalBytesExpected);
        }

        static void connectionDidFinishLoading (id self, SEL, NSURLConnection*)
        {
            getState (self)->finishedLoading();
        }
    };
};
