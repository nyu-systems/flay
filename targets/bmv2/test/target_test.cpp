#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "backends/p4tools/common/compiler/convert_hs_index.h"
#include "backends/p4tools/common/core/z3_solver.h"
#include "backends/p4tools/common/lib/variables.h"
#include "backends/p4tools/modules/flay/core/interpreter/compiler_result.h"
#include "backends/p4tools/modules/flay/test/helpers.h"
#include "frontends/common/parseInput.h"
#include "frontends/p4/createBuiltins.h"
#include "frontends/p4/typeChecking/typeChecker.h"
#include "lib/error.h"
#include "lib/exceptions.h"

namespace P4::P4Tools::Test {
namespace {

// Exercise target dispatch with the specialized package type that the compiler produces.
TEST(Bmv2ProgramInfo, AcceptsSpecializedV1Switch) {
    auto context = P4FlayTest::SetUp("bmv2", "v1model");
    ASSERT_TRUE(context.has_value());
    const auto *program = parseP4String(R"(
        control C() { apply {} }
        package V1Switch<T>(C p, C v, C i, C e, C c, C d);
        V1Switch<bit<8>>(C(), C(), C(), C(), C(), C()) main;
    )");
    ASSERT_NE(program, nullptr);
    const Flay::FlayCompilerResult result(CompilerResult(*program), *program,
                                          P4RuntimeAPI(nullptr, nullptr), {});
    const auto *info = Flay::FlayTarget::produceProgramInfo(result);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->getPipelineSequence()->size(), 6U);
    EXPECT_EQ(errorCount(), 0U);
}

TEST(Bmv2ProgramInfo, RejectsUnsupportedPackages) {
    for (const auto &source : {
             "package Other<T>(); Other<bit<8>>() main;",
             "package V1model(); V1model() main;",
             "package V1Switch(); V1Switch() main;",
         }) {
        SCOPED_TRACE(source);
        auto context = P4FlayTest::SetUp("bmv2", "v1model");
        ASSERT_TRUE(context.has_value());
        const auto *program = parseP4String(source);
        ASSERT_NE(program, nullptr);
        const CompilerResult result(*program);
        EXPECT_EQ(Flay::FlayTarget::produceProgramInfo(result), nullptr);
        EXPECT_EQ(errorCount(), 1U);
    }
}

TEST(Bmv2ProgramInfo, RejectsWrongBlockCount) {
    auto context = P4FlayTest::SetUp("bmv2", "v1model");
    ASSERT_TRUE(context.has_value());
    const auto *program = parseP4String(R"(
        control C() { apply {} }
        package V1Switch<T>(C p);
        V1Switch<bit<8>>(C()) main;
    )");
    ASSERT_NE(program, nullptr);
    const CompilerResult result(*program);
    EXPECT_EQ(Flay::FlayTarget::produceProgramInfo(result), nullptr);
    EXPECT_EQ(errorCount(), 1U);
}

// Exercise the common method resolver through typed control statements. Symbolic fields
// and validity bits make accidental concretization and overlapping-copy bugs visible.
void checkStackShift(const std::string &method, const std::string &count, unsigned shift,
                     bool memberReference) {
    auto context = P4FlayTest::SetUp("bmv2", "v1model");
    ASSERT_TRUE(context.has_value());
    const std::string receiver = memberReference ? "h.stack" : "stack";
    const auto *program = parseP4String(R"(
        header H { bit<8> tag; bit<16> value; }
        struct Headers { H[3] stack; }
        control C() {
            Headers h;
            H[3] stack;
            apply {
    )" + receiver + "." + method + "(" + count +
                                        R"();
            }
        }
        control Control();
        package V1Switch<T>(Control p, Control v, Control i, Control e, Control c, Control d);
        V1Switch<Headers>(C(), C(), C(), C(), C(), C()) main;
    )");
    ASSERT_NE(program, nullptr);
    P4::TypeMap typeMap;
    program = program->apply(P4::TypeInference(&typeMap, false));
    program = program->apply(P4::ApplyTypesToExpressions(&typeMap));
    ASSERT_EQ(errorCount(), 0U);
    const Flay::FlayCompilerResult compilerResult(CompilerResult(*program), *program,
                                                  P4RuntimeAPI(nullptr, nullptr), {});
    const auto *info = Flay::FlayTarget::produceProgramInfo(compilerResult);
    ASSERT_NE(info, nullptr);
    const auto *control = info->getPipelineSequence()->front()->checkedTo<IR::P4Control>();
    const auto *call = control->body->components.at(0)->checkedTo<IR::MethodCallStatement>();
    const auto *stack = call->methodCall->method->checkedTo<IR::Member>()->expr;
    const auto *stackType = stack->type->checkedTo<IR::Type_Array>();
    Flay::ExecutionState state(program);
    const auto *elementType = state.resolveType(stackType->elementType);
    std::vector<const IR::Expression *> elements;
    for (unsigned index = 0; index < 3; ++index) {
        const auto *element = HSIndexToMember::produceStackIndex(elementType, stack, index);
        const auto *value =
            state.createSymbolicExpression(elementType, "stack_" + std::to_string(index));
        state.assignStructLike(element, value);
        elements.push_back(state.get(element));
    }
    Flay::ControlPlaneConstraints constraints;
    auto &stepper = Flay::FlayTarget::getStepper(*info, constraints, state);
    control->body->apply(stepper);
    for (unsigned index = 0; index < 3; ++index) {
        SCOPED_TRACE(index);
        const auto *element = HSIndexToMember::produceStackIndex(elementType, stack, index);
        const bool isPush = method == "push_front";
        if (isPush ? index >= shift : index < 3 - shift) {
            const auto source = isPush ? index - shift : index + shift;
            EXPECT_TRUE(state.get(element)->equiv(*elements.at(source)));
        } else {
            const auto *validity = state.get(ToolsVariables::getHeaderValidity(element));
            ASSERT_TRUE(validity->is<IR::BoolLiteral>());
            EXPECT_FALSE(validity->checkedTo<IR::BoolLiteral>()->value);
        }
    }
    EXPECT_EQ(errorCount(), 0U);
}

TEST(FlayHeaderStack, PushPreservesSymbolicFieldsAndValidity) {
    for (bool member : {false, true}) {
        SCOPED_TRACE(member);
        checkStackShift("push_front", "1", 1, member);
        checkStackShift("push_front", "2", 2, member);
    }
}

TEST(FlayHeaderStack, PopPreservesSymbolicFieldsAndValidity) {
    for (bool member : {false, true}) {
        SCOPED_TRACE(member);
        checkStackShift("pop_front", "1", 1, member);
        checkStackShift("pop_front", "2", 2, member);
    }
}

TEST(FlayHeaderStack, ZeroCountPreservesStack) {
    checkStackShift("push_front", "0", 0, true);
    checkStackShift("pop_front", "0", 0, false);
}

TEST(FlayHeaderStack, FullAndOversizedCountsInvalidateAllElements) {
    for (const auto *count : {"3", "4", "18446744073709551616"}) {
        SCOPED_TRACE(count);
        checkStackShift("push_front", count, 3, true);
        checkStackShift("pop_front", count, 3, false);
    }
}

TEST(FlayTupleState, ExecutesNestedReadsAndAssignments) {
    auto context = P4FlayTest::SetUp("bmv2", "v1model");
    ASSERT_TRUE(context.has_value());
    const auto *program = parseP4String(R"(
        header H { bit<8> value; }
        typedef tuple<bool, bit<16>> Inner;
        struct Meta { tuple<bit<8>, H[2], Inner> values; }
        control C() {
            Meta m;
            Meta copy;
            tuple<bit<8>, bit<8>> pair;
            apply {
                m.values[0] = 8w7;
                m.values[1][0].setValid();
                m.values[1][0].value = 8w42;
                m.values[2] = {true, 16w513};
                copy = m;
                m.values[1][0].value = 8w99;
                pair = {8w11, 8w22};
                pair = {pair[1], pair[0]};
            }
        }
        control Control();
        package V1Switch<T>(Control p, Control v, Control i, Control e, Control c, Control d);
        V1Switch<Meta>(C(), C(), C(), C(), C(), C()) main;
    )");
    ASSERT_NE(program, nullptr);
    P4::TypeMap typeMap;
    program = program->apply(P4::TypeInference(&typeMap, false));
    program = program->apply(P4::ApplyTypesToExpressions(&typeMap));
    ASSERT_EQ(errorCount(), 0U);
    const Flay::FlayCompilerResult compilerResult(CompilerResult(*program), *program,
                                                  P4RuntimeAPI(nullptr, nullptr), {});
    const auto *info = Flay::FlayTarget::produceProgramInfo(compilerResult);
    ASSERT_NE(info, nullptr);
    const auto *control = info->getPipelineSequence()->front()->checkedTo<IR::P4Control>();
    Flay::ExecutionState state(program);
    for (const auto *decl : control->controlLocals) {
        state.declareVariable(Flay::FlayTarget::get(),
                              *decl->checkedTo<IR::Declaration_Variable>());
    }
    Flay::ControlPlaneConstraints constraints;
    auto &stepper = Flay::FlayTarget::getStepper(*info, constraints, state);
    control->body->apply(stepper);
    const auto *copyDecl = control->controlLocals.getDeclaration<IR::Declaration_Variable>("copy");
    const auto *copy = state.get(new IR::PathExpression(copyDecl->type, new IR::Path("copy")))
                           ->checkedTo<IR::StructExpression>();
    const auto *tuple = copy->getField("values"_cs)->expression->checkedTo<IR::ListExpression>();
    EXPECT_EQ(tuple->components.at(0)->checkedTo<IR::Constant>()->asInt(), 7);
    const auto *headers = tuple->components.at(1)->checkedTo<IR::ArrayExpression>();
    const auto *header = headers->components.at(0)->checkedTo<IR::HeaderExpression>();
    EXPECT_TRUE(header->validity->checkedTo<IR::BoolLiteral>()->value);
    EXPECT_EQ(header->getField("value"_cs)->expression->checkedTo<IR::Constant>()->asInt(), 42);
    EXPECT_FALSE(headers->components.at(1)
                     ->checkedTo<IR::HeaderExpression>()
                     ->validity->checkedTo<IR::BoolLiteral>()
                     ->value);
    const auto *inner = tuple->components.at(2)->checkedTo<IR::ListExpression>();
    EXPECT_TRUE(inner->components.at(0)->checkedTo<IR::BoolLiteral>()->value);
    EXPECT_EQ(inner->components.at(1)->checkedTo<IR::Constant>()->asInt(), 513);
    const auto *pairDecl = control->controlLocals.getDeclaration<IR::Declaration_Variable>("pair");
    const auto *pair = state.get(new IR::PathExpression(pairDecl->type, new IR::Path("pair")))
                           ->checkedTo<IR::ListExpression>();
    EXPECT_EQ(pair->components.at(0)->checkedTo<IR::Constant>()->asInt(), 22);
    EXPECT_EQ(pair->components.at(1)->checkedTo<IR::Constant>()->asInt(), 11);
    EXPECT_EQ(errorCount(), 0U);
}

TEST(FlayTupleState, CreatesSymbolicNestedTuplesForSkippedParsers) {
    auto context = P4FlayTest::SetUp("bmv2", "v1model");
    ASSERT_TRUE(context.has_value());
    const auto *program = parseP4String(R"(
        header H { bit<8> value; }
        struct Meta { tuple<bit<8>, H[2], tuple<bool, bit<16>>> values; }
    )");
    ASSERT_NE(program, nullptr);
    Flay::ExecutionState state(program);
    const auto *type = program->getDeclsByName("Meta"_cs)->single()->checkedTo<IR::Type_Struct>();
    const auto *root = new IR::PathExpression(type, new IR::Path("meta"));
    const auto *symbolic = state.createSymbolicExpression(type, "meta"_cs);
    state.assignStructLike(root, symbolic);
    EXPECT_TRUE(state.get(root)->equiv(*symbolic));
    std::vector<IR::StateVariable> valids;
    const auto fields = state.getFlatFields(root, &valids);
    ASSERT_EQ(fields.size(), 5U);
    ASSERT_EQ(valids.size(), 2U);
    for (const auto &field : fields) EXPECT_TRUE(state.get(field)->is<IR::DataPlaneVariable>());
    for (const auto &valid : valids) EXPECT_TRUE(state.get(valid)->is<IR::DataPlaneVariable>());
}

class CountedParserAssignment : public IR::AssignmentStatement {
    unsigned &visits;

