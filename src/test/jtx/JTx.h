#ifndef XRPL_TEST_JTX_JTX_H_INCLUDED
#define XRPL_TEST_JTX_JTX_H_INCLUDED

#include <test/jtx/basic_prop.h>
#include <test/jtx/requires.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <functional>
#include <memory>
#include <vector>

namespace ripple {
namespace test {
namespace jtx {

class Env;

/** Execution context for applying a JSON transaction.
    This augments the transaction with various settings.
*/
struct JTx
{
    Json::Value jv;
    requires_t require;
    std::optional<TER> ter = TER{tesSUCCESS};
    std::optional<std::pair<error_code_i, std::string>> rpcCode = std::nullopt;
    std::optional<std::pair<std::string, std::optional<std::string>>>
        rpcException = std::nullopt;
    bool fill_fee = true;
    bool fill_seq = true;
    bool fill_sig = true;
    bool fill_netid = true;
    std::shared_ptr<STTx const> stx;
    // Functions that sign the transaction from the Account
    std::vector<std::function<void(Env&, JTx&)>> mainSigners;
    // Functions that sign something else after the mainSigners, such as
    // sfCounterpartySignature
    std::vector<std::function<void(Env&, JTx&)>> postSigners;

    JTx() = default;
    JTx(JTx const&) = default;
    JTx&
    operator=(JTx const&) = default;
    JTx(JTx&&) = default;
    JTx&
    operator=(JTx&&) = default;

    JTx(Json::Value&& jv_) : jv(std::move(jv_))
    {
    }

    JTx(Json::Value const& jv_) : jv(jv_)
    {
    }

    template <class Key>
    Json::Value&
    operator[](Key const& key)
    {
        return jv[key];
    }

    /** Return a property if it exists

        @return nullptr if the Prop does not exist
    */
    /** @{ */
    template <class Prop>
    Prop*
    get()
    {
        for (auto& prop : props_.list)
        {
            if (auto test = dynamic_cast<prop_type<Prop>*>(prop.get()))
                return &test->t;
        }
        return nullptr;
    }

    template <class Prop>
    Prop const*
    get() const
    {
        for (auto& prop : props_.list)
        {
            if (auto test = dynamic_cast<prop_type<Prop> const*>(prop.get()))
                return &test->t;
        }
        return nullptr;
    }
    /** @} */

    /** Set a property
        If the property already exists,
        it is replaced.
    */
    /** @{ */
    void
    set(std::unique_ptr<basic_prop> p)
    {
        for (auto& prop : props_.list)
        {
            if (prop->assignable(p.get()))
            {
                prop = std::move(p);
                return;
            }
        }
        props_.list.emplace_back(std::move(p));
    }

    template <class Prop, class... Args>
    void
    set(Args&&... args)
    {
        set(std::make_unique<prop_type<Prop>>(std::forward<Args>(args)...));
    }
    /** @} */

private:
    struct prop_list
    {
        prop_list() = default;

        prop_list(prop_list const& other)
        {
            for (auto const& prop : other.list)
                list.emplace_back(prop->clone());
        }

        prop_list&
        operator=(prop_list const& other)
        {
            if (this != &other)
            {
                list.clear();
                for (auto const& prop : other.list)
                    list.emplace_back(prop->clone());
            }
            return *this;
        }

        prop_list(prop_list&& src) = default;
        prop_list&
        operator=(prop_list&& src) = default;

        std::vector<std::unique_ptr<basic_prop>> list;
    };

    prop_list props_;
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
