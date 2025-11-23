#ifndef CPP_MVS_FLEXIBLE_ARRAY_UNCHECKED_HPP
#define CPP_MVS_FLEXIBLE_ARRAY_UNCHECKED_HPP

#include "library.h"

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
struct FlexibleArrayUnchecked
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
    [[nodiscard]] constexpr explicit FlexibleArrayUnchecked(char* const owned_storage) noexcept : storage(owned_storage) {}

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
        -> FlexibleArrayUnchecked
    {
        auto* storage = static_cast<char*>(
            Detail::aligned_alloc(storage_size_for(capacity), std::max(alignof(Header), alignof(Element))));
        init_header(reinterpret_cast<Header*>(storage));
        return FlexibleArrayUnchecked{storage};
    }

    /// Constructs a buffer with enough space to hold the header and `capacity` number of Elements.
    ///
    /// The given `header` is moved into the storage.
    [[nodiscard]] static constexpr auto with_header(Int const capacity, Header&& header) noexcept -> FlexibleArrayUnchecked
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
        return reinterpret_cast<const_pointee_like<Self, Header*>>(self.storage);
    }

    /// Destroying the header unless the object is in a moved-from state.
    ~FlexibleArrayUnchecked()
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
    FlexibleArrayUnchecked(const FlexibleArrayUnchecked& other) = delete;
    FlexibleArrayUnchecked& operator=(const FlexibleArrayUnchecked& other) = delete;

    /// Move constructor
    FlexibleArrayUnchecked(FlexibleArrayUnchecked&& other) noexcept : storage(other.storage) { other.storage = nullptr; }
    /// Move assignment operator
    FlexibleArrayUnchecked& operator=(FlexibleArrayUnchecked&& other) noexcept
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
    friend void swap(FlexibleArrayUnchecked& a, FlexibleArrayUnchecked& b) noexcept { std::swap(a.storage, b.storage); }

    /// Projects a stack-allocated temporary
    template <std::invocable<FlexibleArrayUnchecked&> F>
    static constexpr auto project_temporary(Int element_count, F consumer) -> std::invoke_result_t<F, FlexibleArrayUnchecked&>
    {
        auto storage_size = FlexibleArrayUnchecked::storage_size_for(element_count);
        char* storage = aligned_alloca(storage_size, alignof(Header));

        FlexibleArrayUnchecked flexible_array{storage};
        auto result = consumer(flexible_array);

        std::destroy_at(reinterpret_cast<Header*>(flexible_array.leak_storage()));

        return result;
    }
};

#endif // CPP_MVS_FLEXIBLE_ARRAY_UNCHECKED_HPP