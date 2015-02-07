//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, Rafal Bobrowski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_FIREBIRD_SOURCE
#include "soci-firebird.h"
#include "common.h"

using namespace soci;
using namespace soci::details;
using namespace soci::details::firebird;

void firebird_vector_into_type_backend::define_by_pos(
    int & position, void * data, exchange_type type)
{
    position_ = position-1;
    data_ = data;
    type_ = type;

    ++position;

    statement_.intoType_ = eVector;
    statement_.intos_.push_back(static_cast<void*>(this));

    XSQLVAR *var = statement_.sqldap_->sqlvar+position_;

    buf_ = allocBuffer(var);
    var->sqldata = buf_;
    var->sqlind = &indISCHolder_;
}

void firebird_vector_into_type_backend::pre_fetch()
{
    // Nothing to do here.
}

namespace // anonymous
{
template <typename T>
void setIntoVector(void *p, std::size_t indx, T const &val)
{
    std::vector<T> *dest =
        static_cast<std::vector<T> *>(p);

    std::vector<T> &v = *dest;
    v[indx] = val;
}

} // namespace anonymous

// this will exchange data with vector user buffers
void firebird_vector_into_type_backend::exchangeData(std::size_t row)
{
    XSQLVAR *var = statement_.sqldap_->sqlvar+position_;

    switch (type_)
    {
        // simple cases
    case x_char:
        setIntoVector(data_, row, getTextParam(var)[0]);
        break;
    case x_short:
        {
            short tmp = from_isc<short>(var);
            setIntoVector(data_, row, tmp);
        }
        break;
    case x_integer:
        {
            int tmp = from_isc<int>(var);
            setIntoVector(data_, row, tmp);
        }
        break;
    case x_long_long:
        {
            long long tmp = from_isc<long long>(var);
            setIntoVector(data_, row, tmp);
        }
        break;
    case x_double:
        {
            double tmp = from_isc<double>(var);
            setIntoVector(data_, row, tmp);
        }
        break;

        // cases that require adjustments and buffer management
    case x_stdstring:
        setIntoVector(data_, row, getTextParam(var));
        break;
    case x_stdtm:
        {
            std::tm data;
            tmDecode(var->sqltype, buf_, &data);
            setIntoVector(data_, row, data);
        }
        break;

    default:
        throw soci_error("Into vector element used with non-supported type.");
    } // switch

}

void firebird_vector_into_type_backend::post_fetch(
    bool gotData, indicator * ind)
{
    // Here we have to set indicators only. Data was exchanged with user
    // buffers during fetch()
    if (gotData)
    {
        std::size_t rows = statement_.rowsFetched_;

        for (std::size_t i = 0; i<rows; ++i)
        {
            if (statement_.inds_[position_][i] == i_null && (ind == NULL))
            {
                throw soci_error("Null value fetched and no indicator defined.");
            }
            else if (ind != NULL)
            {
                ind[i] = statement_.inds_[position_][i];
            }
        }
    }
}

void firebird_vector_into_type_backend::resize(std::size_t sz)
{
    switch (type_)
    {
    case x_char:
        resizeVector<char> (data_, sz);
        break;
    case x_short:
        resizeVector<short> (data_, sz);
        break;
    case x_integer:
        resizeVector<int> (data_, sz);
        break;
    case x_long_long:
        resizeVector<long long> (data_, sz);
        break;
    case x_double:
        resizeVector<double> (data_, sz);
        break;
    case x_stdstring:
        resizeVector<std::string> (data_, sz);
        break;
    case x_stdtm:
        resizeVector<std::tm> (data_, sz);
        break;

    default:
        throw soci_error("Into vector element used with non-supported type.");
    }
}

std::size_t firebird_vector_into_type_backend::size()
{
    std::size_t sz = 0; // dummy initialization to please the compiler
    switch (type_)
    {
        // simple cases
    case x_char:
        sz = getVectorSize<char> (data_);
        break;
    case x_short:
        sz = getVectorSize<short> (data_);
        break;
    case x_integer:
        sz = getVectorSize<int> (data_);
        break;
    case x_long_long:
        sz = getVectorSize<long long> (data_);
        break;
    case x_double:
        sz = getVectorSize<double> (data_);
        break;
    case x_stdstring:
        sz = getVectorSize<std::string> (data_);
        break;
    case x_stdtm:
        sz = getVectorSize<std::tm> (data_);
        break;

    default:
        throw soci_error("Into vector element used with non-supported type.");
    }

    return sz;
}

void firebird_vector_into_type_backend::clean_up()
{
    if (buf_ != NULL)
    {
        delete [] buf_;
        buf_ = NULL;
    }
    std::vector<void*>::iterator it = 
        std::find(statement_.intos_.begin(), statement_.intos_.end(), this);
    if (it != statement_.intos_.end())
        statement_.intos_.erase(it);
}
