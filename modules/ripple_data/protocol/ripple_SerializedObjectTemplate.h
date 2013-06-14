#ifndef RIPPLE_SERIALIZEDOBJECTTEMPLATE_H
#define RIPPLE_SERIALIZEDOBJECTTEMPLATE_H

//------------------------------------------------------------------------------

/** Flags for elements in a SerializedObjectTemplate.
*/
// VFALCO NOTE these don't look like bit-flags...
enum SOE_Flags
{
    SOE_INVALID  = -1,
    SOE_REQUIRED = 0,   // required
    SOE_OPTIONAL = 1,   // optional, may be present with default value
    SOE_DEFAULT  = 2,   // optional, if present, must not have default value
};

//------------------------------------------------------------------------------

/** An element in a SerializedObjectTemplate.
*/
class SOElement
{
public:
    SField::ref const e_field;
    SOE_Flags const   flags;

    SOElement (SField::ref fieldName, SOE_Flags flags)
        : e_field (fieldName)
        , flags (flags)
    {
    }
};

//------------------------------------------------------------------------------

/** Defines the fields and their attributes within a SerializedObject.

    Each subclass of SerializedObject will provide its own template
    describing the available fields and their metadata attributes.
*/
class SOTemplate
{
public:
    /** Create an empty template.

        After creating the template, call @ref push_back with the
        desired fields.

        @see push_back
    */
    SOTemplate ();

    // VFALCO NOTE Why do we even bother with the 'private' keyword if
    //             this function is present?
    //
    std::vector <SOElement const*> const& peek () const
    {
        return mTypes;
    }

    /** Add an element to the template.
    */
    void push_back (SOElement const& r);

    /** Retrieve the position of a named field.
    */
    int getIndex (SField::ref) const;

private:
    std::vector <SOElement const*> mTypes;

    std::vector <int> mIndex;       // field num -> index
};

#endif
