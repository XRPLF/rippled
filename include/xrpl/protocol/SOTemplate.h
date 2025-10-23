#ifndef XRPL_PROTOCOL_SOTEMPLATE_H_INCLUDED
#define XRPL_PROTOCOL_SOTEMPLATE_H_INCLUDED

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/SField.h>

#include <functional>
#include <initializer_list>
#include <stdexcept>

namespace ripple {

/** Kind of element in each entry of an SOTemplate. */
enum SOEStyle {
    soeINVALID = -1,
    soeREQUIRED = 0,  // required
    soeOPTIONAL = 1,  // optional, may be present with default value
    soeDEFAULT = 2,   // optional, if present, must not have default value
                      // inner object with the default fields has to be
                      // constructed with STObject::makeInnerObject()
};

/** Amount fields that can support MPT */
enum SOETxMPTIssue { soeMPTNone, soeMPTSupported, soeMPTNotSupported };

//------------------------------------------------------------------------------

/** An element in a SOTemplate. */
class SOElement
{
    // Use std::reference_wrapper so SOElement can be stored in a std::vector.
    std::reference_wrapper<SField const> sField_;
    SOEStyle style_;
    SOETxMPTIssue supportMpt_ = soeMPTNone;

private:
    void
    init(SField const& fieldName) const
    {
        if (!sField_.get().isUseful())
        {
            auto nm = std::to_string(fieldName.getCode());
            if (fieldName.hasName())
                nm += ": '" + fieldName.getName() + "'";
            Throw<std::runtime_error>(
                "SField (" + nm + ") in SOElement must be useful.");
        }
    }

public:
    SOElement(SField const& fieldName, SOEStyle style)
        : sField_(fieldName), style_(style)
    {
        init(fieldName);
    }

    template <typename T>
        requires(std::is_same_v<T, STAmount> || std::is_same_v<T, STIssue>)
    SOElement(
        TypedField<T> const& fieldName,
        SOEStyle style,
        SOETxMPTIssue supportMpt = soeMPTNotSupported)
        : sField_(fieldName), style_(style), supportMpt_(supportMpt)
    {
        init(fieldName);
    }

    SField const&
    sField() const
    {
        return sField_.get();
    }

    SOEStyle
    style() const
    {
        return style_;
    }

    SOETxMPTIssue
    supportMPT() const
    {
        return supportMpt_;
    }
};

//------------------------------------------------------------------------------

/** Defines the fields and their attributes within a STObject.
    Each subclass of SerializedObject will provide its own template
    describing the available fields and their metadata attributes.
*/
class SOTemplate
{
public:
    // Copying vectors is expensive.  Make this a move-only type until
    // there is motivation to change that.
    SOTemplate(SOTemplate&& other) = default;
    SOTemplate&
    operator=(SOTemplate&& other) = default;

    /** Create a template populated with all fields.
        After creating the template fields cannot be
        added, modified, or removed.
    */
    SOTemplate(
        std::initializer_list<SOElement> uniqueFields,
        std::initializer_list<SOElement> commonFields = {});

    /* Provide for the enumeration of fields */
    std::vector<SOElement>::const_iterator
    begin() const
    {
        return elements_.cbegin();
    }

    std::vector<SOElement>::const_iterator
    cbegin() const
    {
        return begin();
    }

    std::vector<SOElement>::const_iterator
    end() const
    {
        return elements_.cend();
    }

    std::vector<SOElement>::const_iterator
    cend() const
    {
        return end();
    }

    /** The number of entries in this template */
    std::size_t
    size() const
    {
        return elements_.size();
    }

    /** Retrieve the position of a named field. */
    int
    getIndex(SField const&) const;

    SOEStyle
    style(SField const& sf) const
    {
        return elements_[indices_[sf.getNum()]].style();
    }

private:
    std::vector<SOElement> elements_;
    std::vector<int> indices_;  // field num -> index
};

}  // namespace ripple

#endif
