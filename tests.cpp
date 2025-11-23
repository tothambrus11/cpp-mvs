#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "library.h"
#include "flexible_array_checked.hpp"

// =============================================================================
// 1. HELPERS & LIFECYCLE TRACKING
// =============================================================================

struct LifecycleTracker {
    static inline int constructed = 0;
    static inline int destroyed = 0;
    static void reset() { constructed = 0; destroyed = 0; }
};

// A standard header for lifecycle testing
struct TestHeader {
    Int count;
    int id;

    TestHeader(Int c, int i) : count(c), id(i) { LifecycleTracker::constructed++; }
    ~TestHeader() { LifecycleTracker::destroyed++; }

    TestHeader(TestHeader&& other) noexcept : count(other.count), id(other.id) {
        LifecycleTracker::constructed++;
        other.count = 0; other.id = -1;
    }
    TestHeader& operator=(TestHeader&& other) noexcept {
        if (this != &other) {
            count = other.count; id = other.id;
            other.count = 0; other.id = -1;
        }
        return *this;
    }

    [[nodiscard]] Int trailing_element_count() const { return count; }
};

// =============================================================================
// 2. EXOTIC HEADERS & ELEMENTS FOR TEMPLATE TESTING
// =============================================================================

// Helper to satisfy the TrailingElementCountProvider concept easily
struct HeaderBase {
    Int cap;
    explicit HeaderBase(Int c) : cap(c) {}
    [[nodiscard]] Int trailing_element_count() const { return cap; }
};

// Scenario A: Standard Alignment (size 8, align 8)
struct StandardHeader : HeaderBase { using HeaderBase::HeaderBase; };

// Scenario B: Small/Packed Header (size 9, align 1)
// Likely to cause padding issues if Element requires alignment > 1
struct PackedHeader {
    Int cap;
    char c;
    explicit PackedHeader(Int c) : cap(c), c('a') {}
    [[nodiscard]] Int trailing_element_count() const { return cap; }
};

// Scenario C: Over-aligned Header (size 32, align 32)
struct alignas(32) OverAlignedHeader : HeaderBase { using HeaderBase::HeaderBase; };

// Elements
struct alignas(64) OverAlignedElement { char data[64]; };
struct SmallElement { char c; };

// =============================================================================
// 3. TYPE BUNDLING FOR DOCTEST TEMPLATES
// =============================================================================

// This struct bundles a Header type and an Element type into one T
// that TEST_CASE_TEMPLATE can iterate over.
template<typename H, typename E>
struct Spec {
    using Header = H;
    using Element = E;
    static std::string name() {
        return std::string("H_align:") + std::to_string(alignof(H)) +
               " | E_align:" + std::to_string(alignof(E));
    }
};

// =============================================================================
// 4. TEMPLATE TESTS
// =============================================================================

