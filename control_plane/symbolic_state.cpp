#include "backends/p4tools/modules/flay/control_plane/symbolic_state.h"

#include <cstdint>
#include <optional>

#include "backends/p4tools/common/control_plane/symbolic_variables.h"
#include "backends/p4tools/common/lib/constants.h"
#include "backends/p4tools/common/lib/table_utils.h"
#include "backends/p4tools/modules/flay/control_plane/control_plane_objects.h"
#include "backends/p4tools/modules/flay/control_plane/util.h"
#include "frontends/common/resolveReferences/referenceMap.h"
#include "ir/irutils.h"
#include "lib/error.h"

namespace P4Tools::Flay {

ControlPlaneStateInitializer::ControlPlaneStateInitializer(const P4::ReferenceMap &refMap)
    : refMap_(refMap) {}

ControlPlaneConstraints ControlPlaneStateInitializer::getDefaultConstraints() const {
    return defaultConstraints;
}

bool ControlPlaneStateInitializer::computeMatch(const IR::Expression &entryKey,
                                                const IR::SymbolicVariable &keySymbol,
                                                cstring tableName, cstring fieldName,
                                                cstring matchType, TableKeySet &keySet) {
    const auto *keyType = keySymbol.type;

    if (matchType == P4Constants::MATCH_KIND_EXACT) {
        ASSIGN_OR_RETURN_WITH_MESSAGE(const auto &exactValue, entryKey.to<IR::Literal>(), false,
                                      ::error("Entry %1% is not a literal.", entryKey));
        keySet.emplace(keySymbol, exactValue);
        return true;
    }

    if (matchType == P4Constants::MATCH_KIND_LPM) {
        const auto *lpmPrefixSymbol =
            ControlPlaneState::getTableMatchLpmPrefix(tableName, fieldName, keyType);
        // TODO: What does default expression mean as a table entry?
        if (entryKey.is<IR::DefaultExpression>()) {
            keySet.emplace(keySymbol, *IR::getConstant(keyType, 0));
            keySet.emplace(*lpmPrefixSymbol, *IR::getConstant(keyType, 0));
            return true;
        }
        if (const auto *maskExpr = entryKey.to<IR::Mask>()) {
            ASSIGN_OR_RETURN_WITH_MESSAGE(
                const auto &maskLeft, maskExpr->left->to<IR::Literal>(), false,
                ::error("Left mask element %1% is not a literal.", maskExpr->left));
            ASSIGN_OR_RETURN_WITH_MESSAGE(
                const auto &maskRight, maskExpr->right->to<IR::Literal>(), false,
                ::error("Right mask element %1% is not a literal.", maskExpr->right));
            keySet.emplace(keySymbol, maskLeft);
            keySet.emplace(*lpmPrefixSymbol, maskRight);
            return true;
        }
        ASSIGN_OR_RETURN_WITH_MESSAGE(const auto &exactValue, entryKey.to<IR::Literal>(), false,
                                      ::error("%1% is not a literal.", entryKey));
        keySet.emplace(keySymbol, exactValue);
        keySet.emplace(*lpmPrefixSymbol, *IR::getConstant(keyType, keyType->width_bits()));
        return true;
    }
    if (matchType == P4Constants::MATCH_KIND_TERNARY) {
        const auto *maskSymbol =
            ControlPlaneState::getTableTernaryMask(tableName, fieldName, keyType);
        // TODO: What does default expression mean as a table entry?
        if (entryKey.is<IR::DefaultExpression>()) {
            keySet.emplace(keySymbol, *IR::getConstant(keyType, 0));
            keySet.emplace(*maskSymbol, *IR::getConstant(keyType, 0));
            return true;
        }
        if (const auto *maskExpr = entryKey.to<IR::Mask>()) {
            ASSIGN_OR_RETURN_WITH_MESSAGE(
                const auto &maskLeft, maskExpr->left->to<IR::Literal>(), false,
                ::error("Left mask element %1% is not a literal.", maskExpr->left));
            ASSIGN_OR_RETURN_WITH_MESSAGE(
                const auto &maskRight, maskExpr->right->to<IR::Literal>(), false,
                ::error("Right mask element %1% is not a literal.", maskExpr->right));
            keySet.emplace(keySymbol, maskLeft);
            keySet.emplace(*maskSymbol, maskRight);
            return true;
        }
        ASSIGN_OR_RETURN_WITH_MESSAGE(const auto &exactValue, entryKey.to<IR::Literal>(), false,
                                      ::error("Entry %1% is not a literal.", entryKey));
        keySet.emplace(keySymbol, exactValue);
        keySet.emplace(*maskSymbol, *IR::getMaxValueConstant(keyType));
        return true;
    }
    ::error("Match type %1% is not supported.", matchType);
    return false;
}

std::optional<TableKeySet> ControlPlaneStateInitializer::computeEntryKeySet(
    const IR::P4Table &table, const IR::Entry &entry) {
    ASSIGN_OR_RETURN_WITH_MESSAGE(const auto &key, table.getKey(), std::nullopt,
                                  ::error("Table %1% has no key.", table));
    auto numKeys = key.keyElements.size();
    RETURN_IF_FALSE_WITH_MESSAGE(
        numKeys == entry.keys->size(), std::nullopt,
        ::error("Entry key list and key match list must be equal in size."));
    TableKeySet keySet;
    auto tableName = table.controlPlaneName();
    for (size_t idx = 0; idx < numKeys; ++idx) {
        const auto *keyElement = key.keyElements.at(idx);
        const auto *keyExpr = keyElement->expression;
        RETURN_IF_FALSE_WITH_MESSAGE(keyExpr != nullptr, std::nullopt,
                                     ::error("Entry %1% in table %2% is null"));
        ASSIGN_OR_RETURN_WITH_MESSAGE(
            const auto &nameAnnotation, keyElement->getAnnotation("name"), std::nullopt,
            ::error("Key %1% in table %2% does not have a name annotation.", keyElement, table));
        auto fieldName = nameAnnotation.getName();
        const auto matchType = keyElement->matchType->toString();
        const auto *keySymbol = ControlPlaneState::getTableKey(tableName, fieldName, keyExpr->type);
        const auto *entryKey = entry.keys->components.at(idx);
        RETURN_IF_FALSE(
            computeMatch(*entryKey, *keySymbol, tableName, fieldName, matchType, keySet),
            std::nullopt);
    }
    return keySet;
}

std::optional<const IR::Expression *> computeEntryAction(
    const IR::P4Table &table, const IR::P4Action &actionDecl,
    const IR::MethodCallExpression &actionCall) {
    const IR::Expression *actionConstraint =
        new IR::Equ(ControlPlaneState::getTableActionChoice(table.controlPlaneName()),
                    IR::getStringLiteral(actionDecl.controlPlaneName()));
    RETURN_IF_FALSE_WITH_MESSAGE(
        actionCall.arguments->size() == actionDecl.parameters->parameters.size(), std::nullopt,
        ::error("Entry call %1% in table %2% does not have the right number of arguments.",
                actionCall, table));
    for (size_t idx = 0; idx < actionCall.arguments->size(); ++idx) {
        const auto *argument = actionCall.arguments->at(idx);
        const auto *parameter = actionDecl.parameters->parameters.at(idx);
        const auto *actionVariable = ControlPlaneState::getTableActionArgument(
            table.controlPlaneName(), actionDecl.controlPlaneName(), parameter->controlPlaneName(),
            parameter->type);
        RETURN_IF_FALSE_WITH_MESSAGE(
            argument->expression->is<IR::Constant>(), std::nullopt,
            ::error("Argument %1% in table %2% is not a constant.", argument, table));
        actionConstraint =
            new IR::LAnd(actionConstraint, new IR::Equ(actionVariable, argument->expression));
    }
    return actionConstraint;
}

std::optional<TableEntrySet> ControlPlaneStateInitializer::initializeTableEntries(
    const IR::P4Table *table, const P4::ReferenceMap &refMap) {
    TableEntrySet initialTableEntries;
    const auto *entries = table->getEntries();
    RETURN_IF_FALSE(entries != nullptr, initialTableEntries);

    for (const auto *entry : entries->entries) {
        const auto *actionCallExpression = entry->getAction();
        ASSIGN_OR_RETURN_WITH_MESSAGE(
            const auto &actionCall, actionCallExpression->to<IR::MethodCallExpression>(),
            std::nullopt,
            ::error("Action %1% in table %2% is not a method call.", actionCallExpression, table));
        ASSIGN_OR_RETURN_WITH_MESSAGE(const auto &methodName,
                                      actionCall.method->to<IR::PathExpression>(), std::nullopt,
                                      ::error("Action %1% in table %2% is not a path expression.",
                                              actionCallExpression, table));
        ASSIGN_OR_RETURN_WITH_MESSAGE(
            auto &actionDecl, refMap.getDeclaration(methodName.path, false), std::nullopt,
            ::error("Action reference %1% not found in the reference map", methodName));
        ASSIGN_OR_RETURN_WITH_MESSAGE(auto &action, actionDecl.to<IR::P4Action>(), std::nullopt,
                                      ::error("%1% is not a P4Action.", actionDecl));
        ASSIGN_OR_RETURN(auto actionConstraint, computeEntryAction(*table, action, actionCall),
                         std::nullopt);
        ASSIGN_OR_RETURN(auto entryKeySet, computeEntryKeySet(*table, *entry), std::nullopt);
        std::optional<int32_t> entryPriority;
        // In some cases, there is no priority. Try to create a priority from the entry.
        if (entry->priority != nullptr) {
            ASSIGN_OR_RETURN_WITH_MESSAGE(auto entryPriorityConstant,
                                          entry->priority->to<IR::Constant>(), std::nullopt,
                                          ::error("%1% is not a constant.", entry->priority));
            entryPriority = entryPriorityConstant.asInt();
        } else {
            // Assign the lowest priority by default.
            entryPriority = 0;
        }
        initialTableEntries.insert(
            *new TableMatchEntry(actionConstraint, entryPriority.value(), entryKeySet));
    }
    return initialTableEntries;
}

std::optional<const IR::Expression *> ControlPlaneStateInitializer::computeDefaultActionConstraints(
    const IR::P4Table *table, const P4::ReferenceMap &refMap) {
    auto tableName = table->controlPlaneName();
    const auto *defaultAction = table->getDefaultAction();
    ASSIGN_OR_RETURN_WITH_MESSAGE(
        const auto &actionCall, defaultAction->to<IR::MethodCallExpression>(), std::nullopt,
        ::error("Action %1% in table %2% is not a method call.", defaultAction, table));
    ASSIGN_OR_RETURN_WITH_MESSAGE(
        const auto &methodName, actionCall.method->to<IR::PathExpression>(), std::nullopt,
        ::error("Action %1% in table %2% is not a path expression.", defaultAction, table));
    ASSIGN_OR_RETURN_WITH_MESSAGE(
        auto &decl, refMap.getDeclaration(methodName.path, false), std::nullopt,
        ::error("Action reference %1% not found in the reference map.", methodName));
    ASSIGN_OR_RETURN_WITH_MESSAGE(auto &actionDecl, decl.to<IR::P4Action>(), std::nullopt,
                                  ::error("Action reference %1% is not a P4Action.", methodName));

    const auto *selectedAction = IR::getStringLiteral(actionDecl.controlPlaneName());
    const IR::Expression *defaultActionConstraints =
        new IR::Equ(selectedAction, ControlPlaneState::getTableActionChoice(tableName));
    defaultActionConstraints = new IR::LAnd(
        defaultActionConstraints,
        new IR::Equ(selectedAction, ControlPlaneState::getDefaultActionVariable(tableName)));
    const auto *arguments = actionCall.arguments;
    const auto *parameters = actionDecl.parameters;
    RETURN_IF_FALSE_WITH_MESSAGE(
        arguments->size() == parameters->parameters.size(), std::nullopt,
        ::error("Number of arguments does not match number of parameters."));

    for (size_t idx = 0; idx < arguments->size(); ++idx) {
        const auto *parameter = parameters->parameters.at(idx);
        const auto *argument = arguments->at(idx);
        defaultActionConstraints =
            new IR::LAnd(defaultActionConstraints,
                         new IR::Equ(ControlPlaneState::getTableActionArgument(
                                         tableName, actionDecl.controlPlaneName(),
                                         parameter->controlPlaneName(), parameter->type),
                                     argument->expression));
    }
    return defaultActionConstraints;
}

bool ControlPlaneStateInitializer::preorder(const IR::P4Table *table) {
    TableUtils::TableProperties properties;
    TableUtils::checkTableImmutability(*table, properties);
    auto tableName = table->controlPlaneName();

    ASSIGN_OR_RETURN(auto defaultActionConstraints, computeDefaultActionConstraints(table, refMap_),
                     false);
    ASSIGN_OR_RETURN(TableEntrySet initialTableEntries, initializeTableEntries(table, refMap_),
                     false);

    defaultConstraints.insert(
        {tableName, *new TableConfiguration(tableName, TableDefaultAction(defaultActionConstraints),
                                            initialTableEntries)});
    return false;
}

bool ControlPlaneStateInitializer::preorder(const IR::P4ValueSet *parserValueSet) {
    defaultConstraints.emplace(parserValueSet->controlPlaneName(),
                               *new ParserValueSet(parserValueSet->controlPlaneName()));
    return false;
}

}  // namespace P4Tools::Flay
