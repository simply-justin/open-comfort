// This build has no C++ exceptions. libstdc++'s nothrow operator new then calls the
// throwing one, whose failure path aborts (cxx_exception_stubs) instead of returning
// nullptr, so every `new (std::nothrow)` check in the firmware would panic on a full heap.
// These replacements return nullptr as the standard promises; operator delete is
// libstdc++'s, which frees with free().
#include <cstdlib>
#include <new>

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return std::malloc(size != 0 ? size : 1);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return std::malloc(size != 0 ? size : 1);
}
