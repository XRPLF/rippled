//
// Copyright (C) 2004-2008 Maciej Sobczak
// Copyright (C) 2015 Vadim Zeitlin
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_ERROR_H_INCLUDED
#define SOCI_ERROR_H_INCLUDED

#include "soci/soci-platform.h"
// std
#include <stdexcept>
#include <string>

namespace soci
{

class SOCI_DECL soci_error : public std::runtime_error
{
public:
    explicit soci_error(std::string const & msg);

    soci_error(soci_error const& e);
    soci_error& operator=(soci_error const& e);

    virtual ~soci_error() throw();

    // Returns just the error message itself, without the context.
    std::string get_error_message() const;

    // Returns the full error message combining the message given to the ctor
    // with all the available context records.
    virtual char const* what() const throw();

    // This is used only by SOCI itself to provide more information about the
    // exception as it bubbles up. It can be called multiple times, with the
    // first call adding the lowest level context and the last one -- the
    // highest level context.
    void add_context(std::string const& context);

private:
    // Optional extra information (currently just the context data).
    class soci_error_extra_info* info_;
};

} // namespace soci

#endif // SOCI_ERROR_H_INCLUDED
