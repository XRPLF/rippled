#pragma once

#include <xrpl/protocol/KnownFormats.h>

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

    SOTemplate const*
    findSOTemplateBySField(SField const& sField) const;
};

}  // namespace xrpl