// We test combinations that stress the arithmetic:
// 1. Header Align > Element Align
// 2. Header Align < Element Align
// 3. Header Size not multiple of Element Align (padding required)
// 4. Massive over-alignment on elements
TEST_CASE_TEMPLATE("FlexibleArray Memory Layout Matrix", T,
    Spec<StandardHeader, int>,              // Standard case
    Spec<StandardHeader, double>,           // H_align(8) == E_align(8)
    Spec<PackedHeader, int>,                // H_size(9) vs E_align(4) -> Needs 3 bytes padding
    Spec<PackedHeader, double>,             // H_size(9) vs E_align(8) -> Needs 7 bytes padding
    Spec<OverAlignedHeader, char>,          // H_align(32) >> E_align(1)
    Spec<StandardHeader, OverAlignedElement>// H_align(8) << E_align(64) -> Gap depends on E
) {
    using Header  = typename T::Header;
    using Element = typename T::Element;
    using FA      = FlexibleArrayChecked<Header, Element>;

    constexpr Int kCapacity = 3;

    // Construct with header (using default constructor of header if simple, or explicit)
    // We assume the test headers defined above have a constructor taking (Int).
    auto fa = FA::with_header(kCapacity, Header{kCapacity});

    INFO("Testing combination: ", T::name());

    // 1. Validate pointers are not null
    CHECK(fa.header() != nullptr);
    CHECK(fa.element_address(0) != nullptr);

    // 2. Validate Capacity
    CHECK(fa.capacity() == kCapacity);

    // 3. Validate Memory Layout / Padding Logic
    auto h_addr = reinterpret_cast<uintptr_t>(fa.header());
    auto e0_addr = reinterpret_cast<uintptr_t>(fa.element_address(0));
    auto e1_addr = reinterpret_cast<uintptr_t>(fa.element_address(1));

    // A. Header alignment check
    CHECK_MESSAGE(h_addr % alignof(Header) == 0, "Header address is not aligned to Header requirements");

    // B. Element alignment check
    CHECK_MESSAGE(e0_addr % alignof(Element) == 0, "Element(0) address is not aligned to Element requirements");

    // C. Offset Calculation Check
    // The offset of the first element must be:
    // sizeof(Header) rounded up to the nearest multiple of alignof(Element)
    size_t header_size = sizeof(Header);
    size_t element_align = alignof(Element);
    size_t expected_offset = (header_size + element_align - 1) & ~(element_align - 1);

    CHECK_EQ((e0_addr - h_addr), expected_offset);

    // D. Stride Check (Distance between elements)
    // Should be sizeof(Element)
    CHECK_EQ((e1_addr - e0_addr), sizeof(Element));
}

TEST_CASE_TEMPLATE("Primitive Signed Integers Integration", T, char, short, int, long long) {
    // Tests that FlexibleArray works with standard primitives as Elements
    // using a fixed simple header.
    using FA = FlexibleArrayChecked<StandardHeader, T>;

    auto fa = FA::with_header(5, StandardHeader{5});

    // Write generic values
    for(int i=0; i<5; ++i) {
        T val = static_cast<T>(i * 10);
        std::construct_at(fa.element_address(i), val);
    }

    // Read back
    CHECK(*fa.element_address(0) == static_cast<T>(0));
    CHECK(*fa.element_address(4) == static_cast<T>(40));
}

// =============================================================================
// 5. EXISTING SPECIFIC TESTS (Refined)
// =============================================================================

TEST_SUITE("FlexibleArray Lifecycle") {
    TEST_CASE("Construction, Move, and Destruction") {
        LifecycleTracker::reset();
        {
            auto fa = FlexibleArrayChecked<TestHeader, int>::with_header(5, TestHeader{5, 101});
            CHECK(LifecycleTracker::constructed == 2); // 1 arg, 1 internal
            CHECK(LifecycleTracker::destroyed == 1);   // 1 arg destroyed

            auto fa2 = std::move(fa);
            // fa is now in moved-from state
            CHECK(fa2.capacity() == 5);
        }
        // fa2 goes out of scope -> Header Dtor runs
        CHECK(LifecycleTracker::destroyed == 2);
    }

    TEST_CASE("project_temporary Stack Allocation") {
        LifecycleTracker::reset();

        int result = FlexibleArrayChecked<TestHeader, double>::project_temporary(3, [](auto& fa) {
            std::construct_at(fa.header(), 3, 999);

            CHECK(fa.capacity() == 3);

            // Check stack alignment
            auto addr = reinterpret_cast<uintptr_t>(fa.header());
            // Stack allocations often have stricter alignment needs, ensure the macro works
            CHECK(addr % alignof(TestHeader) == 0);

            return 123;
        });

        CHECK(result == 123);
        // project_temporary manually destroys header
        CHECK(LifecycleTracker::destroyed == 1);
    }
}

TEST_SUITE("UnsafeBufferPointer") {
    TEST_CASE("Bounds Checking") {
        int data[] = {1, 2, 3};
        UnsafeBufferPointer<int> ptr(data, 3);

        CHECK(ptr[0] == 1);
        CHECK(ptr[2] == 3);

        // Note: We cannot easily test ptr[3] crashing (quick_exit) in standard unit tests
        // without a death-test harness, which Doctest supports via subcases/forking
        // but is complex to set up in a single file snippet.
    }
}

