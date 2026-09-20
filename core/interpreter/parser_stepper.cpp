#include "backends/p4tools/modules/flay/core/interpreter/parser_stepper.h"

#include <optional>
#include <ranges>
#include <set>

#include "backends/p4tools/common/lib/arch_spec.h"
#include "backends/p4tools/common/lib/gen_eq.h"
#include "backends/p4tools/modules/flay/core/interpreter/target.h"
#include "ir/declaration.h"
#include "ir/id.h"
#include "ir/indexed_vector.h"
#include "ir/irutils.h"
#include "lib/cstring.h"
#include "lib/exceptions.h"

namespace P4::P4Tools::Flay {

ParserStepper::ParserStepper(FlayStepper &stepper) : stepper(stepper) {}

const ProgramInfo &ParserStepper::getProgramInfo() const { return stepper.get().getProgramInfo(); }

ExecutionState &ParserStepper::getExecutionState() const {
    return stepper.get().getExecutionState();
}

bool ParserStepper::preorder(const IR::Node *node) {
    P4C_UNIMPLEMENTED("Node %1% of type %2% not implemented in the core stepper.", node,
                      node->node_type_name());
}

/* =============================================================================================
 *  Visitor functions
 * ============================================================================================= */

bool ParserStepper::preorder(const IR::P4Parser *parser) {
    auto &executionState = getExecutionState();
    // Enter the parser's namespace.
    auto blockName = parser->getName().name;
    auto canonicalName = getProgramInfo().getCanonicalBlockName(blockName);
    const auto *parserParams = parser->getApplyParameters();
    const auto *archSpec = FlayTarget::getArchSpec();

    // If we skip the parser we just initialize all (In)Out variables with a symbolic expression.
    if (FlayOptions::get().skipParsers()) {
        for (size_t paramIdx = 0; paramIdx < parserParams->size(); ++paramIdx) {
            const auto *internalParam = parserParams->getParameter(paramIdx);
            if (internalParam->direction != IR::Direction::Out &&
                internalParam->direction != IR::Direction::InOut) {
                continue;
            }
            auto variablePrefix =
                parser->controlPlaneName() + "_" + internalParam->controlPlaneName();
            const auto *symbolicExpression =
                executionState.createSymbolicExpression(internalParam->type, variablePrefix);
            auto externalParamName = archSpec->getParamName(canonicalName, paramIdx);
            const auto *externalParamRef =
                new IR::PathExpression(internalParam->type, new IR::Path(externalParamName));
            executionState.assignStructLike(externalParamRef, symbolicExpression);
        }
        return false;
    }

    executionState.pushNamespace(parser);
    // Copy-in.
    for (size_t paramIdx = 0; paramIdx < parserParams->size(); ++paramIdx) {
        const auto *internalParam = parserParams->getParameter(paramIdx);
        auto externalParamName = archSpec->getParamName(canonicalName, paramIdx);
        executionState.copyIn(FlayTarget::get(), internalParam, externalParamName);
    }

    stepper.get().initializeParserState(*parser);

    // Declare local variables.
    for (const auto *decl : parser->parserLocals) {
        if (const auto *declVar = decl->to<IR::Declaration_Variable>()) {
            executionState.declareVariable(FlayTarget::get(), *declVar);
        }
    }

    processParserStates(parser);

    // Copy-out.
    for (size_t paramIdx = 0; paramIdx < parserParams->size(); ++paramIdx) {
        const auto *internalParam = parserParams->getParameter(paramIdx);
        auto externalParamName = archSpec->getParamName(canonicalName, paramIdx);
        executionState.copyOut(internalParam, externalParamName);
    }
    executionState.popNamespace();
    return false;
}

void ParserStepper::processParserStates(const IR::P4Parser *parser) {
    auto &executionState = getExecutionState();
    // Reverse postorder is a topological order for the acyclic parser produced by unrolling.
    // Reject remaining cycles explicitly: joining a loop would require a fixed-point analysis.
    std::vector<const IR::ParserState *> order;
    std::set<const IR::ParserState *> active, visited;
    std::function<void(const IR::ParserState *)> visit = [&](const IR::ParserState *state) {
        if (active.contains(state)) {
            P4C_UNIMPLEMENTED(
                "Parser state %1% was already visited. We currently do not support "
                "parser loops.",
                state->name);
        }
        if (!visited.insert(state).second) return;
        active.insert(state);
        auto visitDestination = [&](const IR::PathExpression *path) {
            visit(executionState.findDecl(path)->checkedTo<IR::ParserState>());
        };
        if (state->name != IR::ParserState::accept && state->name != IR::ParserState::reject) {
            if (const auto *select = state->selectExpression->to<IR::SelectExpression>()) {
                bool hasDefault = false;
                for (const auto *selectCase : select->selectCases) {
                    visitDestination(selectCase->state);
                    if (selectCase->keyset->is<IR::DefaultExpression>()) {
                        hasDefault = true;
                        break;
                    }
                }
                if (!hasDefault) visitDestination(new IR::PathExpression(IR::ParserState::reject));
            } else {
                visitDestination(state->selectExpression->checkedTo<IR::PathExpression>());
            }
        }
        active.erase(state);
        order.push_back(state);
    };
    const auto *startState = parser->states.getDeclaration<IR::ParserState>("start"_cs);
    visit(startState);
    incomingStates.clear();
    parserExitStates.clear();
    enqueue(startState, executionState.clone());
    auto &caller = stepper.get();
    for (const auto *state : std::views::reverse(order)) {
        auto incoming = incomingStates.extract(state);
        BUG_CHECK(!incoming.empty(), "No incoming execution state for %1%", state);
        stepper = FlayTarget::getStepper(caller.getProgramInfo(), caller.controlPlaneConstraints(),
                                         *incoming.mapped());
        state->apply_visitor_preorder(*this);
    }
    stepper = caller;
    // Accept and reject, including implicit rejection, partition all paths through the parser.
    // Their union is the entry condition. Avoid carrying the expanded disjunction of every
    // parser path into all subsequent control expressions.
    BUG_CHECK(!parserExitStates.empty(), "Parser %1% has no exit", parser);
    auto &completed = parserExitStates.front().get().clone();
    for (size_t idx = 1; idx < parserExitStates.size(); ++idx) {
        completed.join(parserExitStates[idx]);
    }
    executionState.merge(completed, executionState.getExecutionCondition());
}

void ParserStepper::processSelectExpression(const IR::SelectExpression *selectExpr) {
    auto &executionState = getExecutionState();
    auto &resolver = stepper.get().createExpressionResolver();
    const auto *selectKeyExpr = resolver.computeResult(selectExpr->select);

    const IR::Expression *notCond = IR::BoolLiteral::get(true);
    for (const auto *selectCase : selectExpr->selectCases) {
        const auto *decl = executionState.findDecl(selectCase->state)->checkedTo<IR::ParserState>();
        if (selectCase->keyset->is<IR::DefaultExpression>()) {
            auto &selectState = executionState.clone();
            selectState.pushExecutionCondition(notCond);
            selectState.popNamespace();
            enqueue(decl, selectState);
            return;
        }
        const IR::Expression *selectCaseMatchExpr = nullptr;

        // We need to handle parser value sets a little differently, because we are converting them
        // into a struct expression.
        // We do not resolve the value set in the expression resolver because resolution is only
        // supported in parser select expressions.
        std::optional<std::string> parserValueSetName = std::nullopt;
        if (selectCase->keyset->type->is<IR::Type_Set>() &&
            selectCase->keyset->is<IR::PathExpression>()) {
            const auto *p4ValueSet =
                executionState.findDecl(selectCase->keyset->checkedTo<IR::PathExpression>())
                    ->checkedTo<IR::P4ValueSet>();
            // TODO: We should make this an explicit symbolic state variable for control plane
            // configurations.
            parserValueSetName = p4ValueSet->controlPlaneName();
            selectCaseMatchExpr = executionState.createSymbolicExpression(
                p4ValueSet->elementType, "pvs_" + parserValueSetName.value());
        } else {
            selectCaseMatchExpr = resolver.computeResult(selectCase->keyset);
        }

        const auto *matchCond = GenEq::equate(selectKeyExpr, selectCaseMatchExpr);
        // If there is a value set it only matches when it is configured.
        if (parserValueSetName.has_value()) {
            matchCond = new IR::LAnd(matchCond, ControlPlaneState::getParserValueSetConfigured(
                                                    parserValueSetName.value()));
        }
        auto &selectState = executionState.clone();
        selectState.pushExecutionCondition(new IR::LAnd(notCond, matchCond));
        notCond = new IR::LAnd(notCond, new IR::LNot(matchCond));
        selectState.popNamespace();
        enqueue(decl, selectState);
    }
    // A select without a matching case implicitly rejects.
    auto &rejectState = executionState.clone();
    rejectState.pushExecutionCondition(notCond);
    const auto *reject = executionState.findDecl(new IR::PathExpression(IR::ParserState::reject))
                             ->checkedTo<IR::ParserState>();
    rejectState.popNamespace();
    enqueue(reject, rejectState);
}

void ParserStepper::enqueue(const IR::ParserState *destination, ExecutionState &state) {
    auto [it, inserted] = incomingStates.emplace(destination, &state);
    if (!inserted) it->second->join(state);
}

bool ParserStepper::preorder(const IR::ParserState *parserState) {
    if (parserState->name == IR::ParserState::accept ||
        parserState->name == IR::ParserState::reject) {
        addParserExitState(getExecutionState());
        return false;
    }

    auto &executionState = getExecutionState();
    // Enter the parser state's namespace.
    executionState.pushNamespace(parserState);
    for (const auto *declOrStmt : parserState->components) {
        declOrStmt->apply_visitor_preorder(stepper);
    }

    const auto *select = parserState->selectExpression;

    if (const auto *selectExpr = select->to<IR::SelectExpression>()) {
        processSelectExpression(selectExpr);
        executionState.popNamespace();
    } else if (const auto *pathExpression = select->to<IR::PathExpression>()) {
        executionState.popNamespace();
        // Forward the state in the parser namespace, after leaving the source state.
        const auto *decl = executionState.findDecl(pathExpression)->checkedTo<IR::ParserState>();
        enqueue(decl, executionState);
    } else {
        P4C_UNIMPLEMENTED("Select expression %1% not implemented for parser states.", select);
    }
    return false;
}

const std::vector<ParserStepper::ParserExitState> &ParserStepper::getParserExitStates() const {
    return parserExitStates;
}

void ParserStepper::addParserExitState(const ExecutionState &state) {
    parserExitStates.emplace_back(state);
}
}  // namespace P4::P4Tools::Flay
