#include "backends/p4tools/modules/flay/core/interpreter/table_executor.h"

#include <cstddef>
#include <cstdio>

#include <boost/multiprecision/cpp_int.hpp>

#include "backends/p4tools/common/lib/constants.h"
#include "backends/p4tools/common/lib/symbolic_env.h"
#include "backends/p4tools/common/lib/symbolic_variables.h"
#include "backends/p4tools/common/lib/table_utils.h"
#include "backends/p4tools/modules/flay/core/interpreter/expression_resolver.h"
#include "backends/p4tools/modules/flay/core/interpreter/target.h"
#include "backends/p4tools/modules/flay/core/lib/simplify_expression.h"
#include "ir/id.h"
#include "ir/indexed_vector.h"
#include "ir/irutils.h"
#include "ir/vector.h"
#include "lib/cstring.h"
#include "lib/exceptions.h"

namespace P4::P4Tools::Flay {

namespace {

// Synthesize a list of variables which correspond to a control plane argument.
// We get the unique name of the table coupled with the unique name of the action.
// Getting the unique name is needed to avoid generating duplicate arguments.
IR::Vector<IR::Argument> createActionCallArguments(cstring symbolicTablePrefix, cstring actionName,
                                                   const IR::ParameterList &parameters) {
    IR::Vector<IR::Argument> arguments;
    for (const auto *parameter : parameters.parameters) {
        const IR::Expression *actionArg = nullptr;
        // TODO: This boolean cast hack is only necessary because the P4Info does not contain
        // type information. Is there any way we can simplify this?
        if (parameter->type->is<IR::Type_Boolean>()) {
            actionArg = ControlPlaneState::getTableActionArgument(symbolicTablePrefix, actionName,
                                                                  parameter->controlPlaneName(),
                                                                  IR::Type_Bits::get(1));
            actionArg = new IR::Equ(actionArg, new IR::Constant(IR::Type_Bits::get(1), 1));
        } else {
            actionArg = ControlPlaneState::getTableActionArgument(
                symbolicTablePrefix, actionName, parameter->controlPlaneName(), parameter->type);
        }
        arguments.push_back(new IR::Argument(actionArg));
    }
    return arguments;
}

}  // namespace

TableExecutor::TableExecutor(const IR::P4Table &table, ExpressionResolver &callingResolver)
    : _table(table), _resolver(callingResolver) {
    setSymbolicTablePrefix(table.controlPlaneName());
}

ExpressionResolver &TableExecutor::resolver() const { return _resolver; }

void TableExecutor::setSymbolicTablePrefix(cstring name) { _symbolicTablePrefix = name; }

cstring TableExecutor::symbolicTablePrefix() const { return _symbolicTablePrefix; }

const ProgramInfo &TableExecutor::getProgramInfo() const { return resolver().getProgramInfo(); }

ExecutionState &TableExecutor::getExecutionState() const { return resolver().getExecutionState(); }

ControlPlaneConstraints &TableExecutor::controlPlaneConstraints() const {
    return resolver().controlPlaneConstraints();
}

const IR::P4Table &TableExecutor::getP4Table() const { return _table; }

const IR::Key *TableExecutor::resolveKey(const IR::Key *key) const {
    IR::Vector<IR::KeyElement> keyElements;
    bool hasChanged = false;
    for (const auto *keyField : key->keyElements) {
        const auto *expr = keyField->expression;
        bool keyFieldHasChanged = false;
        if (!SymbolicEnv::isSymbolicValue(expr)) {
            expr = resolver().computeResult(expr);
            keyFieldHasChanged = true;
        }
        if (keyFieldHasChanged) {
            auto *newKeyField = keyField->clone();
            newKeyField->expression = expr;
            keyElements.push_back(newKeyField);
            hasChanged = true;
        }
    }
    if (hasChanged) {
        auto *newKey = key->clone();
        newKey->keyElements = keyElements;
        return newKey;
    }
    return key;
}

KeyMap TableExecutor::computeHitCondition(const IR::Key &key) const {
    KeyMap keyMap;
    for (const auto *keyField : key.keyElements) {
        keyMap.push_back(computeTargetMatchType(keyField));
    }
    return keyMap;
}

const TableMatchKey *TableExecutor::computeTargetMatchType(const IR::KeyElement *keyField) const {
    const auto *keyExpression = keyField->expression;
    const auto matchType = keyField->matchType->toString();
    const auto *nameAnnot = keyField->getAnnotation(IR::Annotation::nameAnnotation);
    // Some hidden tables do not have any key name annotations.
    BUG_CHECK(nameAnnot != nullptr /* || properties.tableIsImmutable*/,
              "Non-constant table key without an annotation");
    cstring fieldName;
    if (nameAnnot != nullptr) {
        fieldName = nameAnnot->getName();
    }
    if (matchType == P4Constants::MATCH_KIND_EXACT) {
        return new ExactTableMatchKey(symbolicTablePrefix(), fieldName, keyExpression);
    }
    if (matchType == P4Constants::MATCH_KIND_TERNARY) {
        return new TernaryTableMatchKey(symbolicTablePrefix(), fieldName, keyExpression);
    }
    if (matchType == P4Constants::MATCH_KIND_LPM) {
        return new LpmTableMatchKey(symbolicTablePrefix(), fieldName, keyExpression);
    }
    P4C_UNIMPLEMENTED("Match type %s not implemented for table keys.", matchType);
}

void TableExecutor::callAction(const ProgramInfo &programInfo,
                               ControlPlaneConstraints &controlPlaneConstraints,
                               ExecutionState &state, const IR::P4Action *actionType,
                               const IR::Vector<IR::Argument> &arguments) {
    const auto *parameters = actionType->parameters;
    BUG_CHECK(
        arguments.size() == parameters->parameters.size(),
        "Method call does not have the same number of arguments as the action has parameters.");
    for (size_t argIdx = 0; argIdx < parameters->size(); ++argIdx) {
        const auto *parameter = parameters->getParameter(argIdx);
        const auto *paramType = state.resolveType(parameter->type);
        // Synthesize a variable constant here that corresponds to a control plane argument.
        // We get the unique name of the table coupled with the unique name of the action.
        // Getting the unique name is needed to avoid generating duplicate arguments.
        const auto *actionArg = arguments.at(argIdx)->expression;
        const auto *paramRef = new IR::PathExpression(paramType, new IR::Path(parameter->name));
        state.set(paramRef, actionArg);
    }
    auto &actionStepper = FlayTarget::getStepper(programInfo, controlPlaneConstraints, state);
    actionType->body->apply(actionStepper);
}

void TableExecutor::processDefaultAction() const {
    auto &state = getExecutionState();
    auto table = getP4Table();
    const auto *defaultAction = getP4Table().getDefaultAction();
    const auto *tableAction = defaultAction->checkedTo<IR::MethodCallExpression>();
    const auto *defaultActionType = state.getP4Action(tableAction);

    // Synthesize arguments for the call based on the action parameters.
    const auto *arguments = tableAction->arguments;
    callAction(getProgramInfo(), controlPlaneConstraints(), state, defaultActionType, *arguments);
}

void TableExecutor::processTableActionOptions(
    const TableUtils::TableProperties & /*tableProperties*/, const ExecutionState &referenceState,
    ReturnProperties &tableReturnProperties) const {
    auto table = getP4Table();
    // auto tableActionList = TableUtils::buildTableActionList(table);
    auto &state = getExecutionState();
    const auto *tableActionID = ControlPlaneState::getTableActionChoice(symbolicTablePrefix());
    const auto *tableActive = ControlPlaneState::getTableActive(symbolicTablePrefix());

    const auto *actionList = table.getActionList();
    if (actionList == nullptr) {
        return;
    }

    for (const auto *action : actionList->actionList) {
        /// Only actions not marked @defaultonly can be executed by the control plane.
        if (action->getAnnotation(IR::Annotation::defaultOnlyAnnotation) == nullptr) {
            const auto *actionType =
                state.getP4Action(action->expression->checkedTo<IR::MethodCallExpression>());
            const auto *actionLiteral = IR::StringLiteral::get(actionType->controlPlaneName());
            tableReturnProperties.totalHitCondition = new IR::LOr(
                tableReturnProperties.totalHitCondition, new IR::Equ(tableActionID, actionLiteral));
        }
    }
    tableReturnProperties.totalHitCondition =
        new IR::LAnd(tableReturnProperties.totalHitCondition, tableActive);

    for (const auto *action : actionList->actionList) {
        const auto *actionType =
            state.getP4Action(action->expression->checkedTo<IR::MethodCallExpression>());
        const auto *actionLiteral = IR::StringLiteral::get(actionType->controlPlaneName());

        const IR::Expression *actionHitCondition = nullptr;
        /// Only actions not marked @defaultonly can be executed by the control plane.
        if (action->getAnnotation(IR::Annotation::defaultOnlyAnnotation) == nullptr) {
            actionHitCondition = new IR::Equ(tableActionID, actionLiteral);
        } else {
            // If the action can only be a default action, the only way to execute it is by setting
            // it as default action.
            actionHitCondition = IR::BoolLiteral::get(false);
        }
        // If the default action is not immutable, it is possible to change it to any other
        // action present in the table.
        if (action->getAnnotation(IR::Annotation::tableOnlyAnnotation) == nullptr) {
            actionHitCondition = new IR::LOr(
                actionHitCondition,
                // We only match when the hitcondition is false.
                new IR::LAnd(
                    new IR::LNot(tableReturnProperties.totalHitCondition),
                    new IR::Equ(ControlPlaneState::getDefaultActionVariable(symbolicTablePrefix()),
                                actionLiteral)));
        }
        state.addReachabilityMapping(action, actionHitCondition);
        // We get the control plane name of the action we are calling.
        cstring actionName = actionType->controlPlaneName();
        const auto &parameters = actionType->parameters;
        // IMPORTANT: We clone 'referenceState' here, NOT 'state'.
        auto &actionState = referenceState.clone();
        actionState.pushExecutionCondition(actionHitCondition);
        // Synthesize arguments for the call based on the action parameters.
        auto arguments = createActionCallArguments(symbolicTablePrefix(), actionName, *parameters);
        callAction(getProgramInfo(), controlPlaneConstraints(), actionState, actionType, arguments);
        // Finally, merge in the state of the action call.
        state.merge(actionState);
        // We use action->controlPlaneName() here, NOT actionType. TODO: Clean this up?
        tableReturnProperties.actionRun = SimplifyExpression::produceSimplifiedMux(
            actionHitCondition, IR::StringLiteral::get(action->controlPlaneName()),
            tableReturnProperties.actionRun);
    }
}

const IR::Expression *TableExecutor::processTable() {
    const auto &table = getP4Table();
    TableUtils::TableProperties properties;
    TableUtils::checkTableImmutability(table, properties);

    // Then, resolve the key.
    const auto *key = table.getKey();
    KeyMap tableKeyMap;
    if (key != nullptr) {
        tableKeyMap = computeHitCondition(*resolveKey(key));
    }

    const auto *defaultActionPath = TableUtils::getDefaultActionName(table);
    ReturnProperties tableReturnProperties{
        IR::BoolLiteral::get(false), IR::StringLiteral::get(defaultActionPath->path->toString())};

    const auto &referenceState = getExecutionState().clone();

    // First, execute the default action.
    processDefaultAction();

    // Execute all other possible action options. Get the combination of all possible hits.
    processTableActionOptions(properties, referenceState, tableReturnProperties);
    // Add the computed hit expression of the table to its control plane configuration.
    // We substitute this match later with concrete assignments.
    auto tableControlPlaneItem = controlPlaneConstraints().find(table.controlPlaneName());
    if (tableControlPlaneItem != controlPlaneConstraints().end()) {
        auto *tableControlPlaneConfiguration =
            tableControlPlaneItem->second.get().checkedTo<TableConfiguration>();
        tableControlPlaneConfiguration->setTableKeyMatch(tableKeyMap);
    } else {
        error("Table %s has no control plane configuration", table.controlPlaneName());
    }

    return new IR::StructExpression(
        nullptr,
        {new IR::NamedExpression("hit", tableReturnProperties.totalHitCondition),
         new IR::NamedExpression("miss", new IR::LNot(tableReturnProperties.totalHitCondition)),
         new IR::NamedExpression("action_run", tableReturnProperties.actionRun),
         new IR::NamedExpression("table_name", IR::StringLiteral::get(table.controlPlaneName()))});
}

}  // namespace P4::P4Tools::Flay
