
#ifdef WIN32

#include <windows.h>
#include <wincrypt.h>

#include <openssl/rand.h>

bool AddSystemEntropy()
{ // Get entropy from the Windows crypto provider
	char name[512], rand[128];
	DWORD count = 500;
	HCRYPTOPROV cryptoHandle;

	if (!CryptGetDefaultProvider(PROV_RSA_FULL, NULL, CRYPT_MACHINE_DEFAULT, name, &count))
		return false;
	if (!CryptAcquireContext(&cryptoHandle, NULL, name, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
		return false;

	if(!CryptGenRandom(cryptoHandle, 128, reinterpret_cast<BYTE*> rand))
	{
		CryptReleaseContext(cryptoHandle, 0);
		return false;
	}

	CryptReleaseContext(cryptoHandle, 0);
	RAND_seed(rand, 128);
	return true;
}

#else

bool AddSystemEntropy()
{ // Stub for implementing on other platforms
	return false;
}

#endif
