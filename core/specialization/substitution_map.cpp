#include "backends/p4tools/modules/flay/core/specialization/substitution_map.h"

#include <optional>

#include "backends/p4tools/modules/flay/core/control_plane/substitute_variable.h"
#include "backends/p4tools/modules/flay/core/lib/simplify_expression.h"
#include "lib/error.h"
#include "lib/timer.h"

namespace P4Tools::Flay {

/**************************************************************************************************
SubstitutionMap
**************************************************************************************************/

SubstitutionMap::SubstitutionMap(const NodeAnnotationMap &map)
    : _symbolMap(map.expressionSymbolMap()) {
    for (auto &pair : map.expressionMap()) {
        emplace(pair.first, pair.second);
    }
}

std::optional<bool> SubstitutionMap::computeNodeSubstitution(
    const IR::Expression *expression, const ControlPlaneAssignmentSet &controlPlaneAssignments) {
    auto it = find(expression);
    if (it == end()) {
        ::error("Substitution mapping for node %1% does not exist.", expression);
        return std::nullopt;
    }

    const auto *originalExpression = it->second->originalExpression();
    originalExpression =
        originalExpression->apply(SubstituteSymbolicVariable(controlPlaneAssignments));
    originalExpression = SimplifyExpression::simplify(originalExpression);
    auto previousSubstitution = it->second->substitution();
    if (const auto *constant = originalExpression->to<IR::Constant>()) {
        it->second->setSubstitution(constant);
        return !previousSubstitution.has_value() || !previousSubstitution.value()->equiv(*constant);
    }
    if (const auto *boolConstant = originalExpression->to<IR::BoolLiteral>()) {
        it->second->setSubstitution(boolConstant);
        return !previousSubstitution.has_value() ||
               !previousSubstitution.value()->equiv(*boolConstant);
    }
    if (previousSubstitution.has_value()) {
        it->second->unsetSubstitution();
        return true;
    }

    return false;
}

std::optional<const IR::Literal *> SubstitutionMap::isExpressionConstant(
    const IR::Expression *expression) const {
    auto it = find(expression);
    if (it != end()) {
        return it->second->substitution();
    }
    ::warning(
        "Unable to find node %1% in the expression map of this execution state. There might be "
        "issues with the source information.",
        expression);
    return std::nullopt;
}

std::optional<bool> SubstitutionMap::recomputeSubstitution(
    const ControlPlaneConstraints &controlPlaneConstraints) {
    /// Generate IR equalities from the control plane constraints.
    /// Generate IR equalities from the control plane constraints.
    ControlPlaneAssignmentSet totalControlPlaneAssignments;
    for (const auto &[entityName, controlPlaneConstraint] : controlPlaneConstraints) {
        const auto &controlPlaneAssignments =
            controlPlaneConstraint.get().computeControlPlaneAssignments();
        totalControlPlaneAssignments.insert(controlPlaneAssignments.begin(),
                                            controlPlaneAssignments.end());
    }

    bool hasChanged = false;
    for (auto &pair : *this) {
        auto result = computeNodeSubstitution(pair.first, totalControlPlaneAssignments);
        if (!result.has_value()) {
            return std::nullopt;
        }
        hasChanged |= result.value();
    }
    return hasChanged;
}

std::optional<bool> SubstitutionMap::recomputeSubstitution(
    const SymbolSet &symbolSet, const ControlPlaneConstraints &controlPlaneConstraints) {
    Util::ScopedTimer timer("SubstitutionMap::recomputeReachability with symbol set");
    ExpressionSet targetExpressions;
    for (const auto &symbol : symbolSet) {
        auto it = _symbolMap.find(symbol);
        if (it != _symbolMap.end()) {
            for (const auto *node : it->second) {
                targetExpressions.insert(node->checkedTo<IR::Expression>());
            }
        }
    }
    return recomputeSubstitution(targetExpressions, controlPlaneConstraints);
}

std::optional<bool> SubstitutionMap::recomputeSubstitution(
    const ExpressionSet &targetExpressions,
    const ControlPlaneConstraints &controlPlaneConstraints) {
    /// Generate IR equalities from the control plane constraints.
    /// Generate IR equalities from the control plane constraints.
    ControlPlaneAssignmentSet totalControlPlaneAssignments;
    for (const auto &[entityName, controlPlaneConstraint] : controlPlaneConstraints) {
        const auto &controlPlaneAssignments =
            controlPlaneConstraint.get().computeControlPlaneAssignments();
        totalControlPlaneAssignments.insert(controlPlaneAssignments.begin(),
                                            controlPlaneAssignments.end());
    }
    bool hasChanged = false;
    for (const auto *node : targetExpressions) {
        auto result = computeNodeSubstitution(node, totalControlPlaneAssignments);
        if (!result.has_value()) {
            return std::nullopt;
        }
        hasChanged |= result.value();
    }
    return hasChanged;
}

}  // namespace P4Tools::Flay
