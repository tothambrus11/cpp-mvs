#ifndef CPP_MVS_FLEXIBLE_ARRAY_CHECKED_HPP
#define CPP_MVS_FLEXIBLE_ARRAY_CHECKED_HPP

#include "flexible_array_unchecked.hpp"

/// A wrapper around FlexibleArrayUnchecked that provides bounds-checked access to elements.
///
/// This class retrieves the capacity from the header and performs precondition checks to ensure safe access.
template <TrailingElementCountProvider Header, typename Element>
class FlexibleArrayChecked {
private:
    FlexibleArrayUnchecked<Header, Element> unchecked_storage;

    /// Constructs the FlexibleArrayChecked by taking ownership of an existing unchecked instance
    constexpr FlexibleArrayChecked(FlexibleArrayUnchecked<Header, Element>&& unchecked) noexcept
        : unchecked_storage(std::move(unchecked)) {}
public:
    /// Constructs a buffer with enough space to hold the header and `capacity` number of Elements.
    ///
    /// `init_header` must initialize the header by placement new/`std::construct_at` at the supplied memory address.
    [[nodiscard]] static constexpr auto with_header_initialized_by(const Int capacity,
                                                                   std::invocable<Header*> auto&& init_header) noexcept
        -> FlexibleArrayChecked
    {
        return FlexibleArrayChecked{FlexibleArrayUnchecked<Header, Element>::with_header_initialized_by(capacity, std::forward<decltype(init_header)>(init_header))};
    }

    /// Constructs a buffer with enough space to hold the header and `capacity` number of Elements.
    ///
    /// The given `header` is moved into the storage.
    [[nodiscard]] static constexpr auto with_header(Int const capacity, Header&& header) noexcept -> FlexibleArrayChecked
        requires(std::movable<Header>)
    {
        return with_header_initialized_by(capacity,
                                          [&](Header* place) { std::construct_at(place, std::move(header)); });
    }

    /// Creates an empty FlexibleArrayChecked with no allocated storage.
    [[nodiscard]] static constexpr auto create_empty() noexcept -> FlexibleArrayChecked 
    { 
        return FlexibleArrayChecked{FlexibleArrayUnchecked<Header, Element>::create_empty()}; 
    }

    /// Whether the FlexibleArrayChecked is valid (not moved-from).
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool { return unchecked_storage.is_valid(); }

    /// Returns the address for the place of the `i`th element in the array.
    ///
    /// Requires 0 <= `i` < `capacity()`, and the object being in a valid, non-moved-from state.
    template <typename Self>
    [[nodiscard]] constexpr auto element_address(this Self&& self, const Int i) noexcept
        -> const_pointee_like<Self, Element*>
    {
        precondition(i >= 0 && i < self.capacity(), "Index out of bounds");
        return self.unchecked_storage.element_address(i);
    }

    /// Returns the pointer to the header.
    ///
    /// Requires the FlexibleArrayChecked being in a valid, non-moved-from state.
    template <typename Self>
    [[nodiscard]] constexpr auto header(this Self&& self) noexcept -> const_pointee_like<Self, Header*>
    {
        return self.unchecked_storage.header();
    }

    /// The number of elements the storage has allocated space for.
    ///
    /// Requires the object to be in a valid, non-moved-from state.
    [[nodiscard]] constexpr auto capacity() const noexcept -> Int { return unchecked_storage.header()->trailing_element_count(); }

    /// Extracts the storage out of the trailing array, handing out the ownership to the callee.
    ///
    /// The original FlexibleCheckedArray will be left in a moved-from state.
    [[nodiscard]] constexpr auto extract_storage() -> FlexibleArrayUnchecked<Header, Element> { return std::move(unchecked_storage); }

    // Not copyable
    FlexibleArrayChecked(const FlexibleArrayChecked& other) = delete;
    FlexibleArrayChecked& operator=(const FlexibleArrayChecked& other) = delete;

    // Moveable
    FlexibleArrayChecked(FlexibleArrayChecked&& other) = default;
    FlexibleArrayChecked& operator=(FlexibleArrayChecked&& other) noexcept = default;
    ~FlexibleArrayChecked() = default;
    
    /// Swaps the underlying storage of `a` and `b`.
    friend void swap(FlexibleArrayChecked& a, FlexibleArrayChecked& b) noexcept 
    { 
        swap(a.unchecked_storage, b.unchecked_storage);
    }

    /// Projects a stack-allocated temporary
    template <std::invocable<FlexibleArrayChecked&> F>
    static constexpr auto project_temporary(const Int element_count, F consumer) -> std::invoke_result_t<F, FlexibleArrayChecked&>
    {
        return FlexibleArrayUnchecked<Header, Element>::project_temporary(element_count, [&](auto& unchecked) {
            // Wrap the unchecked version in a checked wrapper (consume the projected `unchecked` temporarily).
            FlexibleArrayChecked checked{std::move(unchecked)};
            auto result = consumer(checked);

            // After the consumer is done with it, move out the unchecked storage to restore the projection.
            unchecked = checked.extract_storage();
            return result;
        });
    }
};

#endif // CPP_MVS_FLEXIBLE_ARRAY_CHECKED_HPP
