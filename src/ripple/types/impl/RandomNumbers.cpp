//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// VFALCO TODO Replace OpenSSL randomness with a dependency-free implementation
//         Perhaps Schneier's Fortuna or a variant. Abstract the collection of
//         entropy and provide OS-specific implementation. We can re-use the
//         BearShare source code for this.
//
//         Add Random number generation to beast
//
#include <openssl/rand.h>
#if BEAST_WIN32
# include <windows.h>
# include <wincrypt.h>
#endif
#if BEAST_LINUX
# include <sys/time.h>
#else
# include <time.h>
#endif
#include <fstream>

namespace ripple {

RandomNumbers::RandomNumbers ()
    : m_initialized (false)
{
}

RandomNumbers::~RandomNumbers ()
{
}

bool RandomNumbers::initialize (Journal::Stream stream)
{
    assert (!m_initialized);

    bool success = platformAddEntropy (stream);

    if (success)
        m_initialized = true;

    return success;
}

void RandomNumbers::fillBytes (void* destinationBuffer, int numberOfBytes)
{
    // VFALCO NOTE this assert is here to remind us that the code is not yet
    //         thread safe.
    assert (m_initialized);

    // VFALCO NOTE When a spinlock is available in beast, use it here.
    if (! m_initialized)
    {
        if (! initialize ())
        {
            char const* message = "Unable to add system entropy";
            throw std::runtime_error (message);
        }
    }

#ifdef PURIFY
    memset (destinationBuffer, 0, numberOfBytes);
#endif

    if (RAND_bytes (reinterpret_cast <unsigned char*> (destinationBuffer), numberOfBytes) != 1)
    {
        assert (false);

        throw std::runtime_error ("Entropy pool not seeded");
    }
}

RandomNumbers& RandomNumbers::getInstance ()
{
    static RandomNumbers instance;

    return instance;
}

//------------------------------------------------------------------------------

#if BEAST_WIN32

// Get entropy from the Windows crypto provider
bool RandomNumbers::platformAddEntropy (Journal::Stream stream)
{
    char name[512], rand[128];
    DWORD count = 500;
    HCRYPTPROV cryptoHandle;

    if (!CryptGetDefaultProviderA (PROV_RSA_FULL, NULL, CRYPT_MACHINE_DEFAULT, name, &count))
    {
        stream << "Unable to get default crypto provider";
        return false;
    }

    if (!CryptAcquireContextA (&cryptoHandle, NULL, name, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
    {
        stream << "Unable to acquire crypto provider";
        return false;
    }

    if (!CryptGenRandom (cryptoHandle, 128, reinterpret_cast<BYTE*> (rand)))
    {
        stream << "Unable to get entropy from crypto provider";
        CryptReleaseContext (cryptoHandle, 0);
        return false;
    }

    CryptReleaseContext (cryptoHandle, 0);
    RAND_seed (rand, 128);

    return true;
}

#else

bool RandomNumbers::platformAddEntropy (Journal::Stream stream)
{
    char rand[128];
    std::ifstream reader;

    reader.open ("/dev/urandom", std::ios::in | std::ios::binary);

    if (!reader.is_open ())
    {
#ifdef BEAST_DEBUG
        stream << "Unable to open random source";
#endif
        return false;
    }

    reader.read (rand, 128);

    int bytesRead = reader.gcount ();

    if (bytesRead == 0)
    {
#ifdef BEAST_DEBUG
        stream << "Unable to read from random source";
#endif
        return false;
    }

    RAND_seed (rand, bytesRead);
    return bytesRead >= 64;
}

#endif

//------------------------------------------------------------------------------

//
// "Never go to sea with two chronometers; take one or three."
// Our three time sources are:
//  - System clock
//  - Median of other nodes's clocks
//  - The user (asking the user to fix the system clock if the first two disagree)
//

void RandomNumbers::platformAddPerformanceMonitorEntropy ()
{
    // VFALCO TODO Remove all this fancy stuff
    struct
    {
        int64 operator () () const
        {
            return time (NULL);
        }
    } GetTime;

    struct
    {
        void operator () ()
        {
            struct
            {
                // VFALCO TODO clean this up
                int64 operator () () const
                {
                    int64 nCounter = 0;
#if BEAST_WIN32
                    QueryPerformanceCounter ((LARGE_INTEGER*)&nCounter);
#else
                    timeval t;
                    gettimeofday (&t, NULL);
                    nCounter = t.tv_sec * 1000000 + t.tv_usec;
#endif
                    return nCounter;
                }
            } GetPerformanceCounter;

            // Seed with CPU performance counter
            int64 nCounter = GetPerformanceCounter ();
            RAND_add (&nCounter, sizeof (nCounter), 1.5);
            memset (&nCounter, 0, sizeof (nCounter));
        }
    } RandAddSeed;

    RandAddSeed ();

    // This can take up to 2 seconds, so only do it every 10 minutes
    static int64 nLastPerfmon;

    if (GetTime () < nLastPerfmon + 10 * 60)
        return;

    nLastPerfmon = GetTime ();

#if BEAST_WIN32
    // Don't need this on Linux, OpenSSL automatically uses /dev/urandom
    // Seed with the entire set of perfmon data
    unsigned char pdata[250000];
    memset (pdata, 0, sizeof (pdata));
    unsigned long nSize = sizeof (pdata);
    long ret = RegQueryValueExA (HKEY_PERFORMANCE_DATA, "Global", NULL, NULL, pdata, &nSize);
    RegCloseKey (HKEY_PERFORMANCE_DATA);

    if (ret == ERROR_SUCCESS)
    {
        RAND_add (pdata, nSize, nSize / 100.0);
        memset (pdata, 0, nSize);
        //printf("%s RandAddSeed() %d bytes\n", DateTimeStrFormat("%x %H:%M", GetTime()).c_str(), nSize);
    }

#endif
}

}
