// QA host-simulation harness entry point (review_qa_HostSim.cpp holds the harness).
//
// With QA_ALLOCSPY (the Release QA tree only: --wrap does not mix with the sanitizers) the
// linker wraps the C allocator and the blocking primitives. Any call made while the calling
// thread is inside an "audio context" is counted, and the first distinct call stacks are kept
// so the report can name the code that allocated, locked or slept on the audio thread.
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <execinfo.h>

namespace qaspy
{
// 0 = not audio. 1 = inside processBlock / processBlockBypassed. 2 = wrapper-style parameter
// delivery on the audio thread (setValue + sendValueChangedMessageToListeners).
thread_local int audioContext = 0;
thread_local bool inSpy = false;

enum Kind { kAlloc, kFree, kLock, kTryLock, kWait, kSleep, kFile, kNumKinds };
const char* kindName (int k)
{
    static const char* n[] = { "alloc", "free", "mutex_lock", "mutex_trylock", "cond/sem wait", "sleep", "file open/write" };
    return n[k];
}
std::atomic<std::size_t> counts[3][kNumKinds] {};

constexpr int kMaxStacks = 24;
constexpr int kDepth = 24;
struct Stack { int ctx; int kind; int depth; void* frames[kDepth]; std::atomic<int> hits; };
Stack stacks[kMaxStacks];
std::atomic<int> numStacks { 0 };

void record (int kind) noexcept
{
    const int ctx = audioContext;
    if (ctx == 0 || inSpy)
        return;
    inSpy = true;
    counts[ctx][kind].fetch_add (1, std::memory_order_relaxed);
    void* frames[kDepth];
    const int depth = ::backtrace (frames, kDepth);
    // dedupe on frames 2..10
    const int n = numStacks.load();
    bool found = false;
    for (int i = 0; i < n; ++i)
    {
        Stack& s = stacks[i];
        if (s.ctx != ctx || s.kind != kind || s.depth != depth) continue;
        bool same = true;
        for (int f = 2; f < std::min (depth, 10); ++f)
            if (s.frames[f] != frames[f]) { same = false; break; }
        if (same) { s.hits.fetch_add (1); found = true; break; }
    }
    if (! found)
    {
        const int slot = numStacks.fetch_add (1);
        if (slot < kMaxStacks)
        {
            Stack& s = stacks[slot];
            s.ctx = ctx; s.kind = kind; s.depth = depth;
            std::memcpy (s.frames, frames, sizeof (frames));
            s.hits.store (1);
        }
        else
        {
            numStacks.store (kMaxStacks);
        }
    }
    inSpy = false;
}

void report()
{
    std::fprintf (stderr, "\n==== audio-thread spy ====\n");
    for (int ctx = 1; ctx <= 2; ++ctx)
        for (int k = 0; k < kNumKinds; ++k)
            if (auto c = counts[ctx][k].load())
                std::fprintf (stderr, "  ctx=%s %-16s %zu\n", ctx == 1 ? "processBlock" : "wrapperParam", kindName (k), c);
    const int n = std::min (numStacks.load(), kMaxStacks);
    for (int i = 0; i < n; ++i)
    {
        std::fprintf (stderr, "-- stack %d: ctx=%d kind=%s hits=%d\n", i, stacks[i].ctx, kindName (stacks[i].kind), stacks[i].hits.load());
        ::backtrace_symbols_fd (stacks[i].frames, stacks[i].depth, 2);
    }
}

std::size_t total (int ctx)
{
    std::size_t t = 0;
    for (int k = 0; k < kNumKinds; ++k) if (k != kTryLock) t += counts[ctx][k].load();
    return t;
}
}

