//
// Copyright (C) 2015 Maciej Sobczak
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_CALLBACKS_H_INCLUDED
#define SOCI_CALLBACKS_H_INCLUDED

namespace soci
{

class session;

// Simple callback interface for reporting failover events.
// The meaning of each operation is intended to be portable,
// but the behaviour details and parameters can be backend-specific.
class SOCI_DECL failover_callback
{
public:

    // Called when the failover operation has started,
    // after discovering connectivity problems.
    virtual void started() {}

    // Called after successful failover and creating a new connection;
    // the sql parameter denotes the new connection and allows the user
    // to replay any initial sequence of commands (like session configuration).
    virtual void finished(session & /* sql */) {}

    // Called when the attempt to reconnect failed,
    // if the user code sets the retry parameter to true,
    // then new connection will be attempted;
    // the newTarget connection string is a hint that can be ignored
    // by external means.
    virtual void failed(bool & /* out */ /* retry */,
        std::string & /* out */ /* newTarget */) {}

    // Called when there was a failure that prevents further failover attempts.
    virtual void aborted() {}
};

} // namespace soci

#endif // SOCI_CALLBACKS_H_INCLUDED

