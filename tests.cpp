#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "library.h"

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
    using FA      = FlexibleArray<Header, Element>;

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
    using FA = FlexibleArray<StandardHeader, T>;

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
            auto fa = FlexibleArray<TestHeader, int>::with_header(5, TestHeader{5, 101});
            CHECK(LifecycleTracker::constructed == 2); // 1 arg, 1 internal
            CHECK(LifecycleTracker::destroyed == 1);   // 1 arg destroyed

            auto fa2 = std::move(fa);
            CHECK(fa.leak_storage() == nullptr); // moved from
            CHECK(fa2.capacity() == 5);
        }
        // fa2 goes out of scope -> Header Dtor runs
        CHECK(LifecycleTracker::destroyed == 2);
    }

    TEST_CASE("project_temporary Stack Allocation") {
        LifecycleTracker::reset();

        int result = FlexibleArray<TestHeader, double>::project_temporary(3, [](auto& fa) {
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