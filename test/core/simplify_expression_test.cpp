
#include "backends/p4tools/modules/flay/core/lib/simplify_expression.h"

#include <gtest/gtest.h>

#include <sstream>

#include <boost/multiprecision/cpp_int.hpp>

#include "backends/p4tools/common/lib/variables.h"
#include "backends/p4tools/modules/flay/core/interpreter/execution_state.h"
#include "backends/p4tools/modules/flay/core/lib/expression_strength_reduction.h"
#include "backends/p4tools/modules/flay/test/helpers.h"
#include "ir/ir.h"
#include "ir/irutils.h"

namespace P4::P4Tools::Test {

namespace {

using namespace P4::literals;

// Tests for the optimization of various expressions.
TEST_F(P4FlayTest, Optimization01) {
    const auto *eightBitType = IR::Type_Bits::get(8);
    const auto *xVar =
        P4Tools::ToolsVariables::getSymbolicVariable(IR::Type_Boolean::get(), "X"_cs);
    const auto *aVar = P4Tools::ToolsVariables::getSymbolicVariable(eightBitType, "A"_cs);
    const auto *bVar = P4Tools::ToolsVariables::getSymbolicVariable(eightBitType, "B"_cs);

    // |X(bool)| ? |X(bool)| ? |A(bit<8>)| : |B(bit<8>)| : |B(bit<8>)|"
    const auto *nestedMuxExpression = new IR::Mux(xVar, new IR::Mux(xVar, aVar, bVar), bVar);
    const auto *optimizedExpression = P4Tools::SimplifyExpression::simplify(nestedMuxExpression);

    std::stringstream stringResult;
    optimizedExpression->dbprint(stringResult);

    ASSERT_STREQ("|X(bool)| ? |A(bit<8>)| : |B(bit<8>)|;", stringResult.str().c_str());
}

TEST_F(P4FlayTest, Optimization02) {
    const auto *eightBitType = IR::Type_Bits::get(8);
    const auto *xVar =
        P4Tools::ToolsVariables::getSymbolicVariable(IR::Type_Boolean::get(), "X"_cs);
    const auto *aVar = P4Tools::ToolsVariables::getSymbolicVariable(eightBitType, "A"_cs);
    const auto *bVar = P4Tools::ToolsVariables::getSymbolicVariable(eightBitType, "B"_cs);

    // |X(bool)| ? !|X(bool)| ? |A(bit<8>)| : |B(bit<8>)| : |B(bit<8>)|
    const auto *nestedMuxExpression =
        new IR::Mux(xVar, new IR::Mux(new IR::LNot(xVar), aVar, bVar), bVar);
    const auto *optimizedExpression = P4Tools::SimplifyExpression::simplify(nestedMuxExpression);

    std::stringstream stringResult;
    optimizedExpression->dbprint(stringResult);

    ASSERT_STREQ("|B(bit<8>)|", stringResult.str().c_str());
}

TEST_F(P4FlayTest, Optimization03) {
    const auto *eightBitType = IR::Type_Bits::get(8);
    const auto *xVar =
        P4Tools::ToolsVariables::getSymbolicVariable(IR::Type_Boolean::get(), "X"_cs);
    const auto *yVar =
        P4Tools::ToolsVariables::getSymbolicVariable(IR::Type_Boolean::get(), "Y"_cs);
    const auto *aVar = P4Tools::ToolsVariables::getSymbolicVariable(eightBitType, "A"_cs);
    const auto *bVar = P4Tools::ToolsVariables::getSymbolicVariable(eightBitType, "B"_cs);

    // |X(bool)| ? |Y(bool)| || |X(bool)| ? |A(bit<8>)| : |B(bit<8>)| : |B(bit<8>)|
    const auto *nestedMuxExpression =
        new IR::Mux(xVar, new IR::Mux(new IR::LOr(yVar, xVar), aVar, bVar), bVar);
    const auto *optimizedExpression = P4Tools::SimplifyExpression::simplify(nestedMuxExpression);

    std::stringstream stringResult;
    optimizedExpression->dbprint(stringResult);

    ASSERT_STREQ("|X(bool)| ? |A(bit<8>)| : |B(bit<8>)|;", stringResult.str().c_str());
}

TEST_F(P4FlayTest, Optimization04) {
    const auto *eightBitType = IR::Type_Bits::get(8);
    const auto *xVar =
        P4Tools::ToolsVariables::getSymbolicVariable(IR::Type_Boolean::get(), "X"_cs);
    const auto *yVar =
        P4Tools::ToolsVariables::getSymbolicVariable(IR::Type_Boolean::get(), "Y"_cs);
    const auto *aVar = P4Tools::ToolsVariables::getSymbolicVariable(eightBitType, "A"_cs);
    const auto *bVar = P4Tools::ToolsVariables::getSymbolicVariable(eightBitType, "B"_cs);
    {
        // |X(bool)| ? |Y(bool)| ? |A(bit<8>)| : |B(bit<8>)| : |B(bit<8>)|
        const auto *nestedMuxExpression = new IR::Mux(xVar, new IR::Mux(yVar, aVar, bVar), bVar);
        const auto *optimizedExpression =
            P4Tools::SimplifyExpression::simplify(nestedMuxExpression);

        std::stringstream stringResult;
        optimizedExpression->dbprint(stringResult);

        ASSERT_STREQ("|X(bool)| && |Y(bool)| ? |A(bit<8>)| : |B(bit<8>)|;",
                     stringResult.str().c_str());
    }
    {
        // |X(bool)| ? |Y(bool)| ? |B(bit<8>)| : |A(bit<8>)| : |B(bit<8>)|
        const auto *nestedMuxExpression = new IR::Mux(xVar, new IR::Mux(yVar, bVar, aVar), bVar);
        const auto *optimizedExpression =
            P4Tools::SimplifyExpression::simplify(nestedMuxExpression);

        std::stringstream stringResult;
        optimizedExpression->dbprint(stringResult);
        ASSERT_STREQ("|X(bool)| && !|Y(bool)| ? |A(bit<8>)| : |B(bit<8>)|;",
                     stringResult.str().c_str());
    }
    {
        // |X(bool)| ? |A(bit<8>)| : |Y(bool)| ? |A(bit<8>)| : |B(bit<8>)|
        const auto *nestedMuxExpression = new IR::Mux(xVar, aVar, new IR::Mux(yVar, aVar, bVar));
        const auto *optimizedExpression =
            P4Tools::SimplifyExpression::simplify(nestedMuxExpression);

        std::stringstream stringResult;
        optimizedExpression->dbprint(stringResult);
        ASSERT_STREQ("|X(bool)| || |Y(bool)| ? |A(bit<8>)| : |B(bit<8>)|;",
                     stringResult.str().c_str());
    }
    {
        // |X(bool)| ? |A(bit<8>)| : |Y(bool)| ? |B(bit<8>)| : |A(bit<8>)|
        const auto *nestedMuxExpression = new IR::Mux(xVar, aVar, new IR::Mux(yVar, bVar, aVar));
        const auto *optimizedExpression =
            P4Tools::SimplifyExpression::simplify(nestedMuxExpression);

        std::stringstream stringResult;
        optimizedExpression->dbprint(stringResult);
        ASSERT_STREQ("|X(bool)| || !|Y(bool)| ? |A(bit<8>)| : |B(bit<8>)|;",
                     stringResult.str().c_str());
    }
}

class CountedSymbolicVariable : public IR::SymbolicVariable {
    size_t &clones;

