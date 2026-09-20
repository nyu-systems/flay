#include "backends/p4tools/modules/flay/targets/bmv2/symbolic_state.h"

#include <cstdlib>
#include <optional>

#include "backends/p4tools/common/lib/symbolic_variables.h"
#include "backends/p4tools/common/lib/variables.h"
#include "backends/p4tools/modules/flay/core/lib/return_macros.h"
#include "backends/p4tools/modules/flay/targets/bmv2/constants.h"
#include "backends/p4tools/modules/flay/targets/bmv2/control_plane_objects.h"

namespace P4::P4Tools::Flay::V1Model {

bool Bmv2ControlPlaneInitializer::computeMatch(const IR::Expression &entryKey,
                                               const IR::SymbolicVariable &keySymbol,
                                               cstring tableName, cstring fieldName,
                                               cstring matchType,
                                               ControlPlaneAssignmentSet &keySet) {
    if (matchType == V1ModelConstants::MATCH_KIND_RANGE) {
        cstring minName = tableName + "_range_min_" + fieldName;
        cstring maxName = tableName + "_range_max_" + fieldName;
        const auto *minKey = ToolsVariables::getSymbolicVariable(keySymbol.type, minName);
        const auto *maxKey = ToolsVariables::getSymbolicVariable(keySymbol.type, maxName);
        // TODO: What does default expression mean as a table entry?
        if (entryKey.is<IR::DefaultExpression>()) {
            keySet.emplace(*minKey, *IR::Constant::get(keySymbol.type, 0));
            keySet.emplace(*maxKey, *IR::getMaxValueConstant(keySymbol.type));
            return true;
        }

        if (const auto *rangeExpr = entryKey.to<IR::Range>()) {
            ASSIGN_OR_RETURN_WITH_MESSAGE(
                const auto &minExpr, rangeExpr->left->to<IR::Literal>(), false,
                error("Left mask element %1% is not a literal.", rangeExpr->left));
            ASSIGN_OR_RETURN_WITH_MESSAGE(
                const auto &maxExpr, rangeExpr->right->to<IR::Literal>(), false,
                error("Right mask element %1% is not a literal.", rangeExpr->right));
            keySet.emplace(*minKey, minExpr);
            keySet.emplace(*maxKey, maxExpr);
            return true;
        }
        ASSIGN_OR_RETURN_WITH_MESSAGE(const auto &exactValue, entryKey.to<IR::Literal>(), false,
                                      error("Entry %1% is not a literal.", entryKey));
        keySet.emplace(*minKey, exactValue);
        keySet.emplace(*maxKey, exactValue);
        return true;
    }
    if (matchType == V1ModelConstants::MATCH_KIND_OPT) {
        // TODO: What does default expression mean as a table entry?
        // An optional can either be an exact match or a ternary match.
        if (entryKey.is<IR::DefaultExpression>()) {
            const auto *maskSymbol =
                ControlPlaneState::getTableTernaryMask(tableName, fieldName, keySymbol.type);
            keySet.emplace(keySymbol, *IR::Constant::get(keySymbol.type, 0));
            keySet.emplace(*maskSymbol, *IR::Constant::get(keySymbol.type, 0));
            return true;
        }
        ASSIGN_OR_RETURN_WITH_MESSAGE(const auto &exactValue, entryKey.to<IR::Literal>(), false,
                                      error("Entry %1% is not a literal.", entryKey));
        keySet.emplace(keySymbol, exactValue);
        return true;
    }

    return ControlPlaneStateInitializer::computeMatch(entryKey, keySymbol, tableName, fieldName,
                                                      matchType, keySet);
}

std::optional<ControlPlaneConstraints>
Bmv2ControlPlaneInitializer::generateInitialControlPlaneConstraints(const IR::P4Program *program) {
    _defaultConstraints.emplace("clone_session", *new CloneSession(std::nullopt));

    program->apply(*this);
    if (errorCount() > 0) {
        return std::nullopt;
    }
    return defaultConstraints();
}

}  // namespace P4::P4Tools::Flay::V1Model
