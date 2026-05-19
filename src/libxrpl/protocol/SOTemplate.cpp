#include <xrpl/protocol/SOTemplate.h>

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/SField.h>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace xrpl {

SOTemplate::SOTemplate(
    std::initializer_list<SOElement> uniqueFields,
    std::initializer_list<SOElement> commonFields)
    : SOTemplate(std::vector(uniqueFields), std::vector(commonFields))
{
}

SOTemplate::SOTemplate(std::vector<SOElement> uniqueFields, std::vector<SOElement> commonFields)
    : indices_(SField::getNumFields() + 1, -1)  // Unmapped indices == -1
{
    // Add all SOElements.
    //
    elements_ = std::move(uniqueFields);
    std::ranges::move(commonFields, std::back_inserter(elements_));

    // Validate and index elements_.
    //
    for (std::size_t i = 0; i < elements_.size(); ++i)
    {
        SField const& sField{elements_[i].sField()};

        // Make sure the field's index is in range
        //
        if (sField.getNum() <= 0 || sField.getNum() >= indices_.size())
            Throw<std::runtime_error>("Invalid field index for SOTemplate.");

        // Make sure that this field hasn't already been assigned
        //
        if (getIndex(sField) != -1)
            Throw<std::runtime_error>("Duplicate field index for SOTemplate.");

        // Add the field to the index mapping table
        //
        indices_[sField.getNum()] = i;
    }
}

int
SOTemplate::getIndex(SField const& sField) const
{
    // The mapping table should be large enough for any possible field
    //
    if (sField.getNum() <= 0 || sField.getNum() >= indices_.size())
        Throw<std::runtime_error>("Invalid field index for getIndex().");

    return indices_[sField.getNum()];
}

}  // namespace xrpl
