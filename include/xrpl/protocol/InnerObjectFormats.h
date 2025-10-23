#ifndef XRPL_PROTOCOL_INNER_OBJECT_FORMATS_H_INCLUDED
#define XRPL_PROTOCOL_INNER_OBJECT_FORMATS_H_INCLUDED

#include <xrpl/protocol/KnownFormats.h>

namespace ripple {

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

}  // namespace ripple

#endif
