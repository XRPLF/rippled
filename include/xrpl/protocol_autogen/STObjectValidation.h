#pragma once

#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STObject.h>

namespace xrpl::protocol_autogen {

[[nodiscard]]
bool
validateSTObject(STObject const& obj, SOTemplate const& format)
{
    for (auto const& field : format)
    {
        if (obj.isFieldPresent(field.sField()) && field.style() == soeREQUIRED)
        {
            return false;
        }

        if (!field.supportMPT() && obj.isFieldPresent(field.sField()) &&
            field.sField().fieldType == STI_ISSUE)
        {
            auto const& amount = obj.getFieldAmount(field.sField());

            if (amount.asset().holds<MPTIssue>())
                return false;
        }
    }

    return true;
}

}  // namespace xrpl::protocol_autogen
