#pragma once
// Real-time safety check for tests. While a ScopedAllocGuard is alive on a thread,
// any operator new/delete on that thread is counted; a test then requires zero.
//
//     acid::test::ScopedAllocGuard guard;
//     engine.process (...);
//     REQUIRE (guard.count() == 0);
#include <cstddef>

namespace acid::test
{
class ScopedAllocGuard
{
public:
    ScopedAllocGuard() noexcept;
    ~ScopedAllocGuard() noexcept;
    ScopedAllocGuard (const ScopedAllocGuard&) = delete;
    ScopedAllocGuard& operator= (const ScopedAllocGuard&) = delete;

    /** Allocations plus deallocations seen on this thread since construction. */
    std::size_t count() const noexcept;
};
}
