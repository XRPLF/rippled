//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/crypto/RandomNumbers.h>
#include <beast/Config.h>
#include <cassert>
#include <cstdint>
#include <openssl/rand.h>
#if BEAST_WIN32
#include <windows.h>
#include <wincrypt.h>
#endif
#if BEAST_LINUX || BEAST_BSD || BEAST_MAC || BEAST_IOS
#include <sys/time.h>
#else
#include <time.h>
#endif
#include <fstream>

namespace ripple {

bool RandomNumbers::initialize (beast::Journal::Stream stream)
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
    if (! m_initialized && !initialize ())
        throw std::runtime_error ("Unable to add system entropy");

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
bool RandomNumbers::platformAddEntropy (beast::Journal::Stream stream)
{
    char name[512], rand[128];
    DWORD count = 500;
    HCRYPTPROV cryptoHandle;

    if (!CryptGetDefaultProviderA (PROV_RSA_FULL, nullptr, CRYPT_MACHINE_DEFAULT, name, &count))
    {
        stream << "Unable to get default crypto provider";
        return false;
    }

    if (!CryptAcquireContextA (&cryptoHandle, nullptr, name, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
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

bool RandomNumbers::platformAddEntropy (beast::Journal::Stream stream)
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

}
