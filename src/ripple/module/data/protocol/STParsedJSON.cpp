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

#include <cassert>

#include <beast/module/core/text/LexicalCast.h>

namespace ripple {

STParsedJSON::STParsedJSON (std::string const& name, Json::Value const& json)
{
    parse (name, json, sfGeneric, 0, object);
}

//------------------------------------------------------------------------------

std::string STParsedJSON::make_name (std::string const& object,
    std::string const& field)
{
    if (field.empty ())
        return object;

    return object + "." + field;
}

Json::Value STParsedJSON::not_an_object (std::string const& object,
    std::string const& field)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + make_name (object, field) + "' is not a JSON object.");
}

Json::Value STParsedJSON::unknown_field (std::string const& object,
    std::string const& field)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + make_name (object, field) + "' is unknown.");
}

Json::Value STParsedJSON::out_of_range (std::string const& object,
    std::string const& field)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + make_name (object, field) + "' is out of range.");
}

Json::Value STParsedJSON::bad_type (std::string const& object,
    std::string const& field)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + make_name (object, field) + "' has bad type.");
}

Json::Value STParsedJSON::invalid_data (std::string const& object,
    std::string const& field)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + make_name (object, field) + "' has invalid data.");
}

Json::Value STParsedJSON::array_expected (std::string const& object,
    std::string const& field)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + make_name (object, field) + "' must be a JSON array.");
}

Json::Value STParsedJSON::string_expected (std::string const& object,
    std::string const& field)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + make_name (object, field) + "' must be a string.");
}

Json::Value STParsedJSON::too_deep (std::string const& object,
    std::string const& field)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + make_name (object, field) + "' exceeds nesting depth limit.");
}

Json::Value STParsedJSON::singleton_expected (std::string const& object)
{
    return RPC::make_error (rpcINVALID_PARAMS,
        "Field '" + object +
            "' must be an object with a single key/object value.");
}

//------------------------------------------------------------------------------

