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

#ifndef RIPPLE_JOBTYPE_H
#define RIPPLE_JOBTYPE_H

struct JobType
{
private:
    static std::atomic <int> s_nextPriority;

public:
    typedef std::map <int, std::reference_wrapper <JobType>> Map;

    static Map Jobs;

    int const priority;
    int const limit;
    std::string const name;

    // Indicates that jobs of this type should be skipped
    // when the job queue is stopping. Jobs that aren't
    // skipped will be called and the job must call
    // Job::shouldCancel to determine if a long-running or
    // non-mandatory operation should be cancelled.
    bool const skip;

    LoadMonitor load;

    // How many are waiting
    int waiting;

    // How many are running
    int running;

    // How many we didn't signal due to limits
    int deferred;

public:
    static int const maxLimit = std::numeric_limits <int>::max ();

    JobType (char const* name_, int limit_, bool skip_);

    bool addTask ();

    static int getMaxPriority ();
};

bool operator< (JobType const& rhs, JobType const& lhs);
bool operator<= (JobType const& rhs, JobType const& lhs);

bool operator> (JobType const& rhs, JobType const& lhs);
bool operator>= (JobType const& rhs, JobType const& lhs);

bool operator== (JobType const& rhs, JobType const& lhs);
bool operator!= (JobType const& rhs, JobType const& lhs);

//==============================================================================
extern JobType jtPACK;
extern JobType jtPUBOLDLEDGER;
extern JobType jtVALIDATION_ut;
extern JobType jtPROOFWORK;
extern JobType jtTRANSACTION_l;
extern JobType jtPROPOSAL_ut;
extern JobType jtLEDGER_DATA;
extern JobType jtUPDATE_PF;
extern JobType jtCLIENT;
extern JobType jtRPC;
extern JobType jtTRANSACTION;
extern JobType jtUNL;
extern JobType jtADVANCE;
extern JobType jtPUBLEDGER;
extern JobType jtTXN_DATA;
extern JobType jtWAL;
extern JobType jtVALIDATION_t;
extern JobType jtWRITE;
extern JobType jtACCEPT;
extern JobType jtPROPOSAL_t;
extern JobType jtSWEEP;
extern JobType jtNETOP_CLUSTER;
extern JobType jtNETOP_TIMER;
extern JobType jtADMIN;
extern JobType jtPEER;
extern JobType jtDISK;
extern JobType jtTXN_PROC;
extern JobType jtOB_SETUP;
extern JobType jtPATH_FIND;
extern JobType jtHO_READ;
extern JobType jtHO_WRITE;
extern JobType jtGENERIC;

#endif
