//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, Rafal Bobrowski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_FIREBIRD_SOURCE
#include "soci/firebird/soci-firebird.h"
#include "soci-exchange-cast.h"
#include "firebird/common.h"
#include "soci/soci.h"

using namespace soci;
using namespace soci::details;
using namespace soci::details::firebird;

void firebird_standard_use_type_backend::bind_by_pos(
    int & position, void * data, exchange_type type, bool /* readOnly */)
{
    if (statement_.boundByName_)
    {
        throw soci_error(
         "Binding for use elements must be either by position or by name.");
    }

    position_ = position-1;
    data_ = data;
    type_ = type;

    ++position;

    statement_.useType_ = eStandard;
    statement_.uses_.push_back(static_cast<void*>(this));

    XSQLVAR *var = statement_.sqlda2p_->sqlvar+position_;

    buf_ = allocBuffer(var);
    var->sqldata = buf_;
    var->sqlind = &indISCHolder_;

    statement_.boundByPos_ = true;
}

void firebird_standard_use_type_backend::bind_by_name(
    std::string const & name, void * data,
    exchange_type type, bool /* readOnly */)
{
    if (statement_.boundByPos_)
    {
        throw soci_error(
         "Binding for use elements must be either by position or by name.");
    }

    std::map <std::string, int> :: iterator idx =
        statement_.names_.find(name);

    if (idx == statement_.names_.end())
    {
        throw soci_error("Missing use element for bind by name (" + name + ")");
    }

    position_ = idx->second;
    data_ = data;
    type_ = type;

    statement_.useType_ = eStandard;
    statement_.uses_.push_back(static_cast<void*>(this));

    XSQLVAR *var = statement_.sqlda2p_->sqlvar+position_;

    buf_ = allocBuffer(var);
    var->sqldata = buf_;
    var->sqlind = &indISCHolder_;

    statement_.boundByName_ = true;
}

void firebird_standard_use_type_backend::pre_use(indicator const * ind)
{
    indISCHolder_ =  0;
    if (ind)
    {
        switch (*ind)
        {
            case i_null:
                indISCHolder_ = -1;
                break;
            case i_ok:
                indISCHolder_ =  0;
                break;
            default:
                throw soci_error("Unsupported indicator value.");
        }
    }
}

void firebird_standard_use_type_backend::exchangeData()
{
    XSQLVAR *var = statement_.sqlda2p_->sqlvar+position_;

    if (0 != indISCHolder_)
        return;

    switch (type_)
    {
        case x_char:
            setTextParam(&exchange_type_cast<x_char>(data_), 1, buf_, var);
            break;
        case x_short:
            to_isc<short>(data_, var);
            break;
        case x_integer:
            to_isc<int>(data_, var);
            break;
        case x_long_long:
            to_isc<long long>(data_, var);
            break;
        case x_double:
            to_isc<double>(data_, var);
            break;

        case x_stdstring:
            {
                std::string const& tmp = exchange_type_cast<x_stdstring>(data_);
                setTextParam(tmp.c_str(), tmp.size(), buf_, var);
            }
            break;
        case x_stdtm:
            tmEncode(var->sqltype, &exchange_type_cast<x_stdtm>(data_), buf_);
            break;

            // cases that require special handling
        case x_blob:
            {
                blob *tmp = static_cast<blob*>(data_);

                firebird_blob_backend* blob =
                    dynamic_cast<firebird_blob_backend*>(tmp->get_backend());

                if (NULL == blob)
                {
                    throw soci_error("Can't get Firebid BLOB BackEnd");
                }

                blob->save();
                memcpy(buf_, &blob->bid_, var->sqllen);
            }
            break;

        case x_longstring:
            copy_to_blob(exchange_type_cast<x_longstring>(data_).value);
            break;

        case x_xmltype:
            copy_to_blob(exchange_type_cast<x_xmltype>(data_).value);
            break;

        default:
            throw soci_error("Use element used with non-supported type.");
    } // switch
}

void firebird_standard_use_type_backend::copy_to_blob(const std::string& in)
{
    blob_ = new firebird_blob_backend(statement_.session_);
    blob_->append(in.c_str(), in.length());
    blob_->save();
    memcpy(buf_, &blob_->bid_, sizeof(blob_->bid_));
}

void firebird_standard_use_type_backend::post_use(
    bool /* gotData */, indicator * /* ind */)
{
    // TODO: Is it possible to have the bound element being overwritten
    // by the database?
    // If not, then nothing to do here, please remove this comment.
    // If yes, then use the value of the readOnly parameter:
    // - true:  the given object should not be modified and the backend
    //          should detect if the modification was performed on the
    //          isolated buffer and throw an exception if the buffer was modified
    //          (this indicates logic error, because the user used const object
    //          and executed a query that attempted to modified it)
    // - false: the modification should be propagated to the given object.
    // ...
}

void firebird_standard_use_type_backend::clean_up()
{
    if (buf_ != NULL)
    {
        delete [] buf_;
        buf_ = NULL;
    }

    if (blob_)
    {
        delete blob_;
        blob_ = NULL;
    }

    std::vector<void*>::iterator it =
        std::find(statement_.uses_.begin(), statement_.uses_.end(), this);
    if (it != statement_.uses_.end())
        statement_.uses_.erase(it);
}
