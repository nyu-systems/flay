#include <gtest/gtest.h>

#include "backends/p4tools/modules/flay/core/control_plane/control_plane_assignment.h"
#include "backends/p4tools/modules/flay/core/control_plane/symbols.h"
#include "backends/p4tools/modules/flay/core/control_plane/z3_control_plane_assignment.h"
#include "backends/p4tools/modules/flay/test/helpers.h"
#include "ir/compare.h"
#include "ir/ir.h"

namespace P4::P4Tools::Test {
namespace {

TEST_F(P4FlayTest, PlaceholderStructuralComparison) {
    const IR::Constant one(IR::Type_Bits::get(8), 1), two(IR::Type_Bits::get(16), 2);
    const IR::Placeholder first("a"_cs, &one), copy("a"_cs, &two), later("b"_cs, &one);
    EXPECT_EQ(first.structuralCompare(copy), std::weak_ordering::equivalent);
    EXPECT_EQ(first.structuralCompare(later), std::weak_ordering::less);
    EXPECT_EQ(later.structuralCompare(first), std::weak_ordering::greater);
    const IR::SymbolicVariable symbolic(one.type, "a"_cs);
    EXPECT_EQ(first.structuralCompare(symbolic) < 0, first.typeId() < symbolic.typeId());
    EXPECT_EQ(symbolic.structuralCompare(first) < 0, symbolic.typeId() < first.typeId());
    const std::set<const IR::Node *, IR::StructuralLess> values{&first, &copy, &later};
    EXPECT_EQ(values.size(), 2U);
}

TEST_F(P4FlayTest, DataPlaneVariableStructuralComparison) {
    const IR::DataPlaneVariable first(IR::Type_Bits::get(8), "a"_cs);
    const IR::DataPlaneVariable copy(IR::Type_Bits::get(16), "a"_cs);
    const IR::DataPlaneVariable later(IR::Type_Bits::get(8), "b"_cs);
    EXPECT_EQ(first.structuralCompare(copy), std::weak_ordering::equivalent);
    EXPECT_EQ(first.structuralCompare(later), std::weak_ordering::less);
    const IR::SymbolicVariable symbolic(first.type, first.label);
    EXPECT_NE(first.structuralCompare(symbolic), std::weak_ordering::equivalent);
}

TEST_F(P4FlayTest, AssignmentPairsUseStructuralEquivalence) {
    const IR::SymbolicVariable key(IR::Type_Bits::get(8), "a"_cs);
    const IR::SymbolicVariable sameLabel(IR::Type_Bits::get(16), "a"_cs);
    const IR::SymbolicVariable laterKey(IR::Type_Bits::get(8), "b"_cs);
    const IR::Constant one(IR::Type_Bits::get(8), 1), two(IR::Type_Bits::get(8), 2);
    ASSERT_FALSE(key.equiv(sameLabel));
    ASSERT_EQ(key.structuralCompare(sameLabel), std::weak_ordering::equivalent);
    const Flay::ControlPlaneAssignmentPointerPair first{&key, &two}, second{&sameLabel, &one},
        third{&laterKey, &one};
    const Flay::ControlPlaneAssignmentReferencePair firstRef{key, two}, secondRef{sameLabel, one},
        thirdRef{laterKey, one};
    const Flay::StructuralAssignmentLess less;
    EXPECT_FALSE(less(first, second));
    EXPECT_TRUE(less(second, first));
    EXPECT_TRUE(less(first, third));
    EXPECT_FALSE(less(firstRef, secondRef));
    EXPECT_TRUE(less(secondRef, firstRef));
    EXPECT_TRUE(less(firstRef, thirdRef));
}

TEST_F(P4FlayTest, AssignmentSetsAreLexicographical) {
    const IR::SymbolicVariable key(IR::Type_Bits::get(8), "a"_cs);
    const IR::SymbolicVariable sameLabel(IR::Type_Bits::get(16), "a"_cs);
    const IR::SymbolicVariable laterKey(IR::Type_Bits::get(8), "b"_cs);
    const IR::Constant one(IR::Type_Bits::get(8), 1), two(IR::Type_Bits::get(8), 2);
    const Flay::ControlPlaneAssignmentSet empty, first{{key, one}}, equal{{sameLabel, one}},
        greaterValue{{key, two}}, greaterKey{{laterKey, one}}, prefix{{key, one}, {laterKey, two}};
    EXPECT_EQ(Flay::structuralCompare(empty, empty), std::weak_ordering::equivalent);
    EXPECT_EQ(Flay::structuralCompare(empty, first), std::weak_ordering::less);
    EXPECT_EQ(Flay::structuralCompare(first, equal), std::weak_ordering::equivalent);
    EXPECT_EQ(Flay::structuralCompare(first, greaterValue), std::weak_ordering::less);
    EXPECT_EQ(Flay::structuralCompare(greaterValue, greaterKey), std::weak_ordering::less);
    EXPECT_EQ(Flay::structuralCompare(greaterKey, greaterValue), std::weak_ordering::greater);
    EXPECT_EQ(Flay::structuralCompare(first, prefix), std::weak_ordering::less);
    EXPECT_EQ(Flay::structuralCompare(prefix, first), std::weak_ordering::greater);
}

class CountingConstant : public IR::Constant {
    size_t &comparisons;

 public:
    explicit CountingConstant(size_t &comparisons)
        : IR::Constant(IR::Type_Bits::get(8), 1), comparisons(comparisons) {}
    std::weak_ordering structuralCompare(const IR::Node &other) const override {
        ++comparisons;
        return IR::Constant::structuralCompare(other);
    }
};

TEST_F(P4FlayTest, AssignmentValuesAreComparedOnce) {
    size_t comparisons = 0;
    const IR::Expression *first = new CountingConstant(comparisons);
    const IR::Expression *second = new CountingConstant(comparisons);
    for (int depth = 0; depth < 8; ++depth) {
        first = new IR::Neg(first);
        second = new IR::Neg(second);
    }
    const IR::SymbolicVariable key(IR::Type_Bits::get(8), "a"_cs);
    const Flay::ControlPlaneAssignmentSet left{{key, *first}}, right{{key, *second}};
    EXPECT_EQ(Flay::structuralCompare(left, right), std::weak_ordering::equivalent);
    EXPECT_EQ(comparisons, 1U);
}

TEST_F(P4FlayTest, SymbolCollectionsUseStructuralKeys) {
    const IR::SymbolicVariable first(IR::Type_Bits::get(8), "a"_cs);
    const IR::SymbolicVariable copy(IR::Type_Bits::get(16), "a"_cs);
    const Flay::SymbolSet symbols{first, copy};
    EXPECT_EQ(symbols.size(), 1U);
    Flay::SymbolMap uses;
    uses[first].insert(&first);
    uses[copy].insert(&copy);
    EXPECT_EQ(uses.size(), 1U);
    EXPECT_EQ(uses.at(first).size(), 2U);
}

TEST_F(P4FlayTest, Z3AssignmentsUseStructuralKeys) {
    const IR::SymbolicVariable first(IR::Type_Bits::get(8), "a"_cs);
    const IR::SymbolicVariable copy(IR::Type_Bits::get(8), "a"_cs);
    Flay::Z3ControlPlaneAssignmentSet assignments;
    EXPECT_TRUE(assignments.add(first, Z3Cache::context().bv_val(1, 8)));
    assignments.setSymbolic(copy);
    EXPECT_EQ(assignments.size(), 1U);
    assignments.clear();
    EXPECT_EQ(assignments.size(), 0U);
}

}  // namespace
}  // namespace P4::P4Tools::Test
