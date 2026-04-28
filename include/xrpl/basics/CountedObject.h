#pragma once

#include <xrpl/beast/type_name.h>

#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

/** Manages all counted object types. */
class CountedObjects
{
public:
    static CountedObjects&
    getInstance() noexcept;

    using Entry = std::pair<std::string, int>;
    using List = std::vector<Entry>;

    [[nodiscard]] List
    getCounts(int minimumThreshold) const;

public:
    /** Implementation for @ref CountedObject.

        @internal
    */
    class Counter
    {
    public:
        Counter(std::string name) noexcept : name_(std::move(name)), count_(0)
        {
            // Insert ourselves at the front of the lock-free linked list
            CountedObjects& instance = CountedObjects::getInstance();
            Counter* head = nullptr;

            do
            {
                head = instance.m_head.load();
                next_ = head;
            } while (instance.m_head.exchange(this) != head);

            ++instance.m_count;
        }

        ~Counter() noexcept = default;

        int
        increment() noexcept
        {
            return ++count_;
        }

        int
        decrement() noexcept
        {
            return --count_;
        }

        [[nodiscard]] int
        getCount() const noexcept
        {
            return count_.load();
        }

        [[nodiscard]] Counter*
        getNext() const noexcept
        {
            return next_;
        }

        [[nodiscard]] std::string const&
        getName() const noexcept
        {
            return name_;
        }

    private:
        std::string const name_;
        std::atomic<int> count_;
        Counter* next_;
    };

private:
    CountedObjects() noexcept;
    ~CountedObjects() noexcept = default;

private:
    std::atomic<int> m_count;
    std::atomic<Counter*> m_head;
};

//------------------------------------------------------------------------------

/** Tracks the number of instances of an object.

    Derived classes have their instances counted automatically. This is used
    for reporting purposes.

    @ingroup basics
*/
template <class Object>
class CountedObject
{
private:
    static auto&
    getCounter() noexcept
    {
        static CountedObjects::Counter c{beast::type_name<Object>()};
        return c;
    }

    CountedObject() noexcept
    {
        getCounter().increment();
    }

    CountedObject(CountedObject const&) noexcept
    {
        getCounter().increment();
    }

    CountedObject&
    operator=(CountedObject const&) noexcept = default;

public:
    ~CountedObject() noexcept
    {
        getCounter().decrement();
    }

    friend Object;
};

}  // namespace xrpl
