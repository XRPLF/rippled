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

#ifndef RIPPLE_JOB_H
#define RIPPLE_JOB_H

class Job
{
public:
    // Jobs are not default-constructible or assignable.
    Job& operator= (Job const& other) = delete;
    Job () = delete;

    Job (Job const& other);

    Job (JobType& type, uint64 index);

    Job (JobType& type,
         std::string const& name,
         uint64 index,
         std::function <void (Job&)> const& work,
         CancelCallback cancelCallback);

    JobType& getType () const;

    CancelCallback getCancelCallback () const;

    /** Returns `true` if the running job should make a best-effort cancel. */
    bool shouldCancel () const;

    void work ();

    void rename (const std::string& n);

    // These comparison operators make the jobs sort in priority order in the job set
    bool operator< (Job const& j) const;
    bool operator> (Job const& j) const;
    bool operator<= (Job const& j) const;
    bool operator>= (Job const& j) const;

private:
    CancelCallback m_cancelCallback;
    std::reference_wrapper<JobType> m_type;
    uint64 m_index;
    std::function <void (Job&)> m_work;
    LoadEvent::pointer m_loadEvent;
    std::string m_name;
};

#endif
