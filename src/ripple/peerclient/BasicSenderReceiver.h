//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_PEERCLIENT_BASICSENDERRECEIVER_H_INCLUDED
#define RIPPLE_PEERCLIENT_BASICSENDERRECEIVER_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/basics/Coroutine.h>
#include <ripple/basics/promises.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/peerclient/MessageScheduler.h>

#include <utility>

namespace ripple {

class Name;

class Named
{
public:
    virtual ~Named() = default;
    /**
     * Stream a short, human-readbale identifier for this object.
     *
     * This is the method that derived classes should override.
     */
    virtual void
    name(std::ostream& out) const = 0;
    /**
     * Return a proxy object with a stream operator.
     *
     * This is the convenience method that everyone should use.
     */
    Name
    name() const;
};

class Name
{
public:
    Named const& named_;

    friend std::ostream&
    operator<<(std::ostream& out, Name const& name)
    {
        name.named_.name(out);
        return out;
    }
};

inline Name
Named::name() const
{
    return Name{*this};
}

class Journaler : public Named
{
protected:
    beast::Journal journal_;

    Journaler(Application& app, char const* name) : journal_(app.journal(name))
    {
    }
};

template <typename T>
class BasicSenderReceiver : public Coroutine<T>,
                            public Journaler,
                            public MessageScheduler::Sender,
                            public MessageScheduler::Receiver
{
private:
    MessageScheduler& mscheduler_;

public:
    using value_type = T;

    BasicSenderReceiver(
        Application& app,
        Scheduler& jscheduler,
        char const* name)
        : Coroutine<T>(jscheduler)
        , Journaler(app, name)
        , mscheduler_(app.getMessageScheduler())
    {
    }

    void
    onDiscard() override
    {
        return this->throw_("discarded");
    }

    void
    onFailure(
        MessageScheduler::RequestId requestId,
        MessageScheduler::FailureCode reason) override
    {
        if (reason == MessageScheduler::FailureCode::SHUTDOWN)
        {
            return this->throw_("shutdown");
        }
        return schedule();
    }

protected:
    void
    start_() override
    {
        JLOG(journal_.info()) << name() << " start";
        schedule();
    }

    void
    schedule()
    {
        if (!mscheduler_.schedule(this))
        {
            return this->throw_("cannot schedule");
        }
    }

    /**
     * This implementation schedules a job to handle the response,
     * but it can be changed by overriding `onSuccess`.
     */
    void
    onSuccess(MessageScheduler::RequestId requestId, MessagePtr const& response)
        override
    {
        this->jscheduler_.schedule(
            [this, requestId, response = std::move(response)]() {
                this->onSuccess_(requestId, response);
            });
    }

    virtual void
    onSuccess_(
        MessageScheduler::RequestId requestId,
        MessagePtr const& response)
    {
    }
};

}  // namespace ripple

#endif
