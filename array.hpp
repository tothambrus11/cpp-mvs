#include <concepts>
#include "flexible_array_checked.hpp"
#include "library.h"


template <typename Element>
    requires std::movable<Element> && std::destructible<Element>
class Array
{
    struct Header
    {
        Int count;
        Int capacity;

        [[nodiscard]] explicit Header(const Int count, const Int capacity) noexcept : count(count), capacity(capacity)
        {
        }

        /// Returns the number of elements of the storage (capacity)
        ///
        /// Satisfies TrailingElementCountProvider concept.
        [[nodiscard]] Int trailing_element_count() const { return capacity; }
    };
    static_assert(TrailingElementCountProvider<Header>);

    /// The underlying storage for the array.
    ///
    /// May be invalid while the capacity is zero.
    FlexibleArrayChecked<Header, Element> storage;

    /// Constructs the Array with given storage.
    [[nodiscard]] explicit Array(FlexibleArrayChecked<Header, Element>&& storage) noexcept : storage(std::move(storage))
    {
    }

public:
    /// Create an empty array with no heap allocation and zero capacity.
    [[nodiscard]] static auto create_empty() noexcept -> Array
    {
        return Array{FlexibleArrayChecked<Header, Element>::create_empty()};
    }

    /// Creates an array with given capacity, heap-allocating storage unless capacity is zero.
    [[nodiscard]] static auto create_empty(const Int capacity) noexcept -> Array
    {
        precondition(capacity >= 0);
        if (capacity == 0)
        {
            return Array::create_empty();
        }
        return Array{FlexibleArrayChecked<Header, Element>::with_header(capacity, Header{0, capacity})};
    }

    /// The number of initialized elements in the array.
    [[nodiscard]] constexpr auto count() const noexcept -> Int
    {
        return storage.is_valid() ? storage.header()->count : 0;
    }

    /// The number of elements the array currently has allocated space for.
    [[nodiscard]] constexpr auto capacity() const noexcept -> Int
    {
        return storage.is_valid() ? storage.capacity() : 0;
    }

    
};
