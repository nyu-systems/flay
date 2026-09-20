#include "backends/p4tools/modules/flay/core/control_plane/bfruntime/protobuf.h"

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

namespace P4::P4Tools::Flay::BfRuntime {

namespace {

/// Convert a BFRuntime FieldMatch into the appropriate symbolic constraint
/// assignments.
/// @param symbolSet tracks the symbols used in this conversion.
std::optional<ControlPlaneAssignmentSet> produceTableMatch(
    const bfrt_proto::KeyField &field, cstring tableName,
    const p4::config::v1::MatchField &matchField, SymbolSet &symbolSet) {
    ControlPlaneAssignmentSet tableKeySet;
    const auto *keyType = IR::Type_Bits::get(matchField.bitwidth());
    const auto *keySymbol = ControlPlaneState::getTableKey(tableName, matchField.name(), keyType);
    symbolSet.emplace(*keySymbol);
    switch (field.match_type_case()) {
        case bfrt_proto::KeyField::kExact: {
            auto value = Protobuf::stringToBigInt(field.exact().value());
            tableKeySet.emplace(*keySymbol, *IR::Constant::get(keyType, value));
            return tableKeySet;
        }
        case bfrt_proto::KeyField::kLpm: {
            const auto *lpmPrefixSymbol =
                ControlPlaneState::getTableMatchLpmPrefix(tableName, matchField.name(), keyType);
            symbolSet.emplace(*lpmPrefixSymbol);
            auto value = Protobuf::stringToBigInt(field.lpm().value());
            int prefix = field.lpm().prefix_len();
            tableKeySet.emplace(*keySymbol, *IR::Constant::get(keyType, value));
            tableKeySet.emplace(*lpmPrefixSymbol, *IR::Constant::get(keyType, prefix));
            return tableKeySet;
        }
        case bfrt_proto::KeyField::kTernary: {
            const auto *maskSymbol =
                ControlPlaneState::getTableTernaryMask(tableName, matchField.name(), keyType);
            symbolSet.emplace(*maskSymbol);
            auto value = Protobuf::stringToBigInt(field.ternary().value());
            auto mask = Protobuf::stringToBigInt(field.ternary().mask());
            tableKeySet.emplace(*keySymbol, *IR::Constant::get(keyType, value));
            tableKeySet.emplace(*maskSymbol, *IR::Constant::get(keyType, mask));
            return tableKeySet;
        }
        case bfrt_proto::KeyField::kRange: {
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
        case bfrt_proto::KeyField::kOptional: {
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

/// Convert a BFRuntime TableAction into the appropriate symbolic constraint
/// assignments. If @param isDefaultAction is true, then the constraints generated are
/// specialized towards overriding a default action in a table.
std::optional<ControlPlaneAssignmentSet> convertTableAction(const bfrt_proto::TableData &tblAction,
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
    if (tblAction.fields().size() != p4Action.params().size()) {
        return tableActionAssignmentSet;
    }
    RETURN_IF_FALSE_WITH_MESSAGE(tblAction.fields().size() == p4Action.params().size(),
                                 std::nullopt,
                                 error("Action configuration \"%1%\" and target action \"%2%\" "
                                       "have parameters of different number.",
                                       p4Action.ShortDebugString(), tblAction.ShortDebugString()));
    std::map<uint32_t, const p4::config::v1::Action_Param *> paramMap;
    for (const auto &param : p4Action.params()) {
        paramMap.emplace(param.id(), &param);
    }
    for (const auto &paramConfig : tblAction.fields()) {
        auto paramIt = paramMap.find(paramConfig.field_id());
        RETURN_IF_FALSE_WITH_MESSAGE(
            paramIt != paramMap.end(), std::nullopt,
            error("Parameter %1% of action %2% not found.", paramConfig.DebugString(), actionName));
        const auto *paramType = IR::Type_Bits::get(paramIt->second->bitwidth());
        auto paramName = paramIt->second->name();
        const auto *actionArg =
            ControlPlaneState::getTableActionArgument(tableName, actionName, paramName, paramType);
        symbolSet.emplace(*actionArg);
        RETURN_IF_FALSE_WITH_MESSAGE(paramConfig.has_stream(), std::nullopt,
                                     error("Parameter %1% of action %2% is not a stream value.",
                                           paramConfig.DebugString(), actionName));
        const auto *actionVal =
            IR::Constant::get(paramType, Protobuf::stringToBigInt(paramConfig.stream()));
        tableActionAssignmentSet.emplace(*actionArg, *actionVal);
    }
    return tableActionAssignmentSet;
}

/// Convert a BFRuntime TableEntry into a TableMatchEntry.
/// Returns std::nullopt if the conversion fails.
/// @param symbolSet tracks the symbols used in this conversion.
std::optional<TableMatchEntry *> produceTableEntry(cstring tableName,
                                                   P4::ControlPlaneAPI::p4rt_id_t tblId,
                                                   const p4::config::v1::P4Info &p4Info,
                                                   const bfrt_proto::TableEntry &tableEntry,
                                                   SymbolSet &symbolSet) {
    RETURN_IF_FALSE_WITH_MESSAGE(tableEntry.has_data(), std::nullopt,
                                 error("Table entry %1% has no action.", tableEntry.DebugString()));

    const auto &tableAction = tableEntry.data();
    auto actionId = tableAction.action_id();
    ASSIGN_OR_RETURN_WITH_MESSAGE(
        auto &p4Action, P4::ControlPlaneAPI::findP4RuntimeAction(p4Info, actionId), std::nullopt,
        error("Action ID %1% from table entry `%2%` not found in the P4Info.", actionId,
              tableEntry.ShortDebugString()));
    ASSIGN_OR_RETURN(const auto &tableActionAssignmentSet,
                     convertTableAction(tableAction, tableName, p4Action, symbolSet, false),
                     std::nullopt);
    ASSIGN_OR_RETURN_WITH_MESSAGE(
        auto &p4InfoTable, P4::ControlPlaneAPI::findP4RuntimeTable(p4Info, tblId), std::nullopt,
        error("Table ID %1% not found in the P4Info.", actionId));

    RETURN_IF_FALSE_WITH_MESSAGE(
        tableEntry.key().fields_size() <= p4InfoTable.match_fields().size(), std::nullopt,
        error("Table entry %1% has %2% matches, but P4Info has %3%.", tableEntry.DebugString(),
              tableEntry.key().fields_size(), p4InfoTable.match_fields().size()));
    // Use this map to look up which match fields are present in the control plane entry.
    // TODO: Cache this somehow?
    auto matchMap = std::map<uint32_t, bfrt_proto::KeyField>();
    for (const auto &matchField : tableEntry.key().fields()) {
        matchMap.emplace(matchField.field_id(), matchField);
    }

    ControlPlaneAssignmentSet tableKeySet;
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
    return new TableMatchEntry(tableActionAssignmentSet, 0, tableKeySet);
}

/// Convert a BFRuntime TableEntry into the appropriate symbolic constraint
/// assignments.
/// @param symbolSet tracks the symbols used in this conversion.
int updateTableEntry(const p4::config::v1::P4Info &p4Info, const p4::config::v1::Table &p4Table,
                     const bfrt_proto::TableEntry &tableEntry,
                     TableConfiguration &tableConfiguration,
                     const ::bfrt_proto::Update_Type &updateType, SymbolSet &symbolSet) {
    if (tableEntry.is_default_entry()) {
        const auto &defaultAction = tableEntry.data();
        ASSIGN_OR_RETURN_WITH_MESSAGE(
            auto &p4Action,
            P4::ControlPlaneAPI::findP4RuntimeAction(p4Info, defaultAction.action_id()),
            EXIT_FAILURE,
            error("Action ID %1% from default table entry `%2%` not found in the P4Info.",
                  defaultAction.action_id(), tableEntry.ShortDebugString()));
        ASSIGN_OR_RETURN(
            auto defaultActionExpr,
            convertTableAction(defaultAction, p4Table.preamble().name(), p4Action, symbolSet, true),
            EXIT_FAILURE);
        tableConfiguration.setDefaultTableAction(TableDefaultAction(defaultActionExpr));
    }

    RETURN_IF_FALSE_WITH_MESSAGE(
        !p4Table.is_const_table(), EXIT_FAILURE,
        error("Trying to insert an entry into table '%1%', which is a const table.",
              p4Table.preamble().name()));

    // Consider a delete message without an action a wild card delete.
    if (updateType == bfrt_proto::Update::DELETE && !tableEntry.has_data()) {
        tableConfiguration.clearTableEntries();
        return EXIT_SUCCESS;
    }

    ASSIGN_OR_RETURN(auto *tableMatchEntry,
                     produceTableEntry(p4Table.preamble().name(), p4Table.preamble().id(), p4Info,
                                       tableEntry, symbolSet),
                     EXIT_FAILURE);

    if (updateType == bfrt_proto::Update::MODIFY) {
        tableConfiguration.addTableEntry(*tableMatchEntry, true);
    } else if (updateType == bfrt_proto::Update::INSERT) {
        RETURN_IF_FALSE_WITH_MESSAGE(
            tableConfiguration.addTableEntry(*tableMatchEntry, false) == EXIT_SUCCESS, EXIT_FAILURE,
            error("Table entry \"%1%\" already exists.", tableEntry.ShortDebugString()));
    } else if (updateType == bfrt_proto::Update::DELETE) {
        RETURN_IF_FALSE_WITH_MESSAGE(tableConfiguration.deleteTableEntry(*tableMatchEntry) != 0,
                                     EXIT_FAILURE,
                                     error("Table entry %1% not found and can not be deleted.",
                                           tableEntry.ShortDebugString()));
    } else {
        error("Unsupported update type %1%.", updateType);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int updateTableEntry(const p4::config::v1::P4Info &p4Info, const p4::config::v1::Table &p4Table,
                     const bfrt_proto::TableEntry &tableEntry,
                     ControlPlaneConstraints &controlPlaneConstraints,
                     const ::bfrt_proto::Update_Type &updateType, SymbolSet &symbolSet) {
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

    if (p4Table.implementation_id() != 0) {
        warning(
            "Insertions of entries into tables with custom implementation is not supported yet "
            "(Table '%1%') is not implemented.",
            p4Table.preamble().name());
        return EXIT_SUCCESS;
    }

    return updateTableEntry(p4Info, p4Table, tableEntry, tableResult, updateType, symbolSet);
}

std::optional<cstring> getActionProfileName(const p4::config::v1::P4Info &p4Info,
                                            const bfrt_proto::TableEntry &tableEntry) {
    const auto *actionProfile =
        P4::ControlPlaneAPI::findP4RuntimeActionProfile(p4Info, tableEntry.table_id());
    if (actionProfile != nullptr) {
        return actionProfile->preamble().name();
    }
    const auto *actionProfileExtern =
        P4::ControlPlaneAPI::findP4RuntimeExtern(p4Info, cstring("ActionProfile"));
    if (actionProfileExtern == nullptr) {
        return std::nullopt;
    }
    std::optional<cstring> actionProfileNameOpt;
    for (const auto &actionProfileInstance : actionProfileExtern->instances()) {
        if (actionProfileInstance.preamble().id() == tableEntry.table_id()) {
            actionProfileNameOpt = actionProfileInstance.preamble().name();
        }
    }

    return actionProfileNameOpt;
}

std::optional<cstring> getActionSelectorName(const p4::config::v1::P4Info &p4Info,
                                             const bfrt_proto::TableEntry &tableEntry) {
    const auto *actionSelectorExtern =
        P4::ControlPlaneAPI::findP4RuntimeExtern(p4Info, cstring("ActionSelector"));
    if (actionSelectorExtern == nullptr) {
        return std::nullopt;
    }

    std::optional<cstring> actionSelectorNameOpt;
    for (const auto &actionSelectorInstance : actionSelectorExtern->instances()) {
        if (actionSelectorInstance.preamble().id() == tableEntry.table_id()) {
            actionSelectorNameOpt = actionSelectorInstance.preamble().name();
        }
    }
    return actionSelectorNameOpt;
}

int configureActionProfile(const bfrt_proto::TableEntry &tableEntry,
                           const ActionProfile &actionProfile, const p4::config::v1::P4Info &p4Info,
                           ControlPlaneConstraints &controlPlaneConstraints,
                           const ::bfrt_proto::Update_Type &updateType, SymbolSet &symbolSet) {
    // Iterate over each associated table and insert the respective action into the table.
    for (auto associatedTableReference : actionProfile.associatedTables()) {
        auto it = controlPlaneConstraints.find(associatedTableReference);
        RETURN_IF_FALSE_WITH_MESSAGE(it != controlPlaneConstraints.end(), EXIT_FAILURE,
                                     error("Configuration for table %1% not found in the "
                                           "control plane constraints. It should "
                                           "have already been initialized at this point.",
                                           associatedTableReference));

        ASSIGN_OR_RETURN_WITH_MESSAGE(auto &tableResult, it->second.get().to<TableConfiguration>(),
                                      EXIT_FAILURE,
                                      error("Configuration result %1% is not a TableConfiguration.",
                                            associatedTableReference));
        ASSIGN_OR_RETURN_WITH_MESSAGE(
            auto &p4InfoTable,
            P4::ControlPlaneAPI::findP4RuntimeTable(p4Info, associatedTableReference), EXIT_FAILURE,
            error("Table name %1% not found in the P4Info.", associatedTableReference));
        RETURN_IF_FALSE(updateTableEntry(p4Info, p4InfoTable, tableEntry, tableResult, updateType,
                                         symbolSet) == EXIT_SUCCESS,
                        EXIT_FAILURE);
    }
    return EXIT_SUCCESS;
}

int configureActionSelector(const bfrt_proto::TableEntry & /*tableEntry*/,
                            ActionSelector & /*selector*/,
                            const p4::config::v1::P4Info & /*p4Info*/,
                            ControlPlaneConstraints & /*controlPlaneConstraints*/,
                            const ::bfrt_proto::Update_Type & /*updateType*/,
                            SymbolSet & /*symbolSet*/) {
    // Currently a no-op.
    return EXIT_SUCCESS;
}

}  // namespace

int updateControlPlaneConstraintsWithEntityMessage(const bfrt_proto::Entity &entity,
                                                   const p4::config::v1::P4Info &p4Info,
                                                   ControlPlaneConstraints &controlPlaneConstraints,
                                                   const ::bfrt_proto::Update_Type &updateType,
                                                   SymbolSet &symbolSet) {
    if (entity.has_table_entry()) {
        auto tableId = entity.table_entry().table_id();
        const auto *p4Table = P4::ControlPlaneAPI::findP4RuntimeTable(p4Info, tableId);
        if (p4Table != nullptr) {
            RETURN_IF_FALSE(
                updateTableEntry(p4Info, *p4Table, entity.table_entry(), controlPlaneConstraints,
                                 updateType, symbolSet) == EXIT_SUCCESS,
                EXIT_FAILURE)
            return EXIT_SUCCESS;
        }
        // In BFRuntime, table entries could also configure an action profile or selector.
        auto actionProfileNameOpt = getActionProfileName(p4Info, entity.table_entry());
        if (actionProfileNameOpt.has_value()) {
            auto it = controlPlaneConstraints.find(actionProfileNameOpt.value());
            RETURN_IF_FALSE_WITH_MESSAGE(
                it != controlPlaneConstraints.end(), EXIT_FAILURE,
                error("Action profile %1% not found in the control plane constraints. It should "
                      "have already been initialized at this point.",
                      actionProfileNameOpt.value()));

            ASSIGN_OR_RETURN_WITH_MESSAGE(
                auto &actionProfile, it->second.get().to<ActionProfile>(), EXIT_FAILURE,
                error("Configuration result %1% is not an action profile.",
                      actionProfileNameOpt.value()));

            return configureActionProfile(entity.table_entry(), actionProfile, p4Info,
                                          controlPlaneConstraints, updateType, symbolSet);
        }
        auto actionSelectorNameOpt = getActionSelectorName(p4Info, entity.table_entry());
        if (actionSelectorNameOpt.has_value()) {
            auto it = controlPlaneConstraints.find(actionSelectorNameOpt.value());
            RETURN_IF_FALSE_WITH_MESSAGE(
                it != controlPlaneConstraints.end(), EXIT_FAILURE,
                error("Action selector %1% not found in the control plane constraints. It should "
                      "have already been initialized at this point.",
                      actionSelectorNameOpt.value()));

            ASSIGN_OR_RETURN_WITH_MESSAGE(
                auto &actionSelector, it->second.get().to<ActionSelector>(), EXIT_FAILURE,
                error("Configuration result %1% is not an action selector.",
                      actionSelectorNameOpt.value()));

            return configureActionSelector(entity.table_entry(), actionSelector, p4Info,
                                           controlPlaneConstraints, updateType, symbolSet);
        }
    }
    error("Unsupported control plane entry %1%.", entity.DebugString().c_str());
    return EXIT_FAILURE;
}

int updateControlPlaneConstraints(const bfruntime::flaytests::Config &protoControlPlaneConfig,
                                  const p4::config::v1::P4Info &p4Info,
                                  ControlPlaneConstraints &controlPlaneConstraints,
                                  SymbolSet &symbolSet) {
    for (const auto &entity : protoControlPlaneConfig.entities()) {
        if (updateControlPlaneConstraintsWithEntityMessage(entity, p4Info, controlPlaneConstraints,
                                                           bfrt_proto::Update::MODIFY,
                                                           symbolSet) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

}  // namespace P4::P4Tools::Flay::BfRuntime
