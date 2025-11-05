#include <cassert>
#include <coroutine>
#include <optional>
#include <stdexcept>

#include <lsm/cosm.hpp>

namespace
{
lsm::co::Task<int> make_task(int value)
{
    co_return value;
}

lsm::co::Task<int> make_fail()
{
    throw std::runtime_error("boom");
    co_return 0;
}

lsm::co::Task<void> make_void_task()
{
    co_return;
}

template <class TaskT>
void run(TaskT& task)
{
    if(!task.await_ready())
    {
        task.await_suspend(std::noop_coroutine());
    }
    if constexpr(!std::is_same_v<TaskT, lsm::co::Task<void>>)
    {
        (void)task.await_resume();
    }
    else
    {
        task.await_resume();
    }
}

} // namespace

int main()
{
    {
        auto source = make_task(1);
        lsm::co::Task<int> moved(std::move(source));
        auto replacement = make_task(2);
        moved = std::move(replacement);
        run(moved);
    }

    {
        auto void_source = make_void_task();
        lsm::co::Task<void> void_moved(std::move(void_source));
        auto void_replacement = make_void_task();
        void_moved = std::move(void_replacement);
        run(void_moved);
    }

    {
        using FinalInt = lsm::co::Task<int>::promise_type::final_awaiter;
        auto mem_int = &FinalInt::await_resume;
        (FinalInt{}.*mem_int)();

        using FinalVoid = lsm::co::Task<void>::promise_type::final_awaiter;
        auto mem_void = &FinalVoid::await_resume;
        (FinalVoid{}.*mem_void)();
    }

    {
        auto failing = make_fail();
        bool threw = false;
        try
        {
            run(failing);
        }
        catch(const std::runtime_error&)
        {
            threw = true;
        }
        assert(threw);
    }

    return 0;
}
