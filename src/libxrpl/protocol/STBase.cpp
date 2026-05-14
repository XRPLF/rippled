/** @file
 *  Implements the abstract root of the XRPL serialized-type hierarchy.
 *
 *  Every field that appears in a transaction, ledger entry, validation, or
 *  metadata — integers, amounts, account IDs, path sets, objects, arrays —
 *  derives from `STBase`. This translation unit provides field-name
 *  management, comparison dispatch, text/JSON rendering, binary serialization
 *  plumbing, and the small-object placement interface consumed by
 *  `detail::STVar`.
 */
#include <xrpl/protocol/STBase.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <ostream>
#include <string>
#include <utility>

namespace xrpl {

STBase::STBase() : fName_(&kSF_GENERIC)
{
}

STBase::STBase(SField const& n) : fName_(&n)
{
    XRPL_ASSERT(fName_, "xrpl::STBase::STBase : field is set");
}

/** Copy-assign value but preserve a meaningful field name.
 *
 *  `fName_` is only overwritten when the current name is not useful (i.e.,
 *  it is `sfGeneric` or another placeholder with `fieldCode <= 0`). This
 *  dual-purpose behaviour supports two distinct call sites:
 *
 *  - **Slot initialisation:** a freshly-constructed `STBase` (holding only
 *    `sfGeneric`) inherits the source field name, so the new slot acquires
 *    the correct protocol identity.
 *  - **Element slide-down in `STObject`/`STArray`:** when an element is
 *    erased by sliding remaining elements down via copy assignment, the
 *    destination slot already holds a meaningful field name and keeps it,
 *    preventing the surviving neighbours from adopting each other's names.
 *
 *  @note Do NOT store derived types in a plain `std::vector`. The slide-down
 *      semantics above are the *only* reason this operator does not
 *      unconditionally copy `fName_`. Use Boost pointer containers instead.
 */
STBase&
STBase::operator=(STBase const& t)
{
    if (this == &t)
        return *this;

    if (!fName_->isUseful())
        fName_ = t.fName_;
    return *this;
}

bool
STBase::operator==(STBase const& t) const
{
    return (getSType() == t.getSType()) && isEquivalent(t);
}

bool
STBase::operator!=(STBase const& t) const
{
    return (getSType() != t.getSType()) || !isEquivalent(t);
}

STBase*
STBase::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STBase::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

SerializedTypeID
STBase::getSType() const
{
    return STI_NOTPRESENT;
}

std::string
STBase::getFullText() const
{
    std::string ret;

    if (getSType() != STI_NOTPRESENT)
    {
        if (fName_->hasName())
        {
            ret = fName_->fieldName;
            ret += " = ";
        }

        ret += getText();
    }

    return ret;
}

std::string
STBase::getText() const
{
    return std::string();
}

json::Value
STBase::getJson(JsonOptions /*options*/) const
{
    return getText();
}

/** Serialize this field's value into @p s.
 *
 *  The base implementation is a hard-unreachable stub: every concrete
 *  subclass must override this method. Reaching this body indicates a
 *  missing override in a derived type and is treated as a programming error.
 *
 *  @param s The serializer to write into.
 */
void
STBase::add(Serializer& s) const
{
    // LCOV_EXCL_START
    UNREACHABLE("xrpl::STBase::add : not implemented");
    // LCOV_EXCL_STOP
}

/** Base-level value equivalence check, valid only for bare `STBase` objects.
 *
 *  This implementation asserts that `this` has type `STI_NOTPRESENT` — the
 *  only situation in which the base body is legitimately reached. All
 *  concrete subclasses override this method to perform value comparison; any
 *  subclass that omits the override and reaches this body has a bug.
 *
 *  @param t The other instance to compare against.
 *  @return `true` if @p t also has type `STI_NOTPRESENT`.
 */
bool
STBase::isEquivalent(STBase const& t) const
{
    XRPL_ASSERT(getSType() == STI_NOTPRESENT, "xrpl::STBase::isEquivalent : type not present");
    return t.getSType() == STI_NOTPRESENT;
}

bool
STBase::isDefault() const
{
    return true;
}

void
STBase::setFName(SField const& n)
{
    fName_ = &n;
    XRPL_ASSERT(fName_, "xrpl::STBase::setFName : field is set");
}

SField const&
STBase::getFName() const
{
    return *fName_;
}

void
STBase::addFieldID(Serializer& s) const
{
    XRPL_ASSERT(fName_->isBinary(), "xrpl::STBase::addFieldID : field is binary");
    s.addFieldID(fName_->fieldType, fName_->fieldValue);
}

//------------------------------------------------------------------------------

std::ostream&
operator<<(std::ostream& out, STBase const& t)
{
    return out << t.getFullText();
}

}  // namespace xrpl
