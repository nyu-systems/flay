#include "backends/p4tools/modules/flay/core/interpreter/execution_state.h"

#include <gtest/gtest.h>

#include <vector>

#include "backends/p4tools/modules/flay/test/helpers.h"
#include "frontends/common/parseInput.h"
#include "lib/error.h"

namespace P4::P4Tools::Test {
namespace {

TEST(FlayExecutionState, InitializesTupleContainingHeaderArray) {
    auto context = P4FlayTest::SetUp("bmv2", "v1model");
    ASSERT_TRUE(context.has_value());
    const auto *program = parseP4String(R"(
        header H { bit<8> value; }
        struct Metadata { tuple<H[2]> headers; }
    )");
    ASSERT_NE(program, nullptr);
    const auto *metadata = program->getDeclsByName("Metadata"_cs)->single()->checkedTo<IR::Type>();
    Flay::ExecutionState state(program);
    const auto *variable = new IR::PathExpression(metadata, new IR::Path("meta"));
    state.initializeStructLike(Flay::FlayTarget::get(), variable, false);
    std::vector<IR::StateVariable> validFields;
    const auto fields = state.getFlatFields(variable, &validFields);
    ASSERT_EQ(fields.size(), 2U);
    ASSERT_EQ(validFields.size(), 2U);
    for (const auto &field : fields) {
        EXPECT_EQ(state.get(field)->checkedTo<IR::Constant>()->asInt(), 0);
    }
    for (const auto &valid : validFields) {
        EXPECT_FALSE(state.get(valid)->checkedTo<IR::BoolLiteral>()->value);
    }
    const auto *tuple = state.get(variable)
                            ->checkedTo<IR::StructExpression>()
                            ->getField("headers"_cs)
                            ->expression->checkedTo<IR::ListExpression>();
    ASSERT_EQ(tuple->components.size(), 1U);
    EXPECT_EQ(tuple->components.at(0)->checkedTo<IR::ArrayExpression>()->components.size(), 2U);
    EXPECT_EQ(errorCount(), 0U);
}

}  // namespace
}  // namespace P4::P4Tools::Test
