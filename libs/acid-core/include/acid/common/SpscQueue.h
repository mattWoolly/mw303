#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace acid
{
/** Wait-free single-producer/single-consumer FIFO of trivially copyable items.
    One thread calls push, one other thread calls pop; neither allocates or blocks.
    Capacity must be a power of two; one slot is kept free. */
template <typename T, std::size_t Capacity>
class SpscQueue
{
    static_assert (std::is_trivially_copyable_v<T>);
    static_assert (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    /** Producer side. Returns false (item dropped) when full. */
    bool push (const T& item) noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto next = (w + 1) & (Capacity - 1);
        if (next == readIndex.load (std::memory_order_acquire)) return false;
        buffer[w] = item;
        writeIndex.store (next, std::memory_order_release);
        return true;
    }

    /** Consumer side. Returns false when empty. */
    bool pop (T& out) noexcept
    {
        const auto r = readIndex.load (std::memory_order_relaxed);
        if (r == writeIndex.load (std::memory_order_acquire)) return false;
        out = buffer[r];
        readIndex.store ((r + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

    /** Consumer side: discard everything queued. */
    void clear() noexcept
    {
        readIndex.store (writeIndex.load (std::memory_order_acquire), std::memory_order_release);
    }

private:
    std::array<T, Capacity> buffer {};
    alignas (64) std::atomic<std::size_t> writeIndex { 0 };
    alignas (64) std::atomic<std::size_t> readIndex { 0 };
};
}
