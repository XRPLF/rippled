//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Copyright (C) 2015 Vadim Zeitlin
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE

#include "soci/error.h"

#include <sstream>
#include <vector>

namespace soci
{

class soci_error_extra_info
{
public:
    soci_error_extra_info()
    {
    }

    // Default copy ctor, assignment operator and dtor are fine.

    char const* get_full_message(std::string const& message)
    {
        if (full_message_.empty())
        {
            full_message_ = message;

            if (!contexts_.empty())
            {
                // This is a hack, but appending the extra context to the
                // message looks much better if we remove the full stop at its
                // end first.
                if (*full_message_.rbegin() == '.')
                    full_message_.erase(full_message_.size() - 1);

                // Now do append all the extra context we have.
                typedef std::vector<std::string>::const_iterator iter_type;
                for (iter_type i = contexts_.begin(); i != contexts_.end(); ++i)
                {
                    full_message_ += " ";
                    full_message_ += *i;
                }

                // It seems better to always terminate the full message with a
                // full stop, even if the original error message didn't have it
                // (and if it had, we just restore the one we chopped off).
                full_message_ += ".";
            }
        }

        return full_message_.c_str();
    }

    void add_context(std::string const& context)
    {
        full_message_.clear();
        contexts_.push_back(context);
    }

private:
    // The full error message, we need to store it as a string as we return a
    // pointer to its contents from get_full_message().
    std::string full_message_;

    // If non-empty, contains extra context for this exception, e.g.
    // information about the SQL statement that resulted in it, with the top
    // element corresponding to the most global context.
    std::vector<std::string> contexts_;
};

namespace
{

// Make a safe, even in presence of exceptions, heap-allocated copy of the
// given object if it's non-null (otherwise just return null pointer).
soci_error_extra_info *make_safe_copy(soci_error_extra_info* info)
{
    try
    {
        return info ? new soci_error_extra_info(*info) : NULL;
    }
    catch (...)
    {
        // Copy ctor of an exception class shouldn't throw to avoid program
        // termination, so it's better to lose the extra information than allow
        // an exception to except from here.
        return NULL;
    }
}

} // anonymous namespace

soci_error::soci_error(std::string const & msg)
     : std::runtime_error(msg)
{
    info_ = NULL;
}

soci_error::soci_error(soci_error const& e)
    : std::runtime_error(e)
{
    info_ = make_safe_copy(e.info_);
}

soci_error& soci_error::operator=(soci_error const& e)
{
    std::runtime_error::operator=(e);

    delete info_;
    info_ = make_safe_copy(e.info_);

    return *this;
}

soci_error::~soci_error() throw()
{
    delete info_;
}

std::string soci_error::get_error_message() const
{
    return std::runtime_error::what();
}

char const* soci_error::what() const throw()
{
    if (info_)
        return info_->get_full_message(get_error_message());

    return std::runtime_error::what();
}

void soci_error::add_context(std::string const& context)
{
    if (!info_)
        info_ = new soci_error_extra_info();

    info_->add_context(context);
}

} // namespace soci
