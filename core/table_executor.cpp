
#include "backends/p4tools/modules/flay/core/table_executor.h"

#include <cstddef>
#include <cstdio>

#include <boost/multiprecision/cpp_int.hpp>

#include "backends/p4tools/common/control_plane/symbolic_variables.h"
#include "backends/p4tools/common/lib/constants.h"
#include "backends/p4tools/common/lib/symbolic_env.h"
#include "backends/p4tools/common/lib/table_utils.h"
#include "backends/p4tools/modules/flay/core/expression_resolver.h"
#include "backends/p4tools/modules/flay/core/simplify_expression.h"
#include "backends/p4tools/modules/flay/core/target.h"
#include "ir/id.h"
#include "ir/indexed_vector.h"
#include "ir/irutils.h"
#include "ir/vector.h"
#include "lib/cstring.h"
#include "lib/exceptions.h"

namespace P4Tools::Flay {

namespace {

// Synthesize a list of variables which correspond to a control plane argument.
// We get the unique name of the table coupled with the unique name of the action.
// Getting the unique name is needed to avoid generating duplicate arguments.
IR::Vector<IR::Argument> createActionCallArguments(cstring tableName, cstring actionName,
                                                   const IR::ParameterList &parameters) {
    IR::Vector<IR::Argument> arguments;
    for (const auto *parameter : parameters.parameters) {
        const IR::Expression *actionArg = nullptr;
        // TODO: This boolean cast hack is only necessary because the P4Info does not contain
        // type information. Is there any way we can simplify this?
        if (parameter->type->is<IR::Type_Boolean>()) {
            actionArg = ControlPlaneState::getTableActionArgument(
                tableName, actionName, parameter->controlPlaneName(), IR::getBitType(1));
            actionArg = new IR::Equ(actionArg, new IR::Constant(IR::getBitType(1), 1));
        } else {
            actionArg = ControlPlaneState::getTableActionArgument(
                tableName, actionName, parameter->controlPlaneName(), parameter->type);
        }
        arguments.push_back(new IR::Argument(actionArg));
    }
    return arguments;
}

}  // namespace

TableExecutor::TableExecutor(const IR::P4Table &table, ExpressionResolver &callingResolver)
    : table(table), resolver(callingResolver) {}

const ProgramInfo &TableExecutor::getProgramInfo() const { return resolver.get().getProgramInfo(); }

ExecutionState &TableExecutor::getExecutionState() const {
    return resolver.get().getExecutionState();
}

const IR::P4Table &TableExecutor::getP4Table() const { return table; }

const IR::Key *TableExecutor::resolveKey(const IR::Key *key) const {
    IR::Vector<IR::KeyElement> keyElements;
    bool hasChanged = false;
    for (const auto *keyField : key->keyElements) {
        const auto *expr = keyField->expression;
        bool keyFieldHasChanged = false;
        if (!SymbolicEnv::isSymbolicValue(expr)) {
            expr = resolver.get().computeResult(expr);
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

const IR::Expression *TableExecutor::computeKey(const IR::Key *key) const {
    if (key->keyElements.empty()) {
        return IR::getBoolLiteral(false);
    }
    const IR::Expression *hitCondition = nullptr;
    for (const auto *keyField : key->keyElements) {
        const auto *matchExpr = computeTargetMatchType(keyField);
        if (hitCondition == nullptr) {
            hitCondition = matchExpr;
        } else {
            hitCondition = new IR::LAnd(hitCondition, matchExpr);
        }
    }
    return hitCondition;
}

const IR::Expression *TableExecutor::computeTargetMatchType(const IR::KeyElement *keyField) const {
    auto tableName = getP4Table().controlPlaneName();
    const auto *keyExpr = keyField->expression;
    const auto matchType = keyField->matchType->toString();
    const auto *nameAnnot = keyField->getAnnotation("name");
    // Some hidden tables do not have any key name annotations.
    BUG_CHECK(nameAnnot != nullptr /* || properties.tableIsImmutable*/,
              "Non-constant table key without an annotation");
    cstring fieldName;
    if (nameAnnot != nullptr) {
        fieldName = nameAnnot->getName();
    }
    // Create a new variable constant that corresponds to the key expression.
    const auto *ctrlPlaneKey = ControlPlaneState::getTableKey(tableName, fieldName, keyExpr->type);

    if (matchType == P4Constants::MATCH_KIND_EXACT) {
        return new IR::Equ(keyExpr, ctrlPlaneKey);
    }
    if (matchType == P4Constants::MATCH_KIND_TERNARY) {
        const IR::Expression *ternaryMask = nullptr;
        ternaryMask = ControlPlaneState::getTableTernaryMask(tableName, fieldName, keyExpr->type);
        return new IR::Equ(new IR::BAnd(keyExpr, ternaryMask),
                           new IR::BAnd(ctrlPlaneKey, ternaryMask));
    }
    if (matchType == P4Constants::MATCH_KIND_LPM) {
        const auto *keyType = keyExpr->type->checkedTo<IR::Type_Bits>();
        auto keyWidth = keyType->width_bits();
        const IR::Expression *maskVar =
            ControlPlaneState::getTableMatchLpmPrefix(tableName, fieldName, keyExpr->type);
        // The maxReturn is the maximum vale for the given bit width. This value is shifted by
        // the mask variable to create a mask (and with that, a prefix).
        auto maxReturn = IR::getMaxBvVal(keyWidth);
        auto *prefix = new IR::Sub(IR::getConstant(keyType, keyWidth), maskVar);
        const IR::Expression *lpmMask = nullptr;
        lpmMask = new IR::Shl(IR::getConstant(keyType, maxReturn), prefix);
        return new IR::LAnd(
            // This is the actual LPM match under the shifted mask (the prefix).
            new IR::Leq(maskVar, IR::getConstant(keyType, keyWidth)),
            // The mask variable shift should not be larger than the key width.
            new IR::Equ(new IR::BAnd(keyExpr, lpmMask), new IR::BAnd(ctrlPlaneKey, lpmMask)));
    }
    P4C_UNIMPLEMENTED("Match type %s not implemented for table keys.", matchType);
}

void TableExecutor::callAction(const ProgramInfo &programInfo, ExecutionState &state,
                               const IR::P4Action *actionType,
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
    auto &actionStepper = FlayTarget::getStepper(programInfo, state);
    actionType->body->apply(actionStepper);
}

void TableExecutor::processDefaultAction(const TableUtils::TableProperties &tableProperties,
                                         ReturnProperties &tableReturnProperties) const {
    auto &state = getExecutionState();
    auto table = getP4Table();
    const auto *defaultAction = getP4Table().getDefaultAction();
    const auto *tableAction = defaultAction->checkedTo<IR::MethodCallExpression>();
    const auto *defaultActionType = state.getP4Action(tableAction);

    // Synthesize arguments for the call based on the action parameters.
    const auto *arguments = tableAction->arguments;
    callAction(getProgramInfo(), state, defaultActionType, *arguments);

    if (tableProperties.defaultIsImmutable) {
        return;
    }
    // If the default action is not immutable, it is possible to change it to any other action
    // present in the table.
    auto tableActionList = TableUtils::buildTableActionList(table);
    for (const auto *action : tableActionList) {
        const auto *actionType =
            state.getP4Action(action->expression->checkedTo<IR::MethodCallExpression>());
        // Skip the current initial default action from this calculation to avoid duplicating state.
        if (defaultActionType->controlPlaneName() == actionType->controlPlaneName() ||
            (action->getAnnotation("tableonly") != nullptr)) {
            continue;
        }
        const auto *actionExpr = IR::getStringLiteral(actionType->controlPlaneName());
        auto *actionHitCondition = new IR::Equ(
            actionExpr, ControlPlaneState::getDefaultActionVariable(table.controlPlaneName()));
        // We use action->controlPlaneName() here, NOT actionType. TODO: Clean this up?
        tableReturnProperties.actionRun = SimplifyExpression::produceSimplifiedMux(
            actionHitCondition, actionExpr, tableReturnProperties.actionRun);
        // We get the control plane name of the action we are calling.
        cstring actionName = actionType->controlPlaneName();
        // Synthesize arguments for the call based on the action parameters.
        const auto &parameters = actionType->parameters;
        auto &actionState = state.clone();
        actionState.pushExecutionCondition(actionHitCondition);
        auto arguments =
            createActionCallArguments(table.controlPlaneName(), actionName, *parameters);
        state.addReachabilityMapping(action, actionHitCondition);
        callAction(getProgramInfo(), actionState, actionType, arguments);
        // Finally, merge in the state of the action call.
        state.merge(actionState);
    }
}

void TableExecutor::processTableActionOptions(ReturnProperties &tableReturnProperties) const {
    auto table = getP4Table();
    auto tableActionList = TableUtils::buildTableActionList(table);
    auto &state = getExecutionState();
    const auto *tableActionID = ControlPlaneState::getTableActionChoice(table.controlPlaneName());

    for (const auto *action : tableActionList) {
        const auto *actionType =
            state.getP4Action(action->expression->checkedTo<IR::MethodCallExpression>());
        auto *actionChoice =
            new IR::Equ(tableActionID, IR::getStringLiteral(actionType->controlPlaneName()));
        const auto *actionHitCondition =
            new IR::LAnd(tableReturnProperties.totalHitCondition, actionChoice);
        // We use action->controlPlaneName() here, NOT actionType. TODO: Clean this up?
        tableReturnProperties.actionRun = SimplifyExpression::produceSimplifiedMux(
            actionHitCondition, IR::getStringLiteral(action->controlPlaneName()),
            tableReturnProperties.actionRun);
        // We get the control plane name of the action we are calling.
        cstring actionName = actionType->controlPlaneName();
        // Synthesize arguments for the call based on the action parameters.
        const auto &parameters = actionType->parameters;
        auto &actionState = state.clone();
        actionState.pushExecutionCondition(actionHitCondition);
        auto arguments =
            createActionCallArguments(table.controlPlaneName(), actionName, *parameters);
        state.addReachabilityMapping(action, actionHitCondition);
        callAction(getProgramInfo(), actionState, actionType, arguments);
        // Finally, merge in the state of the action call.
        state.merge(actionState);
    }
}

void TableExecutor::processConstantTableEntries(const IR::Key &key,
                                                ReturnProperties &tableReturnProperties) const {
    auto table = getP4Table();
    auto tableActionList = TableUtils::buildTableActionList(table);
    auto &state = getExecutionState();

    const auto *entries = table.getEntries();

    // Sometimes, there are no entries. Just return.
    if (entries == nullptr) {
        return;
    }

    auto entryVector = entries->entries;

    // Sort entries if one of the keys contains an LPM match.
    for (size_t idx = 0; idx < key.keyElements.size(); ++idx) {
        const auto *keyElement = key.keyElements.at(idx);
        if (keyElement->matchType->path->toString() == P4Constants::MATCH_KIND_LPM) {
            std::sort(entryVector.begin(), entryVector.end(), [idx](auto &&PH1, auto &&PH2) {
                return TableUtils::compareLPMEntries(std::forward<decltype(PH1)>(PH1),
                                                     std::forward<decltype(PH2)>(PH2), idx);
            });
            break;
        }
    }
    for (const auto *entry : entryVector) {
        // First, compute the condition to match on the table entry.
        const auto *hitCondition = TableUtils::computeEntryMatch(table, *entry, key);

        // Once we have computed the match, execution the action with its arguments.
        const auto *action = entry->getAction();
        const auto *actionCall = action->checkedTo<IR::MethodCallExpression>();
        const auto *actionType = state.getP4Action(actionCall);
        auto &actionState = state.clone();
        actionState.pushExecutionCondition(hitCondition);
        callAction(getProgramInfo(), actionState, actionType, *actionCall->arguments);
        // Finally, merge in the state of the action call.
        // We can only match if other entries have not previously matched!
        const auto *entryHitCondition =
            new IR::LAnd(hitCondition, new IR::LNot(tableReturnProperties.totalHitCondition));
        state.merge(actionState);
        tableReturnProperties.totalHitCondition =
            new IR::LOr(tableReturnProperties.totalHitCondition, entryHitCondition);
        // We use controlPlaneName() here. TODO: Clean this up?
        tableReturnProperties.actionRun = SimplifyExpression::produceSimplifiedMux(
            entryHitCondition,
            IR::getStringLiteral(
                actionCall->method->checkedTo<IR::PathExpression>()->path->toString()),
            tableReturnProperties.actionRun);
    }
}

const IR::Expression *TableExecutor::processTable() {
    const auto tableName = getP4Table().controlPlaneName();
    TableUtils::TableProperties properties;
    TableUtils::checkTableImmutability(table, properties);

    // Then, resolve the key.
    const auto *key = getP4Table().getKey();
    if (key == nullptr) {
        auto tableActionList = TableUtils::buildTableActionList(table);
        for (const auto *action : tableActionList) {
            getExecutionState().addReachabilityMapping(action, IR::getBoolLiteral(false));
        }
        const auto *actionPath = TableUtils::getDefaultActionName(getP4Table());
        return new IR::StructExpression(
            nullptr,
            {new IR::NamedExpression("hit", IR::getBoolLiteral(false)),
             new IR::NamedExpression("miss", IR::getBoolLiteral(true)),
             new IR::NamedExpression("action_run", IR::getStringLiteral(actionPath->toString())),
             new IR::NamedExpression("table_name", IR::getStringLiteral(tableName))});
    }
    key = resolveKey(key);

    const auto *actionPath = TableUtils::getDefaultActionName(table);
    /// Pay attention to how we use "toString" for the path name here.
    /// We need to match these choices correctly. TODO: Make this very explicit.
    const auto *hitCondition =
        new IR::LAnd(ControlPlaneState::getTableActive(tableName), computeKey(key));
    ReturnProperties tableReturnProperties{hitCondition,
                                           IR::getStringLiteral(actionPath->path->toString())};

    // First, execute the default action.
    processDefaultAction(properties, tableReturnProperties);

    // If the table is immutable, we can not add control-plane entries.
    // We can only execute pre-existing entries.
    if (properties.tableIsImmutable) {
        processConstantTableEntries(*key, tableReturnProperties);
        return new IR::StructExpression(
            nullptr,
            {new IR::NamedExpression("hit", tableReturnProperties.totalHitCondition),
             new IR::NamedExpression("miss", new IR::LNot(tableReturnProperties.totalHitCondition)),
             new IR::NamedExpression("action_run", tableReturnProperties.actionRun),
             new IR::NamedExpression("table_name", IR::getStringLiteral(tableName))});
    }

    // Execute all other possible action options. Get the combination of all possible hits.
    processTableActionOptions(tableReturnProperties);
    return new IR::StructExpression(
        nullptr,
        {new IR::NamedExpression("hit", tableReturnProperties.totalHitCondition),
         new IR::NamedExpression("miss", new IR::LNot(tableReturnProperties.totalHitCondition)),
         new IR::NamedExpression("action_run", tableReturnProperties.actionRun),
         new IR::NamedExpression("table_name", IR::getStringLiteral(tableName))});
}

}  // namespace P4Tools::Flay
