//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/conditions/impl/error.h>
#include <system_error>
#include <string>
#include <type_traits>

namespace ripple {
namespace cryptoconditions {
namespace der {

/** Returns an error code.

    This function is used by the implementation to convert
    @ref error values into @ref error_code objects.
*/
static inline std::error_category const&
derCategory()
{
    using error_category = std::error_category;
    using error_code = std::error_code;
    using error_condition = std::error_condition;

    struct cat_t : public error_category
    {
        char const*
        name() const noexcept override
        {
            return "Der";
        }

        std::string
        message(int e) const override
        {
            switch (static_cast<Error>(e))
            {
                case Error::integerBounds:
                    return "integer bounds";

                case Error::longGroup:
                    return "long group";

                case Error::shortGroup:
                    return "short group";

                case Error::badDerEncoding:
                    return "bad der encoding";

                case Error::tagOverflow:
                    return "tag overflow";

                case Error::preambleMismatch:
                    return "preamble mismatch";

                case Error::contentLengthMismatch:
                    return "content length mismatch";

                case Error::unknownChoiceTag:
                    return "unknown choice tag";

                case Error::unsupported:
                    return "unsupported der feature";

                case Error::largeSize:
                    return "implementation limit exceeded: large payload.";

                case Error::preimageTooLong:
                    return "implementation limit exceeded: preimage is too long.";

                case Error::rsaModulusSizeRangeError:
                    return "rsa modulus size is out of range (129 and 512 bytes, inclusive)";

                case Error::unsupportedType:
                    return "Specification: Requested type not supported.";

                case Error::logicError:
                    return "a coding precondition or postcondition was "
                           "violated";

                default:
                    return "der error";
            }
        }

        std::error_condition
        default_error_condition(int ev) const noexcept override
        {
            return std::error_condition{ev, *this};
        }

        bool
        equivalent(int ev, error_condition const& ec) const noexcept override
        {
            return ec.value() == ev && &ec.category() == this;
        }

        bool
        equivalent(error_code const& ec, int ev) const noexcept override
        {
            return ec.value() == ev && &ec.category() == this;
        }
    };
    static cat_t const cat{};
    return cat;
}

std::error_code
make_error_code(Error e)
{
    return std::error_code{static_cast<int>(e), derCategory()};
}

}
}
}
