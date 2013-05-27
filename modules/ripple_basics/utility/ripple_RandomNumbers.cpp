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
