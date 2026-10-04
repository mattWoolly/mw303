// The allocation spy (docs/research/07 section 4.5, the mw140 pattern). The linker wraps the C
// allocator: every malloc, calloc, realloc, free, posix_memalign, aligned_alloc and memalign
// made by code linked into this executable goes through the __wrap_ functions below, which
// count the call when the calling thread is inside an "audio context" (AllocSpy.h). Global
// operator new and delete call malloc and free, so C++ allocations are counted once, through
// the wrap, never twice. libstdc++ is linked statically so its own calls are wrapped too.
#include "AllocSpy.h"

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <catch2/catch_session.hpp>

#include <atomic>
#include <cstdlib>
#include <new>

namespace allocspy
{
thread_local bool inAudioContext = false;
std::atomic<std::size_t> violations { 0 };

void count() noexcept
{
    if (inAudioContext)
        violations.fetch_add (1, std::memory_order_relaxed);
}
}

extern "C"
{
    void* __real_malloc (size_t);
    void __real_free (void*);
    void* __real_calloc (size_t, size_t);
    void* __real_realloc (void*, size_t);
    int __real_posix_memalign (void**, size_t, size_t);
    void* __real_aligned_alloc (size_t, size_t);
    void* __real_memalign (size_t, size_t);

    void* __wrap_malloc (size_t n) { allocspy::count(); return __real_malloc (n); }
    void __wrap_free (void* p) { if (p != nullptr) allocspy::count(); __real_free (p); }
    void* __wrap_calloc (size_t n, size_t s) { allocspy::count(); return __real_calloc (n, s); }
    void* __wrap_realloc (void* p, size_t n) { allocspy::count(); return __real_realloc (p, n); }
    int __wrap_posix_memalign (void** out, size_t a, size_t n) { allocspy::count(); return __real_posix_memalign (out, a, n); }
    void* __wrap_aligned_alloc (size_t a, size_t n) { allocspy::count(); return __real_aligned_alloc (a, n); }
    void* __wrap_memalign (size_t a, size_t n) { allocspy::count(); return __real_memalign (a, n); }
}

void* operator new (std::size_t n)
{
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n)
{
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new (std::size_t n, const std::nothrow_t&) noexcept { return std::malloc (n == 0 ? 1 : n); }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept { return std::malloc (n == 0 ? 1 : n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    return Catch::Session().run (argc, argv);
}
