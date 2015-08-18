//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_PREPARE_TEMP_TYPE_INCLUDED
#define SOCI_PREPARE_TEMP_TYPE_INCLUDED

#include "soci/into-type.h"
#include "soci/use-type.h"
#include "soci/use.h"
#include "soci/ref-counted-prepare-info.h"

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

    template <typename T, typename Indicator>
    prepare_temp_type &operator,(into_container<T, Indicator> const &ic)
    {
        rcpi_->exchange(ic);
        return *this;
    }
    template <typename T, typename Indicator>
    prepare_temp_type &operator,(use_container<T, Indicator> const &uc)
    {
        rcpi_->exchange(uc);
        return *this;
    }

    ref_counted_prepare_info * get_prepare_info() const { return rcpi_; }


private:
    ref_counted_prepare_info * rcpi_;
};

} // namespace details

} // namespace soci

#endif