#if QA_ALLOCSPY
extern "C"
{
    void* __real_malloc (size_t);
    void __real_free (void*);
    void* __real_calloc (size_t, size_t);
    void* __real_realloc (void*, size_t);
    int __real_posix_memalign (void**, size_t, size_t);
    void* __real_aligned_alloc (size_t, size_t);
    void* __real_memalign (size_t, size_t);

    void* __wrap_malloc (size_t n) { qaspy::record (qaspy::kAlloc); return __real_malloc (n); }
    void __wrap_free (void* p) { if (p != nullptr) qaspy::record (qaspy::kFree); __real_free (p); }
    void* __wrap_calloc (size_t n, size_t s) { qaspy::record (qaspy::kAlloc); return __real_calloc (n, s); }
    void* __wrap_realloc (void* p, size_t n) { qaspy::record (qaspy::kAlloc); return __real_realloc (p, n); }
    int __wrap_posix_memalign (void** out, size_t a, size_t n) { qaspy::record (qaspy::kAlloc); return __real_posix_memalign (out, a, n); }
    void* __wrap_aligned_alloc (size_t a, size_t n) { qaspy::record (qaspy::kAlloc); return __real_aligned_alloc (a, n); }
    void* __wrap_memalign (size_t a, size_t n) { qaspy::record (qaspy::kAlloc); return __real_memalign (a, n); }

    int __real_pthread_mutex_lock (pthread_mutex_t*);
    int __real_pthread_mutex_trylock (pthread_mutex_t*);
    int __real_pthread_cond_wait (pthread_cond_t*, pthread_mutex_t*);
    int __real_pthread_cond_timedwait (pthread_cond_t*, pthread_mutex_t*, const struct timespec*);
    int __real_sem_wait (sem_t*);
    int __real_nanosleep (const struct timespec*, struct timespec*);
    int __real_usleep (useconds_t);
    int __real_open (const char*, int, ...);
    FILE* __real_fopen (const char*, const char*);
    ssize_t __real_write (int, const void*, size_t);

    int __wrap_pthread_mutex_lock (pthread_mutex_t* m) { qaspy::record (qaspy::kLock); return __real_pthread_mutex_lock (m); }
    int __wrap_pthread_mutex_trylock (pthread_mutex_t* m) { qaspy::record (qaspy::kTryLock); return __real_pthread_mutex_trylock (m); }
    int __wrap_pthread_cond_wait (pthread_cond_t* c, pthread_mutex_t* m) { qaspy::record (qaspy::kWait); return __real_pthread_cond_wait (c, m); }
    int __wrap_pthread_cond_timedwait (pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* t) { qaspy::record (qaspy::kWait); return __real_pthread_cond_timedwait (c, m, t); }
    int __wrap_sem_wait (sem_t* s) { qaspy::record (qaspy::kWait); return __real_sem_wait (s); }
    int __wrap_nanosleep (const struct timespec* a, struct timespec* b) { qaspy::record (qaspy::kSleep); return __real_nanosleep (a, b); }
    int __wrap_usleep (useconds_t u) { qaspy::record (qaspy::kSleep); return __real_usleep (u); }
    int __wrap_open (const char* p, int f, ...)
    {
        qaspy::record (qaspy::kFile);
        va_list ap; va_start (ap, f); const int mode = (f & O_CREAT) ? va_arg (ap, int) : 0; va_end (ap);
        return __real_open (p, f, mode);
    }
    FILE* __wrap_fopen (const char* p, const char* m) { qaspy::record (qaspy::kFile); return __real_fopen (p, m); }
    ssize_t __wrap_write (int fd, const void* b, size_t n) { qaspy::record (qaspy::kFile); return __real_write (fd, b, n); }
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
#endif

int qaRunHostSim (int argc, char** argv);

#include <csignal>
static void qaCrashHandler (int sig)
{
    void* frames[64];
    const int n = ::backtrace (frames, 64);
    const char msg[] = "\n==== QA crash backtrace (signal) ====\n";
    (void) ::write (2, msg, sizeof (msg) - 1);
    ::backtrace_symbols_fd (frames, n, 2);
    std::signal (sig, SIG_DFL);
    std::raise (sig);
}

int main (int argc, char** argv)
{
    int rc = 0;
    if (std::getenv ("QA_NO_CRASH_HANDLER") == nullptr)
    {
        std::signal (SIGABRT, qaCrashHandler);
        std::signal (SIGSEGV, qaCrashHandler);
    }
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        rc = qaRunHostSim (argc, argv);
    }
    qaspy::report();
    return rc;
}