 public:
    CountedParserAssignment(const IR::AssignmentStatement &statement, unsigned &visits)
        : IR::AssignmentStatement(statement), visits(visits) {}

    bool apply_visitor_preorder(Inspector &visitor) const override {
        ++visits;
        return IR::AssignmentStatement::apply_visitor_preorder(visitor);
    }
};

class CountParserAssignments : public Transform {
    unsigned &visits;

 public:
    explicit CountParserAssignments(unsigned &visits) : visits(visits) { forceClone = true; }
    const IR::Node *postorder(IR::AssignmentStatement *statement) override {
        return new CountedParserAssignment(*statement, visits);
    }
};

// Type-check a small in-memory parser without the midend's state-merging passes.
// This preserves the graph shape that the scheduler must handle.
Flay::ExecutionState *executeParser(const std::string &states, unsigned *visits = nullptr,
                                    const IR::Expression *condition = nullptr) {
    const auto *program = parseP4String(R"(
        extern packet_in {}
        struct Headers { bit<8> value; }
        struct Metadata { bit<8> key; bit<8> result; }
        struct Standard { bit<32> parser_error; }
        parser P(packet_in pkt, out Headers h, inout Metadata m, inout Standard sm) {
    )" + states + R"(
        }
        control C() { apply {} }
        parser Parser(packet_in pkt, out Headers h, inout Metadata m, inout Standard sm);
        control Control();
        package V1Switch<H>(Parser p, Control v, Control i, Control e, Control c, Control d);
        V1Switch<Headers>(P(), C(), C(), C(), C(), C()) main;
    )");
    if (program == nullptr) return nullptr;
    P4::TypeMap typeMap;
    program = program->apply(P4::CreateBuiltins());
    program = program->apply(P4::TypeInference(&typeMap, false));
    program = program->apply(P4::ApplyTypesToExpressions(&typeMap));
    if (errorCount() != 0) return nullptr;
    if (visits != nullptr) program = program->apply(CountParserAssignments(*visits));
    const auto *compilerResult = new Flay::FlayCompilerResult(CompilerResult(*program), *program,
                                                              P4RuntimeAPI(nullptr, nullptr), {});
    const auto *info = Flay::FlayTarget::produceProgramInfo(*compilerResult);
    if (info == nullptr) return nullptr;
    auto *state = new Flay::ExecutionState(program);
    Flay::ControlPlaneConstraints constraints;
    auto &stepper = Flay::FlayTarget::getStepper(*info, constraints, *state);
    stepper.initializeState();
    const auto *key = new IR::SymbolicVariable(IR::Type_Bits::get(8), "parser_key"_cs);
    state->set(new IR::Member(key->type, new IR::PathExpression("*meta"), "key"), key);
    if (condition != nullptr) state->pushExecutionCondition(condition);
    info->getPipelineSequence()->front()->apply(stepper);
    return state;
}

void expectParserValue(const IR::Expression *value, int keyValue, int expected) {
    const auto *bits = IR::Type_Bits::get(8);
    const auto *key = new IR::SymbolicVariable(bits, "parser_key"_cs);
    Z3Solver solver;
    auto counterexample =
        solver.checkSat({new IR::LAnd(new IR::Equ(key, IR::Constant::get(bits, keyValue)),
                                      new IR::Neq(value, IR::Constant::get(bits, expected)))});
    ASSERT_TRUE(counterexample.has_value());
    EXPECT_FALSE(*counterexample) << "Incorrect parser output for key " << keyValue;
}

void expectParserResult(const Flay::ExecutionState &state, int keyValue, int expected) {
    const auto *value =
        state.get(new IR::Member(IR::Type_Bits::get(8), new IR::PathExpression("*meta"), "result"));
    expectParserValue(value, keyValue, expected);
}

TEST(FlayParser, JoinsValuesBeforeSharedDestination) {
    auto context = P4FlayTest::SetUp("bmv2", "v1model");
    ASSERT_TRUE(context.has_value());
    const auto *state = executeParser(R"(
        state start { transition select(m.key) { 1: left; default: right; } }
        state left { m.result = 10; transition shared; }
        state right { m.result = 20; transition shared; }
        state shared { m.result = m.result + 1; transition accept; }
    )");
    ASSERT_NE(state, nullptr);
    expectParserResult(*state, 1, 11);
    expectParserResult(*state, 2, 21);
    // The substitution used by specialization must also reflect both incoming paths.
    // Retaining only the first path's annotation could fold this read to a wrong constant.
    unsigned reads = 0;
    for (const auto &[expression, annotation] : state->nodeAnnotationMap().substitutionMap()) {
        const auto *member = expression->to<IR::Member>();
        if (member == nullptr || member->member != "result") continue;
        ++reads;
        expectParserValue(annotation->originalExpression(), 1, 10);
        expectParserValue(annotation->originalExpression(), 2, 20);
    }
    EXPECT_EQ(reads, 1U);
    EXPECT_EQ(errorCount(), 0U);
}

TEST(FlayParser, PreservesSelectPriorityAndImplicitReject) {
    auto context = P4FlayTest::SetUp("bmv2", "v1model");
    ASSERT_TRUE(context.has_value());
    const auto *state = executeParser(R"(
        state start { m.result = 5; transition select(m.key) { 1 .. 3: first; 2 .. 4: second; } }
        state first { m.result = 10; transition accept; }
        state second { m.result = 20; transition accept; }
    )");
    ASSERT_NE(state, nullptr);
    expectParserResult(*state, 2, 10);
    expectParserResult(*state, 4, 20);
    expectParserResult(*state, 5, 5);
    EXPECT_EQ(errorCount(), 0U);
}

TEST(FlayParser, ExecutesSharedDestinationsOnce) {
    auto context = P4FlayTest::SetUp("bmv2", "v1model");
    ASSERT_TRUE(context.has_value());
    // Twelve diamonds have 4096 paths, but only 24 assignment statements.
    constexpr unsigned depth = 12;
    std::string states = "state start { transition fork0; }";
    for (unsigned idx = 0; idx < depth; ++idx) {
        const auto suffix = std::to_string(idx);
        const auto next = idx + 1 == depth ? "accept" : "fork" + std::to_string(idx + 1);
        states += "state fork" + suffix + " { transition select(m.key) { " + suffix + ": left" +
                  suffix + "; default: right" + suffix + "; } }";
        states += "state left" + suffix + " { m.result = 10; transition " + next + "; }";
        states += "state right" + suffix + " { m.result = 20; transition " + next + "; }";
    }
    unsigned visits = 0;
    const auto *state = executeParser(states, &visits);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(visits, 2 * depth);
    expectParserResult(*state, depth - 1, 10);
    expectParserResult(*state, 250, 20);
}

TEST(FlayParser, PreservesCallerConditionAcrossAcceptAndReject) {
    auto context = P4FlayTest::SetUp("bmv2", "v1model");
    ASSERT_TRUE(context.has_value());
    const auto *bits = IR::Type_Bits::get(8);
    const auto *condition =
        new IR::Lss(new IR::SymbolicVariable(bits, "parser_key"_cs), IR::Constant::get(bits, 10));
    const auto *state = executeParser(R"(
        state start { transition select(m.key) { 1: left; default: right; } }
        state left { m.result = 11; transition accept; }
        state right { m.result = 22; transition reject; }
    )",
                                      nullptr, condition);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->getExecutionCondition(), condition);
    expectParserResult(*state, 1, 11);
    expectParserResult(*state, 2, 22);
    expectParserResult(*state, 10, 0);
}

TEST(FlayParser, RejectsCyclesThroughDefaultTransitions) {
    auto context = P4FlayTest::SetUp("bmv2", "v1model");
    ASSERT_TRUE(context.has_value());
    EXPECT_THROW(executeParser(R"(
        state start { transition select(m.key) { 1: accept; default: again; } }
        state again { transition start; }
    )"),
                 Util::CompilerUnimplemented);
}

}  // namespace

}  // namespace P4::P4Tools::Test