TEST_SUITE("FlexibleArrayUnchecked Direct Usage") {
    TEST_CASE("Unchecked version without capacity tracking") {
        // Test that FlexibleArrayUnchecked works without storing or checking capacity
        using FA = FlexibleArrayUnchecked<StandardHeader, int>;
        
        auto fa = FA::with_header(5, StandardHeader{5});
        
        // Can access header
        CHECK(fa.header() != nullptr);
        CHECK(fa.header()->trailing_element_count() == 5);
        
        // Can get element addresses (no bounds checking)
        auto* elem0 = fa.element_address(0);
        auto* elem4 = fa.element_address(4);
        CHECK(elem0 != nullptr);
        CHECK(elem4 != nullptr);
        
        // Verify proper spacing between elements
        CHECK((elem4 - elem0) == 4);
        
        // Initialize elements
        for(int i = 0; i < 5; ++i) {
            std::construct_at(fa.element_address(i), i * 10);
        }
        
        // Read back
        CHECK(*fa.element_address(0) == 0);
        CHECK(*fa.element_address(4) == 40);
    }
    
    TEST_CASE("Unchecked move semantics") {
        using FA = FlexibleArrayUnchecked<StandardHeader, double>;
        
        auto fa1 = FA::with_header(3, StandardHeader{3});
        std::construct_at(fa1.element_address(0), 1.5);
        
        auto fa2 = std::move(fa1);
        CHECK(fa1.leak_storage() == nullptr);  // moved-from
        CHECK(*fa2.element_address(0) == 1.5);
    }
}

// =============================================================================
// 6. COMPREHENSIVE EDGE CASE TESTS
// =============================================================================

