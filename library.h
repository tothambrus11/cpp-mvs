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

/// A buffer of header and elements stored in a contiguous region of memory, whose size is determined at
/// instance creation.
///
/// Note: After initialization, the header's destruction is managed by the FlexibleArray.
///
/// The FlexibleArray stores its elements out of line, so it is **movable** but **not copyable**.
///
/// Warning: The destructor of `FlexibleArray` does not destroy the elements that may
///   be stored in its payload. You must ensure that they are properly destroyed before destroying this object.
///   Similarly, the initialization of `FlexibleArray` doesn't start the lifetime of its elements, so users must
///   use placement new or std::construct_at to create the object.
template <TrailingElementCountProvider Header, typename Element>
struct FlexibleArray
{
private:
    /// Storage containing Header, potential padding, then `capacity` number of elements.
    ///
    /// May be null in case of a moved-from object.
    UnsafeMutableRawPointer storage;

    /// The offset of the start of the array from the start of the storage, given in bytes.
    [[nodiscard]] static constexpr auto elements_offset() noexcept -> Int
    {
        return align_up(sizeof(Header), alignof(Element));
    }

    /// The total space required for the storage of `element_count` elements, given in bytes.
    ///
    /// Guaranteed to be a multiple of `Header`'s alignment.
    [[nodiscard]] static constexpr auto storage_size_for(const Int element_count) noexcept -> size_t
    {
        return align_up(static_cast<size_t>(elements_offset() + (sizeof(Element) * element_count)), alignof(Header));
    }

    /// Constructs a flexible array by taking ownership of an existing storage.
    [[nodiscard]] constexpr explicit FlexibleArray(char* const owned_storage) noexcept : storage(owned_storage) {}

    /// Returns the address of the first array element.
    ///
    /// Note: There may be no element at the returned address when `capacity() == 0`.
    /// Requires the object being in a valid, non-moved-from state.
    template <typename Self>
    [[nodiscard]] constexpr auto elements_start(this Self&& self) -> const_pointee_like<Self, Element*>
    {
        return reinterpret_cast<const_pointee_like<Self, Element*>>(self.storage + elements_offset());
    }

public:
    /// Constructs a buffer with enough space to hold the header and `capacity` number of Elements.
    ///
    /// `init_header` must initialize the header by placement new/std::construct_at at the supplied memory address.
    [[nodiscard]] static constexpr auto with_header_initialized_by(Int const capacity,
                                                                   std::invocable<Header*> auto&& init_header) noexcept
        -> FlexibleArray
    {
        auto* storage = static_cast<char*>(
            Detail::aligned_alloc(storage_size_for(capacity), std::max(alignof(Header), alignof(Element))));
        init_header(reinterpret_cast<Header*>(storage));
        return FlexibleArray{storage};
    }

    /// Constructs a buffer with enough space to hold the header and `capacity` number of Elements.
    ///
    /// The given `header` is moved into the storage.
    [[nodiscard]] static constexpr auto with_header(Int const capacity, Header&& header) noexcept -> FlexibleArray
        requires(std::movable<Header>)
    {
        return with_header_initialized_by(capacity,
                                          [&](Header* place) { std::construct_at(place, std::move(header)); });
    }

    /// Returns the address for the place of the `i`th element in the array.
    ///
    /// Requires `i` < `capacity()`, and the object being in a valid, non-moved-from state.
    template <typename Self>
    [[nodiscard]] constexpr auto element_address(this Self&& self, const Int i) noexcept
        -> const_pointee_like<Self, Element*>
    {
        return self.elements_start() + i;
    }

    /// Returns the pointer to the header.
    ///
    /// Requires the FlexibleArray being in a valid, non-moved-from state.
    template <typename Self>
    [[nodiscard]] constexpr auto header(this Self&& self) noexcept -> const_pointee_like<Self, Header*>
    {
        precondition(self.storage != nullptr, "FlexibleArray storage must not be null.");
        return reinterpret_cast<const_pointee_like<Self, Header*>>(self.storage);
    }

    /// The number of elements the storage has allocated space for.
    ///
    /// Requires the object to be in a valid, non-moved-from state.
    [[nodiscard]] constexpr auto capacity() const noexcept -> Int { return header()->trailing_element_count(); }

    /// Destroying the header unless the object is in a moved-from state.
    ~FlexibleArray()
    {
        if (storage != nullptr)
        {
            std::destroy_at(header());
            Detail::aligned_free(storage);
        }
    }

    /// Extracts the storage out of the trailing array, handing out the ownership to the callee.
    ///
    /// The underlying storage won't be freed by this FlexibleArray.
    [[nodiscard]] constexpr auto leak_storage() -> UnsafeMutableRawPointer { return std::exchange(storage, nullptr); }

    // Not copyable
    FlexibleArray(const FlexibleArray& other) = delete;
    FlexibleArray& operator=(const FlexibleArray& other) = delete;

    /// Move constructor
    FlexibleArray(FlexibleArray&& other) noexcept : storage(other.storage) { other.storage = nullptr; }
    /// Move assignment operator
    FlexibleArray& operator=(FlexibleArray&& other) noexcept
    {
        // Moving to self is a no-op
        if (this == &other)
        {
            return *this;
        }
        // Destroying the header unless the object was in a moved-from state.
        if (storage != nullptr)
        {
            std::destroy_at(header());
            Detail::aligned_free(storage);
        }
        // Taking ownership of the other object's storage, marking the other object as moved-from.
        storage = other.storage;
        other.storage = nullptr;
        return *this;
    }

    /// Swaps the underlying storage of `a` and `b`.
    friend void swap(FlexibleArray& a, FlexibleArray& b) noexcept { std::swap(a.storage, b.storage); }

    /// Projects a stack-allocated temporary
    template <std::invocable<FlexibleArray&> F>
    static constexpr auto project_temporary(Int element_count, F consumer) -> std::invoke_result_t<F, FlexibleArray&>
    {
        auto storage_size = FlexibleArray::storage_size_for(element_count);
        char* storage = aligned_alloca(storage_size, alignof(Header));

        FlexibleArray flexible_array{storage};
        auto result = consumer(flexible_array);

        std::destroy_at(reinterpret_cast<Header*>(flexible_array.leak_storage()));

        return result;
    }
};


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
