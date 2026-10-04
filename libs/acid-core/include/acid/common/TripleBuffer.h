#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace acid
{
/** Wait-free hand-off of the latest value of a large trivially copyable object from one
    writer thread to one reader thread. The writer never blocks the reader and the reader
    never blocks the writer; the reader always sees a complete value, and intermediate
    values may be skipped.

    Used to publish the pattern bank and settings from the message thread to the audio
    thread: the writer pays for the copy, the reader only swaps an index.

    The object is large (three copies of T), so allocate it on the heap. */
template <typename T>
class TripleBuffer
{
    static_assert (std::is_trivially_copyable_v<T>);

public:
    explicit TripleBuffer (const T& initial = T {}) noexcept
    {
        for (auto& slot : slots)
            slot = initial;
    }

    /** Writer thread only. Copies the value in and makes it the latest. */
    void write (const T& value) noexcept
    {
        slots[backIndex] = value;
        const auto previous = shared.exchange (static_cast<std::uint8_t> (backIndex | kDirty),
                                               std::memory_order_acq_rel);
        backIndex = previous & kIndexMask;
    }

    /** Reader thread only. Takes the latest value if there is a new one.
        Returns true when read() now refers to a value not seen before. */
    bool update() noexcept
    {
        if ((shared.load (std::memory_order_relaxed) & kDirty) == 0)
            return false;

        const auto previous = shared.exchange (static_cast<std::uint8_t> (frontIndex),
                                               std::memory_order_acq_rel);
        frontIndex = previous & kIndexMask;
        return true;
    }

    /** Reader thread only. The value taken by the last successful update(). */
    const T& read() const noexcept { return slots[frontIndex]; }

private:
    static constexpr std::uint8_t kDirty = 0x80;
    static constexpr std::uint8_t kIndexMask = 0x03;

    std::array<T, 3> slots;
    std::uint8_t backIndex = 0;                  // writer-owned
    std::atomic<std::uint8_t> shared { 1 };      // index of the middle slot, plus the dirty bit
    std::uint8_t frontIndex = 2;                 // reader-owned
};
}
