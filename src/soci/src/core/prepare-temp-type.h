//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_PREPARE_TEMP_TYPE_INCLUDED
#define SOCI_PREPARE_TEMP_TYPE_INCLUDED

#include "into-type.h"
#include "use-type.h"
#include "ref-counted-prepare-info.h"

namespace soci
{

namespace details
{

// this needs to be lightweight and copyable
class SOCI_DECL prepare_temp_type
{
public:
    prepare_temp_type(session &);
    prepare_temp_type(prepare_temp_type const &);
    prepare_temp_type & operator=(prepare_temp_type const &);

    ~prepare_temp_type();

    template <typename T>
    prepare_temp_type & operator<<(T const & t)
    {
        rcpi_->accumulate(t);
        return *this;
    }

    prepare_temp_type & operator,(into_type_ptr const & i);
    prepare_temp_type & operator,(use_type_ptr const & u);

    ref_counted_prepare_info * get_prepare_info() const { return rcpi_; }

private:
    ref_counted_prepare_info * rcpi_;
};

} // namespace details

} // namespace soci

#endif
