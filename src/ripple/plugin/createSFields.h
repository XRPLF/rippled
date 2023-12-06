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

#ifndef RIPPLE_PLUGIN_CREATESFIELDS_H_INCLUDED
#define RIPPLE_PLUGIN_CREATESFIELDS_H_INCLUDED

#include <ripple/protocol/SField.h>

namespace ripple {

template <typename T>
int
getSTId()
{
    return STI_UNKNOWN;
}

template <>
int
getSTId<SF_UINT8>()
{
    return STI_UINT8;
}

template <>
int
getSTId<SF_UINT16>()
{
    return STI_UINT16;
}

template <>
int
getSTId<SF_UINT32>()
{
    return STI_UINT32;
}

template <>
int
getSTId<SF_UINT64>()
{
    return STI_UINT64;
}

template <>
int
getSTId<SF_UINT128>()
{
    return STI_UINT128;
}

template <>
int
getSTId<SF_UINT256>()
{
    return STI_UINT256;
}

template <>
int
getSTId<SF_UINT160>()
{
    return STI_UINT160;
}

template <>
int
getSTId<SF_AMOUNT>()
{
    return STI_AMOUNT;
}

template <>
int
getSTId<SF_VL>()
{
    return STI_VL;
}

template <>
int
getSTId<SF_ACCOUNT>()
{
    return STI_ACCOUNT;
}

template <>
int
getSTId<STObject>()
{
    return STI_OBJECT;
}

template <>
int
getSTId<STArray>()
{
    return STI_ARRAY;
}

template <>
int
getSTId<SF_VECTOR256>()
{
    return STI_VECTOR256;
}

template <>
int
getSTId<SF_UINT96>()
{
    return STI_UINT96;
}

template <>
int
getSTId<SF_UINT192>()
{
    return STI_UINT192;
}

template <>
int
getSTId<SF_UINT384>()
{
    return STI_UINT384;
}

template <>
int
getSTId<SF_UINT512>()
{
    return STI_UINT512;
}

template <>
int
getSTId<SF_ISSUE>()
{
    return STI_ISSUE;
}

template <class T>
T const&
newSField(const int fieldValue, char const* fieldName)
{
    int const typeId = getSTId<T>();
    if (typeId == STI_ARRAY || typeId == STI_OBJECT)
    {
        // TODO: merge `newSField` and `newUntypedSField` for a seamless
        // experience
        throw std::runtime_error(
            "Must use `newUntypedSField` for arrays and objects");
    }
    if (SField const& field = SField::getField(fieldName); field != sfInvalid)
    {
        if (field.fieldValue != fieldValue)
        {
            throw std::runtime_error(
                "Existing value for " + std::string(fieldName) +
                " doesn't match: Expected " + std::to_string(field.fieldValue) +
                ", received " + std::to_string(fieldValue));
        }
        return static_cast<T const&>(field);
    }
    if (SField const& field = SField::getField(field_code(typeId, fieldValue));
        field != sfInvalid)
    {
        if (std::string(field.fieldName) != std::string(fieldName))
            throw std::runtime_error(
                "SField (type " + std::to_string(typeId) + ", field value " +
                std::to_string(fieldValue) + ") already exists: sf" +
                std::string(field.fieldName));
    }
    T const* newSField = new T(typeId, fieldValue, fieldName);
    return *newSField;
}

template <class T>
T const&
newSField(const int fieldValue, std::string const fieldName)
{
    return newSField<T>(fieldValue, fieldName.c_str());
}

template <class T>
SField const&
newUntypedSField(const int fieldValue, char const* fieldName)
{
    if (SField const& field = SField::getField(fieldName); field != sfInvalid)
    {
        return field;
    }
    SField const* newSField = new SField(getSTId<T>(), fieldValue, fieldName);
    return *newSField;
}

SF_PLUGINTYPE const&
constructCustomSField(int tid, int fv, const char* fn)
{
    if (SField const& field = SField::getField(field_code(tid, fv));
        field != sfInvalid)
        return reinterpret_cast<SF_PLUGINTYPE const&>(field);
    return *(new SF_PLUGINTYPE(tid, fv, fn));
}

}  // namespace ripple

#endif