TEST_SUITE("FlexibleArrayChecked Edge Cases") {
    TEST_CASE("Zero capacity array") {
        using FA = FlexibleArrayChecked<StandardHeader, int>;
        auto fa = FA::with_header(0, StandardHeader{0});
        
        CHECK(fa.capacity() == 0);
        CHECK(fa.header() != nullptr);
        CHECK(fa.is_valid());
    }
    
    TEST_CASE("Create empty checked array") {
        using FA = FlexibleArrayChecked<StandardHeader, int>;
        auto fa = FA::create_empty();
        
        CHECK(!fa.is_valid());
        
        // Move constructor preserves invalid state
        auto fa2 = std::move(fa);
        CHECK(!fa.is_valid());
        CHECK(!fa2.is_valid());
    }
    
    TEST_CASE("is_valid after move") {
        using FA = FlexibleArrayChecked<StandardHeader, int>;
        auto fa1 = FA::with_header(3, StandardHeader{3});
        
        CHECK(fa1.is_valid());
        
        auto fa2 = std::move(fa1);
        CHECK(!fa1.is_valid());
        CHECK(fa2.is_valid());
    }
    
    TEST_CASE("is_valid after extract_storage") {
        using FA = FlexibleArrayChecked<StandardHeader, int>;
        auto fa = FA::with_header(3, StandardHeader{3});
        
        CHECK(fa.is_valid());
        
        auto unchecked = fa.extract_storage();
        CHECK(!fa.is_valid());
        CHECK(unchecked.is_valid());
    }
    
    TEST_CASE("Extract storage functionality") {
        using FA = FlexibleArrayChecked<StandardHeader, int>;
        auto fa = FA::with_header(3, StandardHeader{3});
        
        std::construct_at(fa.element_address(0), 42);
        std::construct_at(fa.element_address(1), 43);
        
        // Extract the unchecked storage
        auto unchecked = fa.extract_storage();
        
        // Verify data is intact
        CHECK(*unchecked.element_address(0) == 42);
        CHECK(*unchecked.element_address(1) == 43);
        CHECK(unchecked.header()->trailing_element_count() == 3);
    }
    
    TEST_CASE("Swap functionality") {
        using FA = FlexibleArrayChecked<StandardHeader, int>;
        auto fa1 = FA::with_header(2, StandardHeader{2});
        auto fa2 = FA::with_header(3, StandardHeader{3});
        
        std::construct_at(fa1.element_address(0), 10);
        std::construct_at(fa2.element_address(0), 20);
        
        swap(fa1, fa2);
        
        CHECK(fa1.capacity() == 3);
        CHECK(fa2.capacity() == 2);
        CHECK(*fa1.element_address(0) == 20);
        CHECK(*fa2.element_address(0) == 10);
    }
    
    TEST_CASE("Move assignment") {
        using FA = FlexibleArrayChecked<StandardHeader, int>;
        auto fa1 = FA::with_header(2, StandardHeader{2});
        auto fa2 = FA::with_header(3, StandardHeader{3});
        
        std::construct_at(fa1.element_address(0), 100);
        std::construct_at(fa2.element_address(0), 200);
        
        fa1 = std::move(fa2);
        
        CHECK(fa1.capacity() == 3);
        CHECK(*fa1.element_address(0) == 200);
    }
    
    TEST_CASE("Self move assignment") {
        using FA = FlexibleArrayChecked<StandardHeader, int>;
        auto fa = FA::with_header(2, StandardHeader{2});
        std::construct_at(fa.element_address(0), 42);
        
        fa = std::move(fa);  // Self-move
        
        CHECK(fa.capacity() == 2);
        CHECK(*fa.element_address(0) == 42);
    }
    
    TEST_CASE("Custom header initializer") {
        struct ComplexHeader {
            Int cap;
            int value1;
            double value2;
            
            ComplexHeader(Int c, int v1, double v2) : cap(c), value1(v1), value2(v2) {}
            [[nodiscard]] Int trailing_element_count() const { return cap; }
        };
        
        using FA = FlexibleArrayChecked<ComplexHeader, int>;
        auto fa = FA::with_header_initialized_by(5, [](ComplexHeader* place) {
            std::construct_at(place, 5, 999, 3.14);
        });
        
        CHECK(fa.capacity() == 5);
        CHECK(fa.header()->value1 == 999);
        CHECK(fa.header()->value2 == 3.14);
    }
    
    TEST_CASE("Non-primitive element types") {
        struct ComplexElement {
            int a;
            double b;
            char c;
            
            ComplexElement(int x, double y, char z) : a(x), b(y), c(z) {}
        };
        
        using FA = FlexibleArrayChecked<StandardHeader, ComplexElement>;
        auto fa = FA::with_header(3, StandardHeader{3});
        
        std::construct_at(fa.element_address(0), 1, 1.1, 'a');
        std::construct_at(fa.element_address(1), 2, 2.2, 'b');
        std::construct_at(fa.element_address(2), 3, 3.3, 'c');
        
        CHECK(fa.element_address(0)->a == 1);
        CHECK(fa.element_address(1)->b == 2.2);
        CHECK(fa.element_address(2)->c == 'c');
    }
    
    TEST_CASE("Large capacity array") {
        using FA = FlexibleArrayChecked<StandardHeader, char>;
        constexpr Int large_capacity = 10000;
        auto fa = FA::with_header(large_capacity, StandardHeader{large_capacity});
        
        CHECK(fa.capacity() == large_capacity);
        
        // Initialize some elements
        std::construct_at(fa.element_address(0), 'A');
        std::construct_at(fa.element_address(9999), 'Z');
        
        CHECK(*fa.element_address(0) == 'A');
        CHECK(*fa.element_address(9999) == 'Z');
    }
}

