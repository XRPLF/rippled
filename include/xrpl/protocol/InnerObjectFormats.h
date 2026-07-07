#pragma once

#include <xrpl/protocol/KnownFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>

namespace xrpl {

/** Manages the list of known inner object formats.
 */
class InnerObjectFormats : public KnownFormats<int, InnerObjectFormats>
{
private:
    /** Create the object.
        This will load the object with all the known inner object formats.
    */
    InnerObjectFormats();

public:
    static InnerObjectFormats const&
    getInstance();

    [[nodiscard]] SOTemplate const*
    findSOTemplateBySField(SField const& sField) const;
};

}  // namespace xrpl