bool STParsedJSON::parse (std::string const& json_name,
    Json::Value const& json, SField::ref inName, int depth,
        std::unique_ptr <STObject>& sub_object)
{
    if (! json.isObject ())
    {
        error = not_an_object (json_name);
        return false;
    }

    SField::ptr name (&inName);

    boost::ptr_vector<SerializedType> data;
    Json::Value::Members members (json.getMemberNames ());

    for (Json::Value::Members::iterator it (members.begin ());
        it != members.end (); ++it)
    {
        std::string const& fieldName = *it;
        Json::Value const& value = json [fieldName];

        SField::ref field = SField::getField (fieldName);

        if (field == sfInvalid)
        {
            error = unknown_field (json_name, fieldName);
            return false;
        }

        switch (field.fieldType)
        {
        case STI_UINT8:
            try
            {
                if (value.isString ())
                {
                    // VFALCO TODO wtf?
                }
                else if (value.isInt ())
                {
                    if (value.asInt () < 0 || value.asInt () > 255)
                    {
                        error = out_of_range (json_name, fieldName);
                        return false;
                    }

                    data.push_back (new STUInt8 (field,
                        range_check_cast <unsigned char> (
                            value.asInt (), 0, 255)));
                }
                else if (value.isUInt ())
                {
                    if (value.asUInt () > 255)
                    {
                        error = out_of_range (json_name, fieldName);
                        return false;
                    }

                    data.push_back (new STUInt8 (field,
                        range_check_cast <unsigned char> (
                            value.asUInt (), 0, 255)));
                }
                else
                {
                    error = bad_type (json_name, fieldName);
                    return false;
                }
            }
            catch (...)
            {
                error = invalid_data (json_name, fieldName);
                return false;
            }

            break;

        case STI_UINT16:
            try
            {
                if (value.isString ())
                {
                    std::string strValue = value.asString ();

                    if (! strValue.empty () &&
                        ((strValue[0] < '0') || (strValue[0] > '9')))
                    {
                        if (field == sfTransactionType)
                        {
                            TxType const txType (TxFormats::getInstance()->
                                findTypeByName (strValue));

                            data.push_back (new STUInt16 (field,
                                static_cast <std::uint16_t> (txType)));

                            if (*name == sfGeneric)
                                name = &sfTransaction;
                        }
                        else if (field == sfLedgerEntryType)
                        {
                            LedgerEntryType const type (LedgerFormats::getInstance()->
                                findTypeByName (strValue));

                            data.push_back (new STUInt16 (field,
                                static_cast <std::uint16_t> (type)));

                            if (*name == sfGeneric)
                                name = &sfLedgerEntry;
                        }
                        else
                        {
                            error = invalid_data (json_name, fieldName);
                            return false;
                        }
                    }
                    else
                    {
                        data.push_back (new STUInt16 (field,
                            beast::lexicalCastThrow <std::uint16_t> (strValue)));
                    }
                }
                else if (value.isInt ())
                {
                    data.push_back (new STUInt16 (field,
                        range_check_cast <std::uint16_t> (
                            value.asInt (), 0, 65535)));
                }
                else if (value.isUInt ())
                {
                    data.push_back (new STUInt16 (field,
                        range_check_cast <std::uint16_t> (
                            value.asUInt (), 0, 65535)));
                }
                else
                {
                    error = bad_type (json_name, fieldName);
                    return false;
                }
            }
            catch (...)
            {
                error = invalid_data (json_name, fieldName);
                return false;
            }

            break;

        case STI_UINT32:
            try
            {
                if (value.isString ())
                {
                    data.push_back (new STUInt32 (field,
                        beast::lexicalCastThrow <std::uint32_t> (value.asString ())));
                }
                else if (value.isInt ())
                {
                    data.push_back (new STUInt32 (field,
                        range_check_cast <std::uint32_t> (value.asInt (), 0u, 4294967295u)));
                }
                else if (value.isUInt ())
                {
                    data.push_back (new STUInt32 (field,
                        static_cast <std::uint32_t> (value.asUInt ())));
                }
                else
                {
                    error = bad_type (json_name, fieldName);
                    return false;
                }
            }
            catch (...)
            {
                error = invalid_data (json_name, fieldName);
                return false;
            }

            break;

        case STI_UINT64:
            try
            {
                if (value.isString ())
                {
                    data.push_back (new STUInt64 (field,
                        uintFromHex (value.asString ())));
                }
                else if (value.isInt ())
                {
                    data.push_back (new STUInt64 (field,
                        range_check_cast<std::uint64_t> (
                            value.asInt (), 0, 18446744073709551615ull)));
                }
                else if (value.isUInt ())
                {
                    data.push_back (new STUInt64 (field,
                        static_cast <std::uint64_t> (value.asUInt ())));
                }
                else
                {
                    error = bad_type (json_name, fieldName);
                    return false;
                }
            }
            catch (...)
            {
                error = invalid_data (json_name, fieldName);
                return false;
            }

            break;

        case STI_HASH128:
            try
            {
                if (value.isString ())
                {
                    data.push_back (new STHash128 (field, value.asString ()));
                }
                else
                {
                    error = bad_type (json_name, fieldName);
                    return false;
                }
            }
            catch (...)
            {
                error = invalid_data (json_name, fieldName);
                return false;
            }

            break;

        case STI_HASH160:
            try
            {
                if (value.isString ())
                {
                    data.push_back (new STHash160 (field, value.asString ()));
                }
                else
                {
                    error = bad_type (json_name, fieldName);
                    return false;
                }
            }
            catch (...)
            {
                error = invalid_data (json_name, fieldName);
                return false;
            }

            break;

        case STI_HASH256:
            try
            {
                if (value.isString ())
                {
                    data.push_back (new STHash256 (field, value.asString ()));
                }
                else
                {
                    error = bad_type (json_name, fieldName);
                    return false;
                }
            }
            catch (...)
            {
                error = invalid_data (json_name, fieldName);
                return false;
            }

            break;

        case STI_VL:
            if (! value.isString ())
            {
                error = bad_type (json_name, fieldName);
                return false;
            }

            try
            {
                std::pair<Blob, bool> ret(strUnHex (value.asString ()));

                if (!ret.second)
                    throw std::invalid_argument ("invalid data");

                data.push_back (new STVariableLength (field, ret.first));
            }
            catch (...)
            {
                error = invalid_data (json_name, fieldName);
                return false;
            }

            break;

        case STI_AMOUNT:
            try
            {
                data.push_back (new STAmount (field, value));
            }
            catch (...)
            {
                error = invalid_data (json_name, fieldName);
                return false;
            }

            break;

        case STI_VECTOR256:
            if (! value.isArray ())
            {
                error = array_expected (json_name, fieldName);
                return false;
            }
            
            try
            {
                data.push_back (new STVector256 (field));
                STVector256* tail (dynamic_cast <STVector256*> (&data.back ()));
                assert (tail);

                for (Json::UInt i = 0; value.isValidIndex (i); ++i)
                {
                    uint256 s;
                    s.SetHex (value[i].asString ());
                    tail->addValue (s);
                }
            }
            catch (...)
            {
                error = invalid_data (json_name, fieldName);
                return false;
            }

            break;

        case STI_PATHSET:
            if (!value.isArray ())
            {
                error = array_expected (json_name, fieldName);
                return false;
            }

            try
            {
                data.push_back (new STPathSet (field));
                STPathSet* tail = dynamic_cast <STPathSet*> (&data.back ());
                assert (tail);

                for (Json::UInt i = 0; value.isValidIndex (i); ++i)
                {
                    STPath p;

                    if (!value[i].isArray ())
                    {
                        std::stringstream ss;
                        ss << fieldName << "[" << i << "]";
                        error = array_expected (json_name, ss.str ());
                        return false;
                    }

                    for (Json::UInt j = 0; value[i].isValidIndex (j); ++j)
                    {
                        std::stringstream ss;
                        ss << fieldName << "[" << i << "][" << j << "]";
                        std::string const element_name (
                            json_name + "." + ss.str());

                        // each element in this path has some combination of account,
                        // currency, or issuer

                        Json::Value pathEl = value[i][j];

                        if (!pathEl.isObject ())
                        {
                            error = not_an_object (element_name);
                            return false;
                        }

                        const Json::Value& account  = pathEl["account"];
                        const Json::Value& currency = pathEl["currency"];
                        const Json::Value& issuer   = pathEl["issuer"];
                        bool hasCurrency            = false;
                        uint160 uAccount, uCurrency, uIssuer;

                        if (! account.isNull ())
                        {
                            // human account id
                            if (! account.isString ())
                            {
                                error = string_expected (element_name, "account");
                                return false;
                            }

                            std::string const strValue (account.asString ());

                            if (value.size () == 40) // 160-bit hex account value
                                uAccount.SetHex (strValue);

                            {
                                RippleAddress a;

                                if (! a.setAccountID (strValue))
                                {
                                    error = invalid_data (element_name, "account");
                                    return false;
                                }

                                uAccount = a.getAccountID ();
                            }
                        }

                        if (!currency.isNull ())
                        {
                            // human currency
                            if (!currency.isString ())
                            {
                                error = string_expected (element_name, "currency");
                                return false;
                            }

                            hasCurrency = true;

                            if (currency.asString ().size () == 40)
                            {
                                uCurrency.SetHex (currency.asString ());
                            }
                            else if (!STAmount::currencyFromString (
                                uCurrency, currency.asString ()))
                            {
                                error = invalid_data (element_name, "currency");
                                return false;
                            }
                        }

                        if (!issuer.isNull ())
                        {
                            // human account id
                            if (!issuer.isString ())
                            {
                                error = string_expected (element_name, "issuer");
                                return false;
                            }

                            if (issuer.asString ().size () == 40)
                            {
                                uIssuer.SetHex (issuer.asString ());
                            }
                            else
                            {
                                RippleAddress a;

                                if (!a.setAccountID (issuer.asString ()))
                                {
                                    error = invalid_data (element_name, "issuer");
                                    return false;
                                }

                                uIssuer = a.getAccountID ();
                            }
                        }

                        p.addElement (STPathElement (uAccount, uCurrency, uIssuer, hasCurrency));
                    }

                    tail->addPath (p);
                }
            }
            catch (...)
            {
                error = invalid_data (json_name, fieldName);
                return false;
            }

            break;

        case STI_ACCOUNT:
        {
            if (! value.isString ())
            {
                error = bad_type (json_name, fieldName);
                return false;
            }

            std::string strValue = value.asString ();

            try
            {
                if (value.size () == 40) // 160-bit hex account value
                {
                    uint160 v;
                    v.SetHex (strValue);
                    data.push_back (new STAccount (field, v));
                }
                else
                {
                    // ripple address
                    RippleAddress a;

                    if (!a.setAccountID (strValue))
                    {
                        error = invalid_data (json_name, fieldName);
                        return false;
                    }

                    data.push_back (new STAccount (field, a.getAccountID ()));
                }
            }
            catch (...)
            {
                error = invalid_data (json_name, fieldName);
                return false;
            }
        }
        break;

        case STI_OBJECT:
        case STI_TRANSACTION:
        case STI_LEDGERENTRY:
        case STI_VALIDATION:
            if (! value.isObject ())
            {
                error = not_an_object (json_name, fieldName);
                return false;
            }

            if (depth > 64)
            {
                error = too_deep (json_name, fieldName);
                return false;
            }

            try
            {
                std::unique_ptr <STObject> sub_object_;
                bool const success (parse (json_name + "." + fieldName,
                    value, field, depth + 1, sub_object_));
                if (! success)
                    return false;
                data.push_back (sub_object_.release ());
            }
            catch (...)
            {
                error = invalid_data (json_name, fieldName);
                return false;
            }

            break;

        case STI_ARRAY:
            if (! value.isArray ())
            {
                error = array_expected (json_name, fieldName);
                return false;
            }

            try
            {
                data.push_back (new STArray (field));
                STArray* tail = dynamic_cast<STArray*> (&data.back ());
                assert (tail);

                for (Json::UInt i = 0; value.isValidIndex (i); ++i)
                {
                    bool const isObject (value[i].isObject());
                    bool const singleKey (isObject
                        ? value [i].size() == 1
                        : true);

                    if (!isObject || !singleKey)
                    {
                        std::stringstream ss;
                        ss << json_name << "." << fieldName << "[" << i << "]";
                        error = singleton_expected (ss.str ());
                        return false;
                    }

                    // TODO: There doesn't seem to be a nice way to get just the
                    // first/only key in an object without copying all keys into
                    // a vector
                    std::string const objectName (value[i].getMemberNames()[0]);;
                    SField::ref       nameField (SField::getField(objectName));

                    if (nameField == sfInvalid)
                    {
                        error = unknown_field (json_name, objectName);
                        return false;
                    }

                    Json::Value const objectFields (value[i][objectName]);

                    std::unique_ptr <STObject> sub_object_;
                    {
                        std::stringstream ss;
                        ss << json_name << "." << fieldName <<
                            "[" << i << "]." << objectName;
                        bool const success (parse (ss.str (), objectFields,
                            nameField, depth + 1, sub_object_));
                        if (! success)
                            return false;
                    }
                    tail->push_back (*sub_object_);
                }
            }
            catch (...)
            {
                error = invalid_data (json_name, fieldName);
                return false;
            }

            break;

        default:
            error = bad_type (json_name, fieldName);
            return false;
        }
    }

    sub_object.reset (new STObject (*name, data));
    return true;
}

} // ripple
