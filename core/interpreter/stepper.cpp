#include "backends/p4tools/modules/flay/core/interpreter/stepper.h"

#include <cstddef>
#include <vector>

#include "backends/p4tools/common/lib/arch_spec.h"
#include "backends/p4tools/common/lib/variables.h"
#include "backends/p4tools/modules/flay/core/interpreter/expression_resolver.h"
#include "backends/p4tools/modules/flay/core/interpreter/parser_stepper.h"
#include "backends/p4tools/modules/flay/core/interpreter/target.h"
#include "backends/p4tools/modules/flay/core/lib/simplify_expression.h"
#include "ir/id.h"
#include "ir/irutils.h"
#include "lib/cstring.h"
#include "lib/exceptions.h"

namespace P4::P4Tools::Flay {

FlayStepper::FlayStepper(const ProgramInfo &programInfo, ControlPlaneConstraints &constraints,
                         ExecutionState &executionState)
    : _programInfo(programInfo),
      _controlPlaneConstraints(constraints),
      _executionState(executionState) {
    visitDagOnce = false;
}

const ProgramInfo &FlayStepper::getProgramInfo() const { return _programInfo; }

ExecutionState &FlayStepper::getExecutionState() const { return _executionState; }

ControlPlaneConstraints &FlayStepper::controlPlaneConstraints() const {
    return _controlPlaneConstraints;
}

bool FlayStepper::preorder(const IR::Node *node) {
    P4C_UNIMPLEMENTED("Node %1% of type %2% not implemented in the core stepper.", node,
                      node->node_type_name());
}

void FlayStepper::initializeParserState(const IR::P4Parser & /*parser*/) {};

void FlayStepper::initializeControlState(const IR::P4Control & /*control*/) {};

/* =============================================================================================
 *  Visitor functions
 * ============================================================================================= */

bool FlayStepper::preorder(const IR::P4Parser *parser) {
    // Delegate execution to the parser stepper.
    ParserStepper parserStepper(*this);
    parser->apply(parserStepper);
    return false;
}

bool FlayStepper::preorder(const IR::P4Control *control) {
    auto &executionState = getExecutionState();
    // Enter the control's namespace.
    executionState.pushNamespace(control);
    auto blockName = control->getName().name;
    auto canonicalName = getProgramInfo().getCanonicalBlockName(blockName);
    const auto *controlParams = control->getApplyParameters();
    const auto *archSpec = FlayTarget::getArchSpec();

    // Copy-in.
    for (size_t paramIdx = 0; paramIdx < controlParams->size(); ++paramIdx) {
        const auto *internalParam = controlParams->getParameter(paramIdx);
        auto externalParamName = archSpec->getParamName(canonicalName, paramIdx);
        executionState.copyIn(FlayTarget::get(), internalParam, externalParamName);
    }

    initializeControlState(*control);

    // Declare local variables.
    for (const auto *decl : control->controlLocals) {
        if (const auto *declVar = decl->to<IR::Declaration_Variable>()) {
            executionState.declareVariable(FlayTarget::get(), *declVar);
        }
    }

    // Step into the actual control body.
    control->body->apply_visitor_preorder(*this);

    // Copy-out.
    for (size_t paramIdx = 0; paramIdx < controlParams->size(); ++paramIdx) {
        const auto *internalParam = controlParams->getParameter(paramIdx);
        auto externalParamName = archSpec->getParamName(canonicalName, paramIdx);
        executionState.copyOut(internalParam, externalParamName);
    }
    executionState.popNamespace();
    return false;
}

bool FlayStepper::preorder(const IR::AssignmentStatement *assign) {
    const auto *right = assign->right;
    const auto &left = ToolsVariables::convertReference(assign->left);
    auto &executionState = getExecutionState();

    const auto *assignType = executionState.resolveType(left->type);

    auto &resolver = createExpressionResolver();
    right = resolver.computeResult(right);

    if (right->is<IR::StructExpression>() || right->is<IR::BaseListExpression>()) {
        executionState.assignStructLike(left, right);
        return false;
    }
    if (assignType->is<IR::Type_Base>()) {
        executionState.set(left, right);
        return false;
    }

    P4C_UNIMPLEMENTED("Unsupported assignment type %1% of type %2% from %3%", assignType,
                      assignType->node_type_name(), right);
}

bool FlayStepper::preorder(const IR::EmptyStatement * /*emptyStatement*/) {
    // This is a no-op
    return false;
}

bool FlayStepper::preorder(const IR::BlockStatement *block) {
    auto &executionState = getExecutionState();

    // Enter the block's namespace.
    executionState.pushNamespace(block);
    for (const auto *declOrStmt : block->components) {
        declOrStmt->apply_visitor_preorder(*this);
    }
    executionState.popNamespace();
    return false;
}

bool FlayStepper::preorder(const IR::IfStatement *ifStatement) {
    auto &executionState = getExecutionState();

    const auto *cond = ifStatement->condition;
    auto &resolver = createExpressionResolver();
    cond = resolver.computeResult(cond);

    // Add the node to the reachability map.
    executionState.addReachabilityMapping(ifStatement, cond);

    // Execute the case where the condition is true.
    auto &trueState = executionState.clone();
    trueState.pushExecutionCondition(cond);
    auto &trueStepper =
        FlayTarget::getStepper(getProgramInfo(), controlPlaneConstraints(), trueState);
    ifStatement->ifTrue->apply(trueStepper);

    // Execute the alternative.
    if (ifStatement->ifFalse != nullptr) {
        ifStatement->ifFalse->apply_visitor_preorder(*this);
    }
    // Merge.
    executionState.merge(trueState);
    return false;
}

bool FlayStepper::preorder(const IR::SwitchStatement *switchStatement) {
    // Check whether this is a table switch-case first.
    auto tableMode = false;
    if (const auto *member = switchStatement->expression->to<IR::Member>()) {
        if (member->expr->is<IR::MethodCallExpression>() &&
            member->member.name == IR::Type_Table::action_run) {
            tableMode = true;
        }
    }

    auto &executionState = getExecutionState();
    // Resolve the switch match expression.
    auto &resolver = createExpressionResolver();
    const auto *switchExpr = resolver.computeResult(switchStatement->expression);

    const IR::Expression *cond = nullptr;
    std::vector<const IR::SwitchCase *> accumulatedSwitchCases;
    std::vector<std::reference_wrapper<const ExecutionState>> accumulatedStates;
    for (const auto *switchCase : switchStatement->cases) {
        // The default label must be last. Always break here.
        // We handle the default case separately.
        if (switchCase->label->is<IR::DefaultExpression>()) {
            break;
        }
        const auto *switchCaseLabel = switchCase->label;
        // In table mode, we are actually comparing string expressions.
        if (tableMode) {
            const auto *path = switchCaseLabel->checkedTo<IR::PathExpression>();
            switchCaseLabel = IR::StringLiteral::get(path->path->toString());
        }
        const auto *switchCaseEquality = new IR::Equ(switchExpr, switchCaseLabel);
        if (cond == nullptr) {
            cond = switchCaseEquality;
        } else {
            cond = new IR::LOr(cond, switchCaseEquality);
        }
        // We fall through, so add the statements to execute to a list.
        accumulatedSwitchCases.push_back(switchCase);
        // Add the switch case to the reachability map.
        executionState.addReachabilityMapping(switchCase, switchCaseEquality);
        // Nothing to do with this statement. Fall through to the next case.
        if (switchCase->statement == nullptr) {
            continue;
        }

        // If the statement is a block, we do not fall through and terminate execution.
        if (switchCase->statement->is<IR::BlockStatement>()) {
            // If any of the values in the match list hits, execute the switch case block.
            auto &caseState = executionState.clone();
            // The final condition is the accumulated label condition and NOT other conditions that
            // have previously matched.
            caseState.pushExecutionCondition(SimplifyExpression::simplify(cond));
            // Execute the state with the accumulated statements.
            auto &switchStepper =
                FlayTarget::getStepper(getProgramInfo(), controlPlaneConstraints(), caseState);
            for (const auto *accumulatedSwitchCase : accumulatedSwitchCases) {
                if (accumulatedSwitchCase->statement != nullptr) {
                    accumulatedSwitchCase->statement->apply(switchStepper);
                }
            }
            accumulatedSwitchCases.clear();
            // Save the state for later merging.
            accumulatedStates.emplace_back(caseState);
            cond = nullptr;
        }
    }

    // First, run the default label and get the state that would be covered in this case.
    for (const auto *switchCase : switchStatement->cases) {
        if (switchCase->label->is<IR::DefaultExpression>()) {
            switchCase->statement->apply_visitor_preorder(*this);
            break;
        }
    }
    // After, merge all the accumulated state.
    for (auto accumulatedState : accumulatedStates) {
        executionState.merge(accumulatedState);
    }

    return false;
}

bool FlayStepper::preorder(const IR::MethodCallStatement *callStatement) {
    auto &resolver = createExpressionResolver();
    callStatement->methodCall->apply(resolver);
    return false;
}

}  // namespace P4::P4Tools::Flay