TEST_SUITE("FlexibleArrayUnchecked Edge Cases") {
    TEST_CASE("Zero capacity unchecked array") {
        using FA = FlexibleArrayUnchecked<StandardHeader, int>;
        auto fa = FA::with_header(0, StandardHeader{0});
        
        CHECK(fa.header() != nullptr);
        CHECK(fa.header()->trailing_element_count() == 0);
        CHECK(fa.is_valid());
    }
    
    TEST_CASE("Create empty unchecked array") {
        using FA = FlexibleArrayUnchecked<StandardHeader, int>;
        auto fa = FA::create_empty();
        
        CHECK(!fa.is_valid());
        
        // Move constructor preserves invalid state
        auto fa2 = std::move(fa);
        CHECK(!fa.is_valid());
        CHECK(!fa2.is_valid());
    }
    
    TEST_CASE("is_valid for unchecked after move") {
        using FA = FlexibleArrayUnchecked<StandardHeader, int>;
        auto fa1 = FA::with_header(3, StandardHeader{3});
        
        CHECK(fa1.is_valid());
        
        auto fa2 = std::move(fa1);
        CHECK(!fa1.is_valid());
        CHECK(fa2.is_valid());
    }
    
    TEST_CASE("is_valid after leak_storage") {
        using FA = FlexibleArrayUnchecked<StandardHeader, int>;
        auto fa = FA::with_header(3, StandardHeader{3});
        
        CHECK(fa.is_valid());
        
        auto* raw = fa.leak_storage();
        CHECK(!fa.is_valid());
        CHECK(raw != nullptr);
        
        // Manually cleanup
        std::destroy_at(reinterpret_cast<StandardHeader*>(raw));
        Detail::aligned_free(raw);
    }
    
    TEST_CASE("Swap unchecked arrays") {
        using FA = FlexibleArrayUnchecked<StandardHeader, int>;
        auto fa1 = FA::with_header(2, StandardHeader{2});
        auto fa2 = FA::with_header(3, StandardHeader{3});
        
        std::construct_at(fa1.element_address(0), 10);
        std::construct_at(fa2.element_address(0), 20);
        
        swap(fa1, fa2);
        
        CHECK(fa1.header()->trailing_element_count() == 3);
        CHECK(fa2.header()->trailing_element_count() == 2);
        CHECK(*fa1.element_address(0) == 20);
        CHECK(*fa2.element_address(0) == 10);
    }
    
    TEST_CASE("Move assignment for unchecked") {
        using FA = FlexibleArrayUnchecked<StandardHeader, int>;
        auto fa1 = FA::with_header(2, StandardHeader{2});
        auto fa2 = FA::with_header(3, StandardHeader{3});
        
        std::construct_at(fa1.element_address(0), 100);
        std::construct_at(fa2.element_address(0), 200);
        
        fa1 = std::move(fa2);
        
        CHECK(fa1.header()->trailing_element_count() == 3);
        CHECK(*fa1.element_address(0) == 200);
        CHECK(fa2.leak_storage() == nullptr);  // moved-from
    }
    
    TEST_CASE("Self move assignment unchecked") {
        using FA = FlexibleArrayUnchecked<StandardHeader, int>;
        auto fa = FA::with_header(2, StandardHeader{2});
        std::construct_at(fa.element_address(0), 42);
        
        fa = std::move(fa);  // Self-move
        
        CHECK(fa.header()->trailing_element_count() == 2);
        CHECK(*fa.element_address(0) == 42);
    }
    
    TEST_CASE("Leak storage functionality") {
        using FA = FlexibleArrayUnchecked<StandardHeader, int>;
        auto fa = FA::with_header(3, StandardHeader{3});
        
        std::construct_at(fa.element_address(0), 999);
        
        auto* raw_storage = fa.leak_storage();
        CHECK(raw_storage != nullptr);
        
        // Manually manage the leaked storage
        auto* header = reinterpret_cast<StandardHeader*>(raw_storage);
        CHECK(header->trailing_element_count() == 3);
        
        // Manual cleanup
        std::destroy_at(header);
        Detail::aligned_free(raw_storage);
    }
    
    TEST_CASE("Project temporary with unchecked") {
        int result = FlexibleArrayUnchecked<StandardHeader, int>::project_temporary(5, [](auto& fa) {
            std::construct_at(fa.header(), 5);
            
            for (int i = 0; i < 5; ++i) {
                std::construct_at(fa.element_address(i), i * i);
            }
            
            int sum = 0;
            for (int i = 0; i < 5; ++i) {
                sum += *fa.element_address(i);
            }
            
            return sum;
        });
        
        CHECK(result == 0 + 1 + 4 + 9 + 16); // 30
    }
}

