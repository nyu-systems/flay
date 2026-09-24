#include "backends/p4tools/modules/flay/core/control_plane/p4runtime/protobuf.h"

#include <fcntl.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <optional>

#include "backends/p4tools/common/lib/symbolic_variables.h"
#include "backends/p4tools/modules/flay/core/control_plane/control_plane_item.h"
#include "backends/p4tools/modules/flay/core/control_plane/control_plane_objects.h"
#include "backends/p4tools/modules/flay/core/control_plane/protobuf_utils.h"
#include "control-plane/p4RuntimeArchHandler.h"
#include "control-plane/p4infoApi.h"
#include "ir/irutils.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wpedantic"
#include "p4/config/v1/p4info.pb.h"
#include "p4/v1/p4runtime.pb.h"
#pragma GCC diagnostic pop

namespace P4::P4Tools::Flay::P4Runtime {

namespace {

/// Convert a P4Runtime FieldMatch into the appropriate symbolic constraint
/// assignments.
/// @param symbolSet tracks the symbols used in this conversion.
std::optional<ControlPlaneAssignmentSet> produceTableMatch(
    const p4::v1::FieldMatch &field, cstring tableName,
    const p4::config::v1::MatchField &matchField, SymbolSet &symbolSet) {
    ControlPlaneAssignmentSet tableKeySet;
    const auto *keyType = IR::Type_Bits::get(matchField.bitwidth());
    const auto *keySymbol = ControlPlaneState::getTableKey(tableName, matchField.name(), keyType);
    symbolSet.emplace(*keySymbol);
    switch (field.field_match_type_case()) {
        case p4::v1::FieldMatch::kExact: {
            auto value = Protobuf::stringToBigInt(field.exact().value());
            tableKeySet.emplace(*keySymbol, *IR::Constant::get(keyType, value));
            return tableKeySet;
        }
        case p4::v1::FieldMatch::kLpm: {
            const auto *lpmPrefixSymbol =
                ControlPlaneState::getTableMatchLpmPrefix(tableName, matchField.name(), keyType);
            symbolSet.emplace(*lpmPrefixSymbol);
            auto value = Protobuf::stringToBigInt(field.lpm().value());
            int prefix = field.lpm().prefix_len();
            tableKeySet.emplace(*keySymbol, *IR::Constant::get(keyType, value));
            tableKeySet.emplace(*lpmPrefixSymbol, *IR::Constant::get(keyType, prefix));
            return tableKeySet;
        }
        case p4::v1::FieldMatch::kTernary: {
            const auto *maskSymbol =
                ControlPlaneState::getTableTernaryMask(tableName, matchField.name(), keyType);
            symbolSet.emplace(*maskSymbol);
            auto value = Protobuf::stringToBigInt(field.ternary().value());
            auto mask = Protobuf::stringToBigInt(field.ternary().mask());
            tableKeySet.emplace(*keySymbol, *IR::Constant::get(keyType, value));
            tableKeySet.emplace(*maskSymbol, *IR::Constant::get(keyType, mask));
            return tableKeySet;
        }
        case p4::v1::FieldMatch::kRange: {
            const auto &[rangeMinSymbol, rangeMaxSymbol] =
                Bmv2ControlPlaneState::getTableRange(tableName, matchField.name(), keyType);
            symbolSet.emplace(*rangeMinSymbol);
            symbolSet.emplace(*rangeMaxSymbol);
            auto low = Protobuf::stringToBigInt(field.range().low());
            auto high = Protobuf::stringToBigInt(field.range().high());
            tableKeySet.emplace(*rangeMinSymbol, *IR::Constant::get(keyType, low));
            tableKeySet.emplace(*rangeMaxSymbol, *IR::Constant::get(keyType, high));
            return tableKeySet;
        }
        case p4::v1::FieldMatch::kOptional: {
            auto value = Protobuf::stringToBigInt(field.optional().value());
            tableKeySet.emplace(*keySymbol, *IR::Constant::get(keyType, value));
            return tableKeySet;
        }
        default:
            error("Unsupported table match type %1%.", field.DebugString().c_str());
    }
    return std::nullopt;
}

/// Retrieve the appropriate symbolic constraint assignments for a field that is not set in the
/// message.
/// @param symbolSet tracks the symbols used in this conversion.
std::optional<ControlPlaneAssignmentSet> produceTableMatchForMissingField(
    cstring tableName, const p4::config::v1::MatchField &matchField, SymbolSet &symbolSet) {
    ControlPlaneAssignmentSet tableKeySet;
    const auto *keyType = IR::Type_Bits::get(matchField.bitwidth());
    const auto *keySymbol = ControlPlaneState::getTableKey(tableName, matchField.name(), keyType);
    symbolSet.emplace(*keySymbol);
    switch (matchField.match_type()) {
        /// We can convert missing ternary and optional fields to 0.
        case p4::config::v1::MatchField::TERNARY:
        case p4::config::v1::MatchField::OPTIONAL: {
            const auto *keySymbol =
                ControlPlaneState::getTableKey(tableName, matchField.name(), keyType);
            const auto *maskSymbol =
                ControlPlaneState::getTableTernaryMask(tableName, matchField.name(), keyType);
            symbolSet.emplace(*keySymbol);
            symbolSet.emplace(*maskSymbol);
            tableKeySet.emplace(*keySymbol, *IR::Constant::get(keyType, 0));
            tableKeySet.emplace(*maskSymbol, *IR::Constant::get(keyType, 0));
            return tableKeySet;
        }
        default:
            error("Unsupported match type %1%.", matchField.DebugString());
    }
    return std::nullopt;
}

/// Convert a P4Runtime TableAction into the appropriate symbolic constraint
/// assignments. If @param isDefaultAction is true, then the constraints generated are
/// specialized towards overriding a default action in a table.
std::optional<ControlPlaneAssignmentSet> convertTableAction(const p4::v1::Action &tblAction,
                                                            cstring tableName,
                                                            const p4::config::v1::Action &p4Action,
                                                            SymbolSet &symbolSet,
                                                            bool isDefaultAction) {
    const IR::SymbolicVariable *tableActionID =
        isDefaultAction ? ControlPlaneState::getDefaultActionVariable(tableName)
                        : ControlPlaneState::getTableActionChoice(tableName);
    symbolSet.emplace(*tableActionID);
    auto actionName = p4Action.preamble().name();
    const auto *actionAssignment = IR::StringLiteral::get(actionName);
    ControlPlaneAssignmentSet tableActionAssignmentSet;
    tableActionAssignmentSet.emplace(*tableActionID, *actionAssignment);
    if (tblAction.params().size() != p4Action.params().size()) {
        return tableActionAssignmentSet;
    }
    RETURN_IF_FALSE_WITH_MESSAGE(tblAction.params().size() == p4Action.params().size(),
                                 std::nullopt,
                                 error("Action configuration \"%1%\" and target action \"%2%\" "
                                       "have parameters of different number.",
                                       p4Action.ShortDebugString(), tblAction.ShortDebugString()));
    std::map<uint32_t, const p4::config::v1::Action_Param *> paramMap;
    for (const auto &param : p4Action.params()) {
        paramMap.emplace(param.id(), &param);
    }
    for (const auto &paramConfig : tblAction.params()) {
        auto paramIt = paramMap.find(paramConfig.param_id());
        RETURN_IF_FALSE_WITH_MESSAGE(
            paramIt != paramMap.end(), std::nullopt,
            error("Parameter %1% of action %2% not found.", paramConfig.DebugString(), actionName));
        const auto *paramType = IR::Type_Bits::get(paramIt->second->bitwidth());
        auto paramName = paramIt->second->name();
        const auto *actionArg =
            ControlPlaneState::getTableActionArgument(tableName, actionName, paramName, paramType);
        symbolSet.emplace(*actionArg);
        const auto *actionVal =
            IR::Constant::get(paramType, Protobuf::stringToBigInt(paramConfig.value()));
        tableActionAssignmentSet.emplace(*actionArg, *actionVal);
    }
    return tableActionAssignmentSet;
}

/// Convert a P4Runtime TableEntry into a TableMatchEntry.
/// Returns std::nullopt if the conversion fails.
/// @param symbolSet tracks the symbols used in this conversion.
std::optional<TableMatchEntry *> produceTableEntry(cstring tableName,
                                                   P4::ControlPlaneAPI::p4rt_id_t tblId,
                                                   const p4::config::v1::P4Info &p4Info,
                                                   const p4::v1::TableEntry &tableEntry,
                                                   SymbolSet &symbolSet) {
    RETURN_IF_FALSE_WITH_MESSAGE(tableEntry.action().has_action(), std::nullopt,
                                 error("Table entry %1% has no action.", tableEntry.DebugString()));

    auto tableAction = tableEntry.action().action();
    auto actionId = tableAction.action_id();
    ASSIGN_OR_RETURN_WITH_MESSAGE(
        auto &p4Action, P4::ControlPlaneAPI::findP4RuntimeAction(p4Info, actionId), std::nullopt,
        error("Action ID %1% not found in the P4Info.", actionId));
    ASSIGN_OR_RETURN(const auto &tableActionAssignmentSet,
                     convertTableAction(tableAction, tableName, p4Action, symbolSet, false),
                     std::nullopt);
    ControlPlaneAssignmentSet tableKeySet;
    ASSIGN_OR_RETURN_WITH_MESSAGE(
        auto &p4InfoTable, P4::ControlPlaneAPI::findP4RuntimeTable(p4Info, tblId), std::nullopt,
        error("Table ID %1% not found in the P4Info.", actionId));

    RETURN_IF_FALSE_WITH_MESSAGE(
        tableEntry.match().size() <= p4InfoTable.match_fields().size(), std::nullopt,
        error("Table entry %1% has %2% matches, but P4Info has %3%.", tableEntry.DebugString(),
              tableEntry.match().size(), p4InfoTable.match_fields().size()));
    // Use this map to look up which match fields are present in the control plane entry.
    // TODO: Cache this somehow?
    auto matchMap = std::map<uint32_t, p4::v1::FieldMatch>();
    for (const auto &matchField : tableEntry.match()) {
        matchMap.emplace(matchField.field_id(), matchField);
    }

    for (const auto &p4InfoMatchField : p4InfoTable.match_fields()) {
        auto matchFieldIt = matchMap.find(p4InfoMatchField.id());
        std::optional<ControlPlaneAssignmentSet> matchSetOpt;
        // If we are missing a match entry, create the dummy entry for supported fields.
        if (matchFieldIt == matchMap.end()) {
            matchSetOpt = produceTableMatchForMissingField(tableName, p4InfoMatchField, symbolSet);
        } else {
            matchSetOpt =
                produceTableMatch(matchFieldIt->second, tableName, p4InfoMatchField, symbolSet);
        }
        ASSIGN_OR_RETURN(auto matchSet, matchSetOpt, std::nullopt);
        tableKeySet.insert(matchSet.begin(), matchSet.end());
    }

    return new TableMatchEntry(tableActionAssignmentSet, tableEntry.priority(), tableKeySet);
}

/// Convert a P4Runtime TableEntry into the appropriate symbolic constraint
/// assignments.
/// @param symbolSet tracks the symbols used in this conversion.
int updateTableEntry(const p4::config::v1::P4Info &p4Info, const p4::v1::TableEntry &tableEntry,
                     ControlPlaneConstraints &controlPlaneConstraints,
                     const ::p4::v1::Update_Type &updateType, SymbolSet &symbolSet) {
    auto tblId = tableEntry.table_id();
    ASSIGN_OR_RETURN_WITH_MESSAGE(
        auto &p4Table, P4::ControlPlaneAPI::findP4RuntimeTable(p4Info, tblId), EXIT_FAILURE,
        error("Table ID %1% not found in the P4Info.", tblId));
    cstring tableName = p4Table.preamble().name();

    auto it = controlPlaneConstraints.find(tableName);
    RETURN_IF_FALSE_WITH_MESSAGE(
        it != controlPlaneConstraints.end(), EXIT_FAILURE,
        error("Configuration for table %1% not found in the control plane constraints. It should "
              "have already been initialized at this point.",
              tableName));

    ASSIGN_OR_RETURN_WITH_MESSAGE(
        auto &tableResult, it->second.get().to<TableConfiguration>(), EXIT_FAILURE,
        error("Configuration result is not a TableConfiguration.", tableName));

    if (tableEntry.is_default_action()) {
        auto defaultAction = tableEntry.action().action();
        ASSIGN_OR_RETURN_WITH_MESSAGE(
            auto &p4Action,
            P4::ControlPlaneAPI::findP4RuntimeAction(p4Info, defaultAction.action_id()),
            EXIT_FAILURE,
            error("Action ID %1% not found in the P4Info.", defaultAction.action_id()));
        ASSIGN_OR_RETURN(auto defaultActionExpr,
                         convertTableAction(defaultAction, tableName, p4Action, symbolSet, true),
                         EXIT_FAILURE);
        tableResult.setDefaultTableAction(TableDefaultAction(defaultActionExpr));
    }

    RETURN_IF_FALSE_WITH_MESSAGE(
        !p4Table.is_const_table(), EXIT_FAILURE,
        error("Trying to insert an entry into table '%1%', which is a const table.", tableName));

    ASSIGN_OR_RETURN(auto *tableMatchEntry,
                     produceTableEntry(tableName, tblId, p4Info, tableEntry, symbolSet),
                     EXIT_FAILURE);

    if (updateType == p4::v1::Update::MODIFY) {
        tableResult.addTableEntry(*tableMatchEntry, true);
    } else if (updateType == p4::v1::Update::INSERT) {
        RETURN_IF_FALSE_WITH_MESSAGE(
            tableResult.addTableEntry(*tableMatchEntry, false) == EXIT_SUCCESS, EXIT_FAILURE,
            error("Table entry \"%1%\" already exists.", tableEntry.ShortDebugString()));
    } else if (updateType == p4::v1::Update::DELETE) {
        RETURN_IF_FALSE_WITH_MESSAGE(tableResult.deleteTableEntry(*tableMatchEntry) != 0,
                                     EXIT_FAILURE,
                                     error("Table entry %1% not found and can not be deleted.",
                                           tableEntry.ShortDebugString()));
    } else {
        error("Unsupported update type %1%.", updateType);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

}  // namespace

int updateControlPlaneConstraintsWithEntityMessage(const p4::v1::Entity &entity,
                                                   const p4::config::v1::P4Info &p4Info,
                                                   ControlPlaneConstraints &controlPlaneConstraints,
                                                   const ::p4::v1::Update_Type &updateType,
                                                   SymbolSet &symbolSet) {
    if (entity.has_table_entry()) {
        RETURN_IF_FALSE(updateTableEntry(p4Info, entity.table_entry(), controlPlaneConstraints,
                                         updateType, symbolSet) == EXIT_SUCCESS,
                        EXIT_FAILURE)
    } else {
        error("Unsupported control plane entry %1%.", entity.DebugString().c_str());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int updateControlPlaneConstraints(const ::p4runtime::flaytests::Config &protoControlPlaneConfig,
                                  const p4::config::v1::P4Info &p4Info,
                                  ControlPlaneConstraints &controlPlaneConstraints,
                                  SymbolSet &symbolSet) {
    for (const auto &entity : protoControlPlaneConfig.entities()) {
        if (updateControlPlaneConstraintsWithEntityMessage(entity, p4Info, controlPlaneConstraints,
                                                           p4::v1::Update::MODIFY,
                                                           symbolSet) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

}  // namespace P4::P4Tools::Flay::P4Runtime
