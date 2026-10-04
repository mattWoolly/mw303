#include "AllocGuard.h"
#include <cstdlib>
#include <new>

namespace
{
thread_local bool armed = false;
thread_local std::size_t events = 0;

void* allocate (std::size_t size)
{
    if (armed) ++events;
    if (void* p = std::malloc (size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}

void* allocateAligned (std::size_t size, std::align_val_t al)
{
    if (armed) ++events;
    const auto a = static_cast<std::size_t> (al);
    const auto rounded = (size + a - 1) / a * a;
    if (void* p = std::aligned_alloc (a, rounded == 0 ? a : rounded)) return p;
    throw std::bad_alloc();
}

void release (void* p) noexcept
{
    if (armed && p != nullptr) ++events;
    std::free (p);
}
}

static void* allocateNoThrow (std::size_t size) noexcept
{
    try { return allocate (size); } catch (...) { return nullptr; }
}

static void* allocateAlignedNoThrow (std::size_t size, std::align_val_t al) noexcept
{
    try { return allocateAligned (size, al); } catch (...) { return nullptr; }
}

void* operator new (std::size_t n) { return allocate (n); }
void* operator new[] (std::size_t n) { return allocate (n); }
void* operator new (std::size_t n, std::align_val_t a) { return allocateAligned (n, a); }
void* operator new[] (std::size_t n, std::align_val_t a) { return allocateAligned (n, a); }
void* operator new (std::size_t n, const std::nothrow_t&) noexcept { return allocateNoThrow (n); }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept { return allocateNoThrow (n); }
void* operator new (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return allocateAlignedNoThrow (n, a); }
void* operator new[] (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return allocateAlignedNoThrow (n, a); }
void operator delete (void* p, const std::nothrow_t&) noexcept { release (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { release (p); }
void operator delete (void* p, std::align_val_t, const std::nothrow_t&) noexcept { release (p); }
void operator delete[] (void* p, std::align_val_t, const std::nothrow_t&) noexcept { release (p); }
void operator delete (void* p) noexcept { release (p); }
void operator delete[] (void* p) noexcept { release (p); }
void operator delete (void* p, std::size_t) noexcept { release (p); }
void operator delete[] (void* p, std::size_t) noexcept { release (p); }
void operator delete (void* p, std::align_val_t) noexcept { release (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { release (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { release (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { release (p); }

namespace acid::test
{
ScopedAllocGuard::ScopedAllocGuard() noexcept { events = 0; armed = true; }
ScopedAllocGuard::~ScopedAllocGuard() noexcept { armed = false; }
std::size_t ScopedAllocGuard::count() const noexcept { return events; }
}
