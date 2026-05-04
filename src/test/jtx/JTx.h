#pragma once

#include <test/jtx/basic_prop.h>
#include <test/jtx/requires.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <functional>
#include <memory>
#include <vector>

namespace xrpl::test::jtx {

class Env;

/** Execution context for applying a JSON transaction.
    This augments the transaction with various settings.
*/
struct JTx
{
    json::Value jv;
    requires_t require;
    std::optional<TER> ter = TER{tesSUCCESS};
    std::optional<std::pair<ErrorCodeI, std::string>> rpcCode = std::nullopt;
    std::optional<std::pair<std::string, std::optional<std::string>>> rpcException = std::nullopt;
    bool fillFee = true;
    bool fillSeq = true;
    bool fillSig = true;
    bool fillNetid = true;
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

    JTx(json::Value&& jv) : jv(std::move(jv))
    {
    }

    JTx(json::Value const& jv) : jv(jv)
    {
    }

    template <class Key>
    json::Value&
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
            if (auto test = dynamic_cast<PropType<Prop>*>(prop.get()))
                return &test->t;
        }
        return nullptr;
    }

    template <class Prop>
    [[nodiscard]] Prop const*
    get() const
    {
        for (auto& prop : props_.list)
        {
            if (auto test = dynamic_cast<PropType<Prop> const*>(prop.get()))
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
    set(std::unique_ptr<BasicProp> p)
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
        set(std::make_unique<PropType<Prop>>(std::forward<Args>(args)...));
    }
    /** @} */

private:
    struct PropList
    {
        PropList() = default;

        PropList(PropList const& other)
        {
            for (auto const& prop : other.list)
                list.emplace_back(prop->clone());
        }

        PropList&
        operator=(PropList const& other)
        {
            if (this != &other)
            {
                list.clear();
                for (auto const& prop : other.list)
                    list.emplace_back(prop->clone());
            }
            return *this;
        }

        PropList(PropList&& src) = default;
        PropList&
        operator=(PropList&& src) = default;

        std::vector<std::unique_ptr<BasicProp>> list;
    };

    PropList props_;
};

}  // namespace xrpl::test::jtx
