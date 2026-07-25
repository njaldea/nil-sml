#include <nil/sm.hpp>

#include <cstdio>
#include <iostream>

struct e1
{
};

struct e2
{
};

struct e3
{
};

struct sub2;

struct sub1 // NOLINT
{
    using events = nil::xalt::tlist<e1>;

    static auto on_event(const e1& /* ev */)
    {
        std::cout << "sub1 - e1" << std::endl;
        return nil::sm::Transit<sub2>();
    }
};

struct sub2 // NOLINT
{
    using events = nil::xalt::tlist<e1>;

    static auto on_event(const e1& /* ev */)
    {
        std::cout << "sub2 - e1" << std::endl;
        return nil::sm::Forward{};
    }
};

struct composite // NOLINT
{
    using regions = nil::xalt::tlist<sub1>;

    using events = nil::xalt::tlist<e1>;

    static auto on_event(const e1& /* ev */)
    {
        std::cout << "composite - e1" << std::endl;
        return nil::sm::Discard{};
    }

    static auto on_event(const e2& /* ev */) -> std::variant<nil::sm::Discard>
    {
        std::cout << "composite - e2" << std::endl;
        return nil::sm::Discard{};
    }
};

int main()
{
    nil::sm::DefaultSM<composite> ss{{}, {}};

    {
        e1 e;
        ss.post(e);
    }
    {
        e1 e;
        ss.post(e);
    }
    {
        e2 e;
        ss.post(e);
    }
}