 public:
    explicit CountedSymbolicVariable(size_t &clones)
        : IR::SymbolicVariable(IR::Type_Boolean::get(), "shared"_cs), clones(clones) {}

    CountedSymbolicVariable *clone() const override {
        ++clones;
        return new CountedSymbolicVariable(*this);
    }
};

TEST_F(P4FlayTest, SimplificationPreservesSharedExpressionGraph) {
    size_t clones = 0;
    const IR::Expression *expression = new CountedSymbolicVariable(clones);
    // A small DAG with exponentially many paths to the same leaf.
    for (unsigned depth = 0; depth < 12; ++depth) {
        expression = new IR::LAnd(expression, expression);
    }
    EXPECT_EQ(SimplifyExpression::simplify(expression), expression);
    // Each context-independent pass should visit the shared leaf only once.
    EXPECT_LE(clones, 2U);
}

TEST_F(P4FlayTest, BatchSimplificationKeepsBranchAssumptionsSeparate) {
    const auto *condition = new IR::SymbolicVariable(IR::Type_Boolean::get(), "condition"_cs);
    const auto *a = new IR::SymbolicVariable(IR::Type_Bits::get(8), "a"_cs);
    const auto *b = new IR::SymbolicVariable(IR::Type_Bits::get(8), "b"_cs);
    const auto *c = new IR::SymbolicVariable(IR::Type_Bits::get(8), "c"_cs);
    const auto *shared = new IR::Mux(condition, a, b);
    const auto *input = new IR::ListExpression(
        {new IR::Mux(condition, shared, c), new IR::Mux(condition, c, shared)});
    const auto *result = SimplifyExpression::simplify(input)->checkedTo<IR::ListExpression>();
    const IR::Mux first(condition, a, c), second(condition, c, b);
    EXPECT_EQ(result->components.at(0)->structuralCompare(first), std::weak_ordering::equivalent);
    EXPECT_EQ(result->components.at(1)->structuralCompare(second), std::weak_ordering::equivalent);
}

TEST_F(P4FlayTest, ArrayIndexReduction) {
    const auto *elementType = IR::Type_Bits::get(8);
    const auto *arrayType = new IR::Type_Array(elementType, new IR::Constant(2));
    const auto *first = new IR::Constant(elementType, 10);
    const auto *second = new IR::Constant(elementType, 20);
    const auto *array = new IR::ArrayExpression(arrayType, {first, second}, arrayType);
    const auto *index = new IR::ArrayIndex(array, new IR::Constant(1));
    const auto *result = index->apply(ExpressionStrengthReduction());
    EXPECT_EQ(result->structuralCompare(*second), std::weak_ordering::equivalent);
}

TEST_F(P4FlayTest, SymbolicArrayConstruction) {
    Flay::ExecutionState state(new IR::P4Program());
    const auto *arrayType = new IR::Type_Array(IR::Type_Bits::get(8), new IR::Constant(2));
    const auto *result = state.createSymbolicExpression(arrayType, "values"_cs);
    const auto *array = result->to<IR::ArrayExpression>();
    ASSERT_NE(array, nullptr);
    EXPECT_EQ(array->arrayType, arrayType);
    ASSERT_EQ(array->components.size(), 2U);
    const auto *first = array->components.at(0)->checkedTo<IR::DataPlaneVariable>();
    const auto *second = array->components.at(1)->checkedTo<IR::DataPlaneVariable>();
    EXPECT_EQ(first->label, "values[0]"_cs);
    EXPECT_EQ(second->label, "values[1]"_cs);
    EXPECT_EQ(first->type, arrayType->elementType);
    EXPECT_EQ(second->type, arrayType->elementType);
}

}  // anonymous namespace

}  // namespace P4::P4Tools::Test
