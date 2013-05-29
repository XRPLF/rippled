//------------------------------------------------------------------------------
/*
	Copyright (c) 2011-2013, OpenCoin, Inc.

	Permission to use, copy, modify, and/or distribute this software for any
	purpose with  or without fee is hereby granted,  provided that the above
	copyright notice and this permission notice appear in all copies.

	THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
	WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES OF
	MERCHANTABILITY  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
	ANY SPECIAL,  DIRECT, INDIRECT,  OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
	WHATSOEVER  RESULTING  FROM LOSS OF USE, DATA OR PROFITS,  WHETHER IN AN
	ACTION OF CONTRACT, NEGLIGENCE  OR OTHER TORTIOUS ACTION, ARISING OUT OF
	OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

void getRand(unsigned char *buf, int num)
{
#ifdef PURIFY
	memset(buf, 0, num);
#endif
	if (RAND_bytes(buf, num) != 1)
	{
		assert(false);
		throw std::runtime_error("Entropy pool not seeded");
	}
}

//------------------------------------------------------------------------------

// VFALCO: TODO replace WIN32 macro with VFLIB_WIN32

#ifdef WIN32

bool AddSystemEntropy()
{ // Get entropy from the Windows crypto provider
	char name[512], rand[128];
	DWORD count = 500;
	HCRYPTPROV cryptoHandle;

	if (!CryptGetDefaultProvider(PROV_RSA_FULL, NULL, CRYPT_MACHINE_DEFAULT, name, &count))
	{
#ifdef DEBUG
		std::cerr << "Unable to get default crypto provider" << std::endl;
#endif
		return false;
	}

	if (!CryptAcquireContext(&cryptoHandle, NULL, name, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
	{
#ifdef DEBUG
		std::cerr << "Unable to acquire crypto provider" << std::endl;
#endif
		return false;
	}

	if(!CryptGenRandom(cryptoHandle, 128, reinterpret_cast<BYTE*>(rand)))
	{
#ifdef DEBUG
		std::cerr << "Unable to get entropy from crypto provider" << std::endl;
#endif
		CryptReleaseContext(cryptoHandle, 0);
		return false;
	}

	CryptReleaseContext(cryptoHandle, 0);
	RAND_seed(rand, 128);

	return true;
}

#else

#include <iostream>
#include <fstream>

#include <openssl/rand.h>

bool AddSystemEntropy()
{
	char rand[128];
	std::ifstream reader;

	reader.open("/dev/urandom", std::ios::in | std::ios::binary);
	if (!reader.is_open())
	{
#ifdef DEBUG
		std::cerr << "Unable to open random source" << std::endl;
#endif
		return false;
	}
	reader.read(rand, 128);

	int bytesRead = reader.gcount();
	if (bytesRead == 0)
	{
#ifdef DEBUG
		std::cerr << "Unable to read from random source" << std::endl;
#endif
		return false;
	}
	RAND_seed(rand, bytesRead);
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

void RandAddSeedPerfmon()
{
	// VFALCO: This is how we simulate local functions
	struct
	{
		int64 operator() () const
		{
			return time (NULL);
		}
	} GetTime;

	struct
	{
		void operator() ()
		{
			struct
			{
				// VFALCO: TODO, clean this up
				int64 operator() () const
				{
					int64 nCounter = 0;
#if defined(WIN32) || defined(WIN64)
					QueryPerformanceCounter((LARGE_INTEGER*)&nCounter);
#else
					timeval t;
					gettimeofday(&t, NULL);
					nCounter = t.tv_sec * 1000000 + t.tv_usec;
#endif
					return nCounter;
				}
			} GetPerformanceCounter;

			// Seed with CPU performance counter
			int64 nCounter = GetPerformanceCounter();
			RAND_add(&nCounter, sizeof(nCounter), 1.5);
			memset(&nCounter, 0, sizeof(nCounter));
		}
	} RandAddSeed;

	RandAddSeed();

	// This can take up to 2 seconds, so only do it every 10 minutes
	static int64 nLastPerfmon;
	if (GetTime () < nLastPerfmon + 10 * 60)
		return;
	nLastPerfmon = GetTime ();

#ifdef WIN32
	// Don't need this on Linux, OpenSSL automatically uses /dev/urandom
	// Seed with the entire set of perfmon data
	unsigned char pdata[250000];
	memset(pdata, 0, sizeof(pdata));
	unsigned long nSize = sizeof(pdata);
	long ret = RegQueryValueExA(HKEY_PERFORMANCE_DATA, "Global", NULL, NULL, pdata, &nSize);
	RegCloseKey(HKEY_PERFORMANCE_DATA);
	if (ret == ERROR_SUCCESS)
	{
		RAND_add(pdata, nSize, nSize/100.0);
		memset(pdata, 0, nSize);
		//printf("%s RandAddSeed() %d bytes\n", DateTimeStrFormat("%x %H:%M", GetTime()).c_str(), nSize);
	}
#endif
}
