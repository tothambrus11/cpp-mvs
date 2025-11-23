#ifndef CPP_MVS_LIBRARY_H
#define CPP_MVS_LIBRARY_H

#include <concepts>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <print>
#include <string_view>
#include <type_traits>
#include <utility>

using Int = long long;

namespace Detail
{
    inline void* aligned_alloc(size_t size, size_t align)
    {
#ifdef _MSC_VER
        return _aligned_malloc(size, align);
#else
        return std::aligned_alloc(align, size);
#endif
    }

    inline void aligned_free(void* block)
    {
#ifdef _MSC_VER
        _aligned_free(block);
#else
        std::free(block);
#endif
    }
} // namespace Detail
/// Applies const to `TargetType` if `Base` is const.
template <typename Base, typename TargetType>
using const_like = std::conditional_t<std::is_const_v<std::remove_reference_t<Base>>, TargetType const, TargetType>;

/// Applies const to the type POINTED TO by TargetType.
/// Example: T*  =>  T* or T const*  depending on whether Base is const
template <typename Base, typename TargetType>
using const_pointee_like = std::conditional_t<std::is_const_v<std::remove_reference_t<Base>>,
                                              // If Base is const: remove pointer (*), make const, add pointer back (*)
                                              std::add_pointer_t<std::add_const_t<std::remove_pointer_t<TargetType>>>,
                                              // If Base is not const: keep original
                                              TargetType>;

constexpr void precondition(const bool p, const std::string_view message = "Precondition failure.")
{
    if (!p)
    {
        std::println("Assertion failure: {}\n", message);
        std::quick_exit(EXIT_FAILURE);
    }
}

/// Rounds up 'n' to the next multiple of 'align', assuming `n` and `align` are non-negative integer powers of 2.
template <std::unsigned_integral T>
constexpr auto align_up(const T n, T const align) -> T
{
    return (n + align - 1) & ~(align - 1);
}

/// Allocates at least `size` bytes on the stack, ensuring alignment.
#define aligned_alloca(size, alignment)                                                                                \
    reinterpret_cast<char*>(                                                                                           \
        ::align_up(reinterpret_cast<uintptr_t>(alloca((size) + (alignment))), static_cast<uintptr_t>(alignment)))

template <typename T>
concept TrailingElementCountProvider = requires(T&& a) {
    /// fun trailing_element_count() -> Int
    { a.trailing_element_count() } -> std::same_as<Int>;
};

using UnsafeMutableRawPointer = char*;


//
// void test_f()
// {
//     for (Int i = 0; i <= 64; i += 8)
//     {
//         size_t x = static_cast<size_t>(i);
//         size_t header_stride = 16;
//         size_t element_align = 32;
//
//         auto offset_with_known_initial_offset = align_up(x + header_stride, element_align) - x;
//         auto offset_without_known_initial_offset = align_up(header_stride, element_align);
//         std::cout << x << " | " << offset_without_known_initial_offset << "  " << offset_with_known_initial_offset
//                   << std::endl;
//     }
// }

/// A dynamically sized buffer with bounds checked element access.
template <typename Element> //
class UnsafeBufferPointer
{
    Element* start_;
    Int count_;

public:
    explicit constexpr UnsafeBufferPointer(Element* start, const Int count) : start_(start), count_(count) {}

    template <typename Self>
    constexpr auto&& operator[](this Self&& self, Int index) noexcept
    {
        precondition(index >= 0 && index < self.count_);
        return std::forward_like<Self>(*(self.start_ + index));
    }

    [[nodiscard]] Int count() const noexcept { return count_; }
};
#endif // CPP_MVS_LIBRARY_H
