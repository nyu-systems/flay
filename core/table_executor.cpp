
#include "backends/p4tools/modules/flay/core/table_executor.h"

#include "backends/p4tools/common/lib/constants.h"
#include "backends/p4tools/common/lib/table_utils.h"
#include "backends/p4tools/common/lib/variables.h"
#include "backends/p4tools/modules/flay/core/expression_resolver.h"
#include "backends/p4tools/modules/flay/core/state_utils.h"
#include "backends/p4tools/modules/flay/core/target.h"
#include "ir/irutils.h"

namespace P4Tools::Flay {

const IR::Type_Bits TableExecutor::ACTION_BIT_TYPE = IR::Type_Bits(8, false);

TableExecutor::TableExecutor(ExpressionResolver &callingResolver) : resolver(callingResolver) {}

const ProgramInfo &TableExecutor::getProgramInfo() const { return resolver.get().getProgramInfo(); }

ExecutionState &TableExecutor::getExecutionState() const {
    return resolver.get().getExecutionState();
}

const IR::Key *TableExecutor::resolveKey(const IR::Key *key) const {
    IR::Vector<IR::KeyElement> keyElements;
    bool hasChanged = false;
    for (const auto *keyField : key->keyElements) {
        const auto *expr = keyField->expression;
        bool keyFieldHasChanged = false;
        if (!SymbolicEnv::isSymbolicValue(expr)) {
            expr->apply(resolver);
            expr = resolver.get().getResult();
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

const IR::Expression *computeTargetMatchType(const IR::P4Table *table, const IR::Key *key) {
    auto tableName = table->controlPlaneName();
    const IR::Expression *hitCondition = IR::getBoolLiteral(true);
    for (const auto *keyField : key->keyElements) {
        const auto *keyExpr = keyField->expression;
        const auto matchType = keyField->matchType->toString();
        const auto *nameAnnot = keyField->getAnnotation("name");
        bool isTainted = false;
        // Some hidden tables do not have any key name annotations.
        BUG_CHECK(nameAnnot != nullptr /* || properties.tableIsImmutable*/,
                  "Non-constant table key without an annotation");
        cstring fieldName;
        if (nameAnnot != nullptr) {
            fieldName = nameAnnot->getName();
        }
        // Create a new variable constant that corresponds to the key expression.
        cstring keyName = tableName + "_key_" + fieldName;
        const auto *ctrlPlaneKey = ToolsVariables::getSymbolicVariable(keyExpr->type, 0, keyName);

        if (matchType == P4Constants::MATCH_KIND_EXACT) {
            hitCondition = new IR::LAnd(hitCondition, new IR::Equ(keyExpr, ctrlPlaneKey));
        } else if (matchType == P4Constants::MATCH_KIND_TERNARY) {
            cstring maskName = tableName + "_mask_" + fieldName;
            const IR::Expression *ternaryMask = nullptr;
            // We can recover from taint by inserting a ternary match that is 0.
            if (isTainted) {
                ternaryMask = IR::getConstant(keyExpr->type, 0);
                keyExpr = ternaryMask;
            } else {
                ternaryMask = ToolsVariables::getSymbolicVariable(keyExpr->type, 0, maskName);
            }
            hitCondition =
                new IR::LAnd(hitCondition, new IR::Equ(new IR::BAnd(keyExpr, ternaryMask),
                                                       new IR::BAnd(ctrlPlaneKey, ternaryMask)));
        } else if (matchType == P4Constants::MATCH_KIND_LPM) {
            const auto *keyType = keyExpr->type->checkedTo<IR::Type_Bits>();
            auto keyWidth = keyType->width_bits();
            cstring maskName = tableName + "_lpm_prefix_" + fieldName;
            const IR::Expression *maskVar =
                ToolsVariables::getSymbolicVariable(keyExpr->type, 0, maskName);
            // The maxReturn is the maximum vale for the given bit width. This value is shifted by
            // the mask variable to create a mask (and with that, a prefix).
            auto maxReturn = IR::getMaxBvVal(keyWidth);
            auto *prefix = new IR::Sub(IR::getConstant(keyType, keyWidth), maskVar);
            const IR::Expression *lpmMask = nullptr;
            // We can recover from taint by inserting a ternary match that is 0.
            if (isTainted) {
                lpmMask = IR::getConstant(keyExpr->type, 0);
                maskVar = lpmMask;
                keyExpr = lpmMask;
            } else {
                lpmMask = new IR::Shl(IR::getConstant(keyType, maxReturn), prefix);
            }
            hitCondition = new IR::LAnd(
                hitCondition,
                new IR::LAnd(
                    // This is the actual LPM match under the shifted mask (the prefix).
                    new IR::Leq(maskVar, IR::getConstant(keyType, keyWidth)),
                    // The mask variable shift should not be larger than the key width.
                    new IR::Equ(new IR::BAnd(keyExpr, lpmMask),
                                new IR::BAnd(ctrlPlaneKey, lpmMask))));
        } else {
            P4C_UNIMPLEMENTED("Match type %s not implemented for table keys.", matchType);
        }
    }
    return hitCondition;
}

void TableExecutor::processDefaultAction(const IR::P4Table *table) const {
    auto &state = getExecutionState();
    const auto *defaultAction = table->getDefaultAction();
    const auto *tableAction = defaultAction->checkedTo<IR::MethodCallExpression>();
    const auto *actionType = StateUtils::getP4Action(state, tableAction);

    auto &actionStepper = FlayTarget::getStepper(getProgramInfo(), state);
    // Synthesize arguments for the call based on the action parameters.
    const auto &parameters = actionType->parameters;
    const auto *arguments = tableAction->arguments;
    BUG_CHECK(
        arguments->size() == parameters->parameters.size(),
        "Method call does not have the same number of arguments as the action has parameters.");
    for (size_t argIdx = 0; argIdx < parameters->size(); ++argIdx) {
        const auto *parameter = parameters->getParameter(argIdx);
        const auto *paramType = state.resolveType(parameter->type);
        // Synthesize a variable constant here that corresponds to a control plane argument.
        // We get the unique name of the table coupled with the unique name of the action.
        // Getting the unique name is needed to avoid generating duplicate arguments.
        const auto *actionArg = arguments->at(argIdx)->expression;
        const auto *paramRef = new IR::PathExpression(paramType, new IR::Path(parameter->name));
        state.set(paramRef, actionArg);
    }
    actionType->body->apply(actionStepper);
}

void TableExecutor::processTableActionOptions(const IR::P4Table *table,
                                              const IR::SymbolicVariable *tableActionID,
                                              const IR::Key *key) const {
    auto tableActionList = TableUtils::buildTableActionList(table);
    auto &state = getExecutionState();

    for (size_t actionIdx = 0; actionIdx < tableActionList.size(); ++actionIdx) {
        const auto *action = tableActionList.at(actionIdx);
        const auto *actionType = StateUtils::getP4Action(state, action->expression);

        // First, we compute the hit condition to trigger this particular action call.
        const auto *hitCondition = computeTargetMatchType(table, key);
        hitCondition = new IR::LAnd(
            hitCondition, new IR::Equ(tableActionID, IR::getConstant(&ACTION_BIT_TYPE, actionIdx)));
        // We get the control plane name of the action we are calling.
        cstring actionName = actionType->controlPlaneName();
        // Synthesize arguments for the call based on the action parameters.
        const auto &parameters = actionType->parameters;
        auto &actionState = state.clone();
        for (size_t argIdx = 0; argIdx < parameters->size(); ++argIdx) {
            const auto *parameter = parameters->getParameter(argIdx);
            const auto *paramType = state.resolveType(parameter->type);
            // Synthesize a variable constant here that corresponds to a control plane argument.
            // We get the unique name of the table coupled with the unique name of the action.
            // Getting the unique name is needed to avoid generating duplicate arguments.
            cstring paramName =
                table->controlPlaneName() + "_" + actionName + "_" + parameter->controlPlaneName();
            const auto &actionArg =
                ToolsVariables::getSymbolicVariable(parameter->type, 0, paramName);
            const auto *paramRef = new IR::PathExpression(paramType, new IR::Path(parameter->name));
            actionState.set(paramRef, actionArg);
        }
        auto &actionStepper = FlayTarget::getStepper(getProgramInfo(), actionState);
        actionType->body->apply(actionStepper);
        state.merge(actionState.getSymbolicEnv(), hitCondition);
    }
}

const IR::Expression *TableExecutor::processTable(const IR::P4Table *table) {
    const auto tableName = table->controlPlaneName();
    const auto actionVar = tableName + "_action";
    const auto *tableActionID = ToolsVariables::getSymbolicVariable(&ACTION_BIT_TYPE, 0, actionVar);

    // First, resolve the key.
    const auto *key = table->getKey();
    if (key == nullptr) {
        return new IR::BoolLiteral(false);
    }
    key = resolveKey(key);

    // Execute the default action.
    processDefaultAction(table);

    // Execute all other possible action options.
    processTableActionOptions(table, tableActionID, key);
    return new IR::BoolLiteral(false);
}

}  // namespace P4Tools::Flay