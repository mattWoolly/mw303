#pragma once
// The allocation spy's switches (implemented in RealtimeMain.cpp).
#include <atomic>
#include <cstddef>

namespace allocspy
{
/** Set on the thread that plays the audio thread while it is inside processBlock. */
extern thread_local bool inAudioContext;

/** Allocations and frees seen while inAudioContext was set on the calling thread. */
extern std::atomic<std::size_t> violations;

struct ScopedAudioContext
{
    ScopedAudioContext() noexcept { inAudioContext = true; }
    ~ScopedAudioContext() noexcept { inAudioContext = false; }
};
}
