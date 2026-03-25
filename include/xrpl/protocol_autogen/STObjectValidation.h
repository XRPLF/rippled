#pragma once

#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STObject.h>

namespace xrpl::protocol_autogen {

[[nodiscard]]
inline bool
validateSTObject(STObject const& obj, SOTemplate const& format)
{
    for (auto const& field : format)
    {
        if (!obj.isFieldPresent(field.sField()) && field.style() == soeREQUIRED)
        {
            return false;  // LCOV_EXCL_LINE
        }

        if (field.supportMPT() == soeMPTNotSupported && obj.isFieldPresent(field.sField()))
        {
            if (field.sField().fieldType == STI_AMOUNT)
            {
                auto const& amount = obj.getFieldAmount(field.sField());

                if (amount.asset().holds<MPTIssue>())
                    return false;  // LCOV_EXCL_LINE
            }
            else if (field.sField().fieldType == STI_ISSUE)
            {
                auto issue = dynamic_cast<STIssue const*>(obj.peekAtPField(field.sField()));
                if (!issue)
                    return false;  // LCOV_EXCL_LINE

                if (issue->holds<MPTIssue>())
                    return false;  // LCOV_EXCL_LINE
            }
        }
    }

    return true;
}

}  // namespace xrpl::protocol_autogen