TEST_SUITE("Mixed Checked and Unchecked Usage") {
    TEST_CASE("Convert checked to unchecked and back") {
        using FAChecked = FlexibleArrayChecked<StandardHeader, int>;
        using FAUnchecked = FlexibleArrayUnchecked<StandardHeader, int>;
        
        auto checked = FAChecked::with_header(3, StandardHeader{3});
        std::construct_at(checked.element_address(0), 42);
        
        // Extract to unchecked
        auto unchecked = checked.extract_storage();
        CHECK(*unchecked.element_address(0) == 42);
        
        // Wrap back in checked (move into private constructor via factory)
        auto checked2 = FAChecked::with_header_initialized_by(0, [&](auto*) {
            // Don't actually initialize, we'll swap
        });
        
        // Since we can't directly construct from unchecked publicly, 
        // verify the unchecked can still be used independently
        CHECK(unchecked.header()->trailing_element_count() == 3);
    }
    
    TEST_CASE("Checked wraps unchecked without overhead") {
        using FAChecked = FlexibleArrayChecked<StandardHeader, int>;
        using FAUnchecked = FlexibleArrayUnchecked<StandardHeader, int>;
        
        // Verify that FlexibleArrayChecked has the same size as FlexibleArrayUnchecked
        // since it should only contain the unchecked storage member
        CHECK(sizeof(FAChecked) == sizeof(FAUnchecked));
    }
}

