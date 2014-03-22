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

// sysinfo() from sysinfo-bsd
// https://code.google.com/p/sysinfo-bsd/
/*
 * Copyright (C) 2010 Kostas Petrikas, All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name(s) of the author(s) may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

namespace beast
{

#define SI_LOAD_SHIFT   16
struct sysinfo {
  long uptime;                  /* Seconds since boot */
  unsigned long loads[3];       /* 1, 5, and 15 minute load averages */
  unsigned long totalram;       /* Total usable main memory size */
  unsigned long freeram;        /* Available memory size */
  unsigned long sharedram;      /* Amount of shared memory */
  unsigned long bufferram;      /* Memory used by buffers */
  unsigned long totalswap;      /* Total swap space size */
  unsigned long freeswap;       /* swap space still available */
  unsigned short procs;         /* Number of current processes */
  unsigned short pad;           /* leaving this for linux compatability */
  unsigned long totalhigh;      /* Total high memory size */
  unsigned long freehigh;       /* Available high memory size */
  unsigned int mem_unit;        /* Memory unit size in bytes */
  char _f[20-2*sizeof(long)-sizeof(int)];       /* leaving this for linux compatability */
};

#define NLOADS 3
#define UNIT_S 1024 /*Kb*/
#define R_IGNORE -1

/*the macros*/
#define R_ERROR(_EC) {if(_EC > R_IGNORE)errno = _EC; return -1;}
#define GETSYSCTL(name, var) getsysctl((char*)name, &(var), sizeof(var))
#define PAGE_2_UNIT(_PAGE) (((double)_PAGE * page_s) / UNIT_S)

/*sysctl wrapper*/
static int getsysctl(char *name, void *ptr, size_t len){  
        size_t nlen = len;
        if (sysctlbyname(name, ptr, &nlen, NULL, 0) == -1)
                return -1;
       
        if (nlen != len)
                return -1;

        return 0;
}

int sysinfo(struct sysinfo *info){
        kvm_t *kvmh;
        double load_avg[NLOADS];
        int page_s = getpagesize();
       
        if (info == NULL)
                R_ERROR(EFAULT);

        memset(info, 0, sizeof(struct sysinfo));        
        info -> mem_unit = UNIT_S;
       
        /*kvm init*/
        if ((kvmh = kvm_open(NULL, "/dev/null", "/dev/null",
        O_RDONLY, "kvm_open")) == NULL)
                R_ERROR(0);
       
        /*load averages*/
        if (kvm_getloadavg(kvmh, load_avg, NLOADS) == -1)
                R_ERROR(0);
       
        info -> loads[0] = (u_long)((float)load_avg[0] * USHRT_MAX);
        info -> loads[1] = (u_long)((float)load_avg[1] * USHRT_MAX);
        info -> loads[2] = (u_long)((float)load_avg[2] * USHRT_MAX);
       
        /*swap space*/
        struct kvm_swap k_swap;

        if (kvm_getswapinfo(kvmh, &k_swap, 1, 0) == -1)
                R_ERROR(0);

        info -> totalswap =
        (u_long)PAGE_2_UNIT(k_swap.ksw_total);
        info -> freeswap = info -> totalswap -
        (u_long)PAGE_2_UNIT(k_swap.ksw_used);
       
        /*processes*/
        int n_procs;    
       
        if (kvm_getprocs(kvmh, KERN_PROC_ALL, 0, &n_procs) == NULL)
                R_ERROR(0);
               
        info -> procs = (u_short)n_procs;
       
        /*end of kvm session*/
        if (kvm_close(kvmh) == -1)
                R_ERROR(0);
       
        /*uptime*/
        struct timespec ts;
       
        if (clock_gettime(CLOCK_UPTIME, &ts) == -1)
                R_ERROR(R_IGNORE);
               
        info -> uptime = (long)ts.tv_sec;      
       
        /*ram*/
        int total_pages,
            free_pages,
            active_pages,
            inactive_pages;
        u_long shmmax;
       
        if (GETSYSCTL("vm.stats.vm.v_page_count", total_pages) == -1)
                R_ERROR(R_IGNORE);      
        if (GETSYSCTL("vm.stats.vm.v_free_count", free_pages) == -1)
                R_ERROR(R_IGNORE);              
        if (GETSYSCTL("vm.stats.vm.v_active_count", active_pages) == -1)
                R_ERROR(R_IGNORE);              
        if (GETSYSCTL("vm.stats.vm.v_inactive_count", inactive_pages) == -1)
                R_ERROR(R_IGNORE);
        if (GETSYSCTL("kern.ipc.shmmax", shmmax) == -1)
                R_ERROR(R_IGNORE);
       
        info -> totalram = (u_long)PAGE_2_UNIT(total_pages);
        info -> freeram = (u_long)PAGE_2_UNIT(free_pages);
        info -> bufferram = (u_long)PAGE_2_UNIT(active_pages);
        info -> sharedram = shmmax / UNIT_S;
       
        /*high mem (todo)*/
        info -> totalhigh = 0; /*Does this supose to refer to HMA or reserved ram?*/
        info -> freehigh = 0;
       
        return 0;
}

