#include "BitcoinUtil.h"
#include <cstdarg>
#include <openssl/rand.h>
#include <ctime>

#if defined(WIN32) || defined(WIN64)
#include <windows.h>
#else
#include <sys/time.h>
#endif

using namespace std;

std::string gFormatStr("v1");

std::string FormatFullVersion()
{
	return(gFormatStr);
}


string strprintf(const char* format, ...)
{
	char buffer[50000];
	char* p = buffer;
	int limit = sizeof(buffer);
	int ret;
	loop
	{
		va_list arg_ptr;
		va_start(arg_ptr, format);
		ret = _vsnprintf(p, limit, format, arg_ptr);
		va_end(arg_ptr);
		if (ret >= 0 && ret < limit)
			break;
		if (p != buffer)
			delete[] p;
		limit *= 2;
		p = new char[limit];
		if (p == NULL)
			throw std::bad_alloc();
	}
	string str(p, p+ret);
	if (p != buffer)
		delete[] p;
	return str;
}


inline int64 GetPerformanceCounter()
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

void RandAddSeed()
{
	// Seed with CPU performance counter
	int64 nCounter = GetPerformanceCounter();
	RAND_add(&nCounter, sizeof(nCounter), 1.5);
	memset(&nCounter, 0, sizeof(nCounter));
}
//
// "Never go to sea with two chronometers; take one or three."
// Our three time sources are:
//  - System clock
//  - Median of other nodes's clocks
//  - The user (asking the user to fix the system clock if the first two disagree)
//
int64 GetTime()
{
	return time(NULL);
}

void RandAddSeedPerfmon()
{
	RandAddSeed();

	// This can take up to 2 seconds, so only do it every 10 minutes
	static int64 nLastPerfmon;
	if (GetTime() < nLastPerfmon + 10 * 60)
		return;
	nLastPerfmon = GetTime();

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