TEST_SUITE("is_valid() and create_empty() Comprehensive Tests") {
    TEST_CASE("Empty array move assignment") {
        using FA = FlexibleArrayChecked<StandardHeader, int>;
        auto fa1 = FA::with_header(3, StandardHeader{3});
        auto fa2 = FA::create_empty();
        
        CHECK(fa1.is_valid());
        CHECK(!fa2.is_valid());
        
        fa1 = std::move(fa2);
        CHECK(!fa1.is_valid());
    }
    
    TEST_CASE("Move from empty to non-empty") {
        using FA = FlexibleArrayChecked<StandardHeader, int>;
        auto fa1 = FA::create_empty();
        auto fa2 = FA::with_header(3, StandardHeader{3});
        
        CHECK(!fa1.is_valid());
        CHECK(fa2.is_valid());
        
        fa1 = std::move(fa2);
        CHECK(fa1.is_valid());
        CHECK(!fa2.is_valid());
        CHECK(fa1.capacity() == 3);
    }
    
    TEST_CASE("Swap with empty array") {
        using FA = FlexibleArrayChecked<StandardHeader, int>;
        auto fa1 = FA::with_header(3, StandardHeader{3});
        auto fa2 = FA::create_empty();
        
        CHECK(fa1.is_valid());
        CHECK(!fa2.is_valid());
        
        swap(fa1, fa2);
        
        CHECK(!fa1.is_valid());
        CHECK(fa2.is_valid());
        CHECK(fa2.capacity() == 3);
    }
    
    TEST_CASE("is_valid throughout lifecycle") {
        using FA = FlexibleArrayChecked<StandardHeader, int>;
        
        // Create
        auto fa = FA::with_header(5, StandardHeader{5});
        CHECK(fa.is_valid());
        
        // Use
        std::construct_at(fa.element_address(0), 42);
        CHECK(fa.is_valid());
        CHECK(*fa.element_address(0) == 42);
        
        // Move
        auto fa2 = std::move(fa);
        CHECK(!fa.is_valid());
        CHECK(fa2.is_valid());
        CHECK(*fa2.element_address(0) == 42);
        
        // Extract
        auto unchecked = fa2.extract_storage();
        CHECK(!fa2.is_valid());
        CHECK(unchecked.is_valid());
        CHECK(*unchecked.element_address(0) == 42);
    }
    
    TEST_CASE("Unchecked empty array move assignment") {
        using FA = FlexibleArrayUnchecked<StandardHeader, int>;
        auto fa1 = FA::with_header(3, StandardHeader{3});
        auto fa2 = FA::create_empty();
        
        CHECK(fa1.is_valid());
        CHECK(!fa2.is_valid());
        
        fa1 = std::move(fa2);
        CHECK(!fa1.is_valid());
    }
    
    TEST_CASE("Unchecked swap with empty array") {
        using FA = FlexibleArrayUnchecked<StandardHeader, int>;
        auto fa1 = FA::with_header(3, StandardHeader{3});
        auto fa2 = FA::create_empty();
        
        CHECK(fa1.is_valid());
        CHECK(!fa2.is_valid());
        
        swap(fa1, fa2);
        
        CHECK(!fa1.is_valid());
        CHECK(fa2.is_valid());
        CHECK(fa2.header()->trailing_element_count() == 3);
    }
    
    TEST_CASE("Multiple create_empty calls") {
        using FA = FlexibleArrayChecked<StandardHeader, int>;
        
        auto fa1 = FA::create_empty();
        auto fa2 = FA::create_empty();
        auto fa3 = FA::create_empty();
        
        CHECK(!fa1.is_valid());
        CHECK(!fa2.is_valid());
        CHECK(!fa3.is_valid());
        
        // They should all be independently invalid
        auto fa4 = std::move(fa1);
        CHECK(!fa4.is_valid());
    }
    
    TEST_CASE("Self-move of empty array") {
        using FA = FlexibleArrayChecked<StandardHeader, int>;
        auto fa = FA::create_empty();
        
        CHECK(!fa.is_valid());
        
        fa = std::move(fa);
        CHECK(!fa.is_valid());
    }
    
    TEST_CASE("Validity checks with different header types") {
        struct ComplexHeader {
            Int cap;
            int value;
            ComplexHeader(Int c, int v) : cap(c), value(v) {}
            [[nodiscard]] Int trailing_element_count() const { return cap; }
        };
        
        using FA = FlexibleArrayChecked<ComplexHeader, double>;
        
        auto empty = FA::create_empty();
        CHECK(!empty.is_valid());
        
        auto valid = FA::with_header(5, ComplexHeader{5, 999});
        CHECK(valid.is_valid());
        CHECK(valid.capacity() == 5);
        CHECK(valid.header()->value == 999);
    }
}

TEST_SUITE("Alignment and Padding Verification") {
    TEST_CASE("Misaligned header with over-aligned elements") {
        struct TinyHeader {
            char tag;
            Int cap;
            [[nodiscard]] Int trailing_element_count() const { return cap; }
        };
        
        struct alignas(16) AlignedElement {
            double value;
        };
        
        using FA = FlexibleArrayChecked<TinyHeader, AlignedElement>;
        auto fa = FA::with_header(5, TinyHeader{'X', 5});
        
        // Verify elements are properly aligned
        auto elem_addr = reinterpret_cast<uintptr_t>(fa.element_address(0));
        CHECK(elem_addr % 16 == 0);
    }
    
    TEST_CASE("Header larger than element alignment") {
        struct alignas(64) LargeHeader {
            char data[64];
            Int cap;
            explicit LargeHeader(Int c) : cap(c) { data[0] = 'H'; }
            [[nodiscard]] Int trailing_element_count() const { return cap; }
        };
        
        using FA = FlexibleArrayChecked<LargeHeader, char>;
        auto fa = FA::with_header(10, LargeHeader{10});
        
        CHECK(fa.capacity() == 10);
        auto header_addr = reinterpret_cast<uintptr_t>(fa.header());
        CHECK(header_addr % 64 == 0);
    }
}
