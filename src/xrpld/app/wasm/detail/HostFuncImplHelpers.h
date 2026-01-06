#include <xrpl/basics/Expected.h>
#include <xrpl/protocol/STBase.h>

namespace xrpl {

static Expected<Bytes, HostFunctionError>
getAnyFieldData(STBase const* obj)
{
    // auto const& fname = obj.getFName();
    if (!obj)
        return Unexpected(HostFunctionError::FIELD_NOT_FOUND);

    auto const stype = obj->getSType();
    switch (stype)
    {
        // LCOV_EXCL_START
        case STI_UNKNOWN:
        case STI_NOTPRESENT:
            return Unexpected(HostFunctionError::FIELD_NOT_FOUND);
            break;
        // LCOV_EXCL_STOP
        case STI_OBJECT:
        case STI_ARRAY:
            return Unexpected(HostFunctionError::NOT_LEAF_FIELD);
            break;
        case STI_ACCOUNT: {
            auto const* account(static_cast<STAccount const*>(obj));
            auto const& data = account->value();
            return Bytes{data.begin(), data.end()};
        }
        break;
        case STI_AMOUNT:
            // will be processed by serializer
            break;
        case STI_ISSUE: {
            auto const* issue(static_cast<STIssue const*>(obj));
            Asset const& asset(issue->value());
            // XRP and IOU will be processed by serializer
            if (asset.holds<MPTIssue>())
            {
                // MPT
                auto const& mptIssue = asset.get<MPTIssue>();
                auto const& mptID = mptIssue.getMptID();
                return Bytes{mptID.cbegin(), mptID.cend()};
            }
        }
        break;
        case STI_VL: {
            auto const* vl(static_cast<STBlob const*>(obj));
            auto const& data = vl->value();
            return Bytes{data.begin(), data.end()};
        }
        break;
        case STI_UINT16: {
            auto const& num(static_cast<STInteger<std::uint16_t> const*>(obj));
            std::uint16_t const data = num->value();
            auto const* b = reinterpret_cast<uint8_t const*>(&data);
            auto const* e = reinterpret_cast<uint8_t const*>(&data + 1);
            return Bytes{b, e};
        }
        case STI_UINT32: {
            auto const* num(static_cast<STInteger<std::uint32_t> const*>(obj));
            std::uint32_t const data = num->value();
            auto const* b = reinterpret_cast<uint8_t const*>(&data);
            auto const* e = reinterpret_cast<uint8_t const*>(&data + 1);
            return Bytes{b, e};
        }
        break;
        default:
            break;  // default to serializer
    }

    Serializer msg;
    obj->add(msg);
    auto const data = msg.getData();

    return data;
}

static inline bool
noField(STBase const* field)
{
    return !field || (STI_NOTPRESENT == field->getSType()) ||
        (STI_UNKNOWN == field->getSType());
}

static Expected<STBase const*, HostFunctionError>
locateField(STObject const& obj, Slice const& locator)
{
    if (locator.empty() || (locator.size() & 3))  // must be multiple of 4
        return Unexpected(HostFunctionError::LOCATOR_MALFORMED);

    int32_t const* locPtr = reinterpret_cast<int32_t const*>(locator.data());
    int32_t const locSize = locator.size() / 4;
    STBase const* field = nullptr;
    auto const& knownSFields = SField::getKnownCodeToField();

    {
        int32_t const sfieldCode = locPtr[0];
        auto const it = knownSFields.find(sfieldCode);
        if (it == knownSFields.end())
            return Unexpected(HostFunctionError::INVALID_FIELD);

        auto const& fname(*it->second);
        field = obj.peekAtPField(fname);
        if (noField(field))
            return Unexpected(HostFunctionError::FIELD_NOT_FOUND);
    }

    for (int i = 1; i < locSize; ++i)
    {
        int32_t const sfieldCode = locPtr[i];

        if (STI_ARRAY == field->getSType())
        {
            auto const* arr = static_cast<STArray const*>(field);
            if (sfieldCode >= arr->size())
                return Unexpected(HostFunctionError::INDEX_OUT_OF_BOUNDS);
            field = &(arr->operator[](sfieldCode));
        }
        else if (STI_OBJECT == field->getSType())
        {
            auto const* o = static_cast<STObject const*>(field);

            auto const it = knownSFields.find(sfieldCode);
            if (it == knownSFields.end())
                return Unexpected(HostFunctionError::INVALID_FIELD);

            auto const& fname(*it->second);
            field = o->peekAtPField(fname);
        }
        else  // simple field must be the last one
        {
            return Unexpected(HostFunctionError::LOCATOR_MALFORMED);
        }

        if (noField(field))
            return Unexpected(HostFunctionError::FIELD_NOT_FOUND);
    }

    return field;
}

}  // namespace xrpl