//==============================================================================

void Logger::outputDebugString (const String& text)
{
    std::cerr << text << std::endl;
}

//==============================================================================
SystemStats::OperatingSystemType SystemStats::getOperatingSystemType()
{
    return FreeBSD;
}

String SystemStats::getOperatingSystemName()
{
    return "FreeBSD";
}

bool SystemStats::isOperatingSystem64Bit()

{
   #if BEAST_64BIT
    return true;
   #else
    //xxx not sure how to find this out?..
    return false;
   #endif
}

//==============================================================================
namespace BSDStatsHelpers
{
    String getDmesgInfo (const char* const key)
    {
        StringArray lines;
        File ("/var/run/dmesg.boot").readLines (lines);

        for (int i = lines.size(); --i >= 0;) // (NB - it's important that this runs in reverse order)
            if (lines[i].startsWith (key))
                return lines[i].substring (strlen (key)).trim();

        return String::empty;
    }

}

String SystemStats::getCpuVendor()
{
    return BSDStatsHelpers::getDmesgInfo ("  Origin =").upToFirstOccurrenceOf (" ", false, false).unquoted();
}

int SystemStats::getCpuSpeedInMegaherz()
{
    return roundToInt (BSDStatsHelpers::getDmesgInfo ("CPU:").fromLastOccurrenceOf ("(", false, false).upToFirstOccurrenceOf ("-MHz", false, false).getFloatValue());
}

int SystemStats::getMemorySizeInMegabytes()
{
    struct sysinfo sysi;

    if (sysinfo (&sysi) == 0)
        return (sysi.totalram * sysi.mem_unit / (1024 * 1024));

    return 0;
}

int SystemStats::getPageSize()
{
    return sysconf (_SC_PAGESIZE);
}

//==============================================================================
String SystemStats::getLogonName()
{
    const char* user = getenv ("USER");

    if (user == nullptr)
    {
        struct passwd* const pw = getpwuid (getuid());
        if (pw != nullptr)
            user = pw->pw_name;
    }

    return CharPointer_UTF8 (user);
}

String SystemStats::getFullUserName()
{
    return getLogonName();
}

String SystemStats::getComputerName()
{
    char name [256] = { 0 };
    if (gethostname (name, sizeof (name) - 1) == 0)
        return name;

    return String::empty;
}

String getLocaleValue (nl_item key)
{
    const char* oldLocale = ::setlocale (LC_ALL, "");
    return String (const_cast <const char*> (nl_langinfo (key)));
    ::setlocale (LC_ALL, oldLocale);
}

String SystemStats::getUserLanguage()
{
    return "Uknown user language";
}

String SystemStats::getUserRegion()
{
    return "Unknown user region";
}

String SystemStats::getDisplayLanguage()
{
    return getUserLanguage();
}

//==============================================================================
void CPUInformation::initialise() noexcept
{
    const String features (BSDStatsHelpers::getDmesgInfo ("  Features="));
    hasMMX = features.contains ("MMX");
    hasSSE = features.contains ("SSE");
    hasSSE2 = features.contains ("SSE2");
    const String features2 (BSDStatsHelpers::getDmesgInfo ("  Features2="));
    hasSSE3 = features2.contains ("SSE3");
    const String amdfeatures2 (BSDStatsHelpers::getDmesgInfo ("  AMD Features2="));
    has3DNow = amdfeatures2.contains ("3DNow!");

    GETSYSCTL("hw.ncpu", numCpus);
    if (numCpus == -1) numCpus = 1;
}

//==============================================================================
std::uint32_t beast_millisecondsSinceStartup() noexcept
{
    timespec t;
    clock_gettime (CLOCK_MONOTONIC, &t);

    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

std::int64_t Time::getHighResolutionTicks() noexcept
{
    timespec t;
    clock_gettime (CLOCK_MONOTONIC, &t);

    return (t.tv_sec * (std::int64_t) 1000000) + (t.tv_nsec / 1000);
}

std::int64_t Time::getHighResolutionTicksPerSecond() noexcept
{
    return 1000000;  // (microseconds)
}

double Time::getMillisecondCounterHiRes() noexcept
{
    return getHighResolutionTicks() * 0.001;
}

bool Time::setSystemTimeToThisTime() const
{
    timeval t;
    t.tv_sec = millisSinceEpoch / 1000;
    t.tv_usec = (millisSinceEpoch - t.tv_sec * 1000) * 1000;

    return settimeofday (&t, 0) == 0;
}

} // beast
