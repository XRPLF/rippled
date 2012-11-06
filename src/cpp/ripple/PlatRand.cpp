
#ifdef WIN32

#include <windows.h>
#include <wincrypt.h>

#include <openssl/rand.h>

bool AddSystemEntropy()
{ // Get entropy from the Windows crypto provider
	RAND_screen();  // this isn't really that safe since it only works for end users not servers

/* TODO: you need the cryptoAPI installed I think for the below to work. I suppose we should require people to install this to build the windows version
	char name[512], rand[128];
	DWORD count = 500;
	HCRYPTOPROV cryptoHandle;

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

*/
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
