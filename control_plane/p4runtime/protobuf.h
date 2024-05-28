#ifndef BACKENDS_P4TOOLS_MODULES_FLAY_CONTROL_PLANE_P4RUNTIME_PROTOBUF_H_
#define BACKENDS_P4TOOLS_MODULES_FLAY_CONTROL_PLANE_P4RUNTIME_PROTOBUF_H_

#include <optional>

#include "control-plane/p4RuntimeArchHandler.h"
#include "ir/ir.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wpedantic"
#include "backends/p4tools/modules/flay/control_plane/p4runtime/flaytests.pb.h"
#pragma GCC diagnostic pop

#include "backends/p4tools/modules/flay/control_plane/control_plane_objects.h"
#include "backends/p4tools/modules/flay/control_plane/symbolic_state.h"

/// Parses a Protobuf text message file and converts the instructions contained
/// within into P4C-IR nodes. These IR-nodes are structured to represent a
/// control-plane configuration that maps to the semantic data-plane
/// representation of the program.
namespace P4Tools::Flay::P4Runtime {
/// Convert a P4Runtime TableAction into the appropriate symbolic constraint
/// assignments. If @param isDefaultAction is true, then the constraints generated are
/// specialized towards overriding a default action in a table.
[[nodiscard]] std::optional<const IR::Expression *> convertTableAction(
    const p4::v1::Action &tblAction, cstring tableName, const p4::config::v1::Action &p4Action,
    SymbolSet &symbolSet, bool isDefaultAction);

/// Convert a P4Runtime FieldMatch into the appropriate symbolic constraint
/// assignments.
/// @param symbolSet tracks the symbols used in this conversion.
[[nodiscard]] std::optional<TableKeySet> produceTableMatch(
    const p4::v1::FieldMatch &field, cstring tableName,
    const p4::config::v1::MatchField &matchField, SymbolSet &symbolSet);

/// Retrieve the appropriate symbolic constraint assignments for a field that is not set in the
/// message.
/// @param symbolSet tracks the symbols used in this conversion.
[[nodiscard]] std::optional<TableKeySet> produceTableMatchForMissingField(
    cstring tableName, const p4::config::v1::MatchField &matchField, SymbolSet &symbolSet);

/// Convert a P4Runtime TableEntry into a TableMatchEntry.
/// Returns std::nullopt if the conversion fails.
/// @param symbolSet tracks the symbols used in this conversion.
[[nodiscard]] std::optional<TableMatchEntry *> produceTableEntry(
    cstring tableName, P4::ControlPlaneAPI::p4rt_id_t tblId, const p4::config::v1::P4Info &p4Info,
    const p4::v1::TableEntry &tableEntry, SymbolSet &symbolSet);

/// Convert a P4Runtime TableEntry into the appropriate symbolic constraint
/// assignments.
/// @param symbolSet tracks the symbols used in this conversion.
[[nodiscard]] int updateTableEntry(const p4::config::v1::P4Info &p4Info,
                                   const p4::v1::TableEntry &tableEntry,
                                   ControlPlaneConstraints &controlPlaneConstraints,
                                   const ::p4::v1::Update_Type &updateType, SymbolSet &symbolSet);

/// Convert a Protobuf P4Runtime entity object into a set of IR-based
/// control-plane constraints. Use the
/// @param irToIdMap to lookup the nodes associated with P4Runtime Ids.
/// @param symbolSet tracks the symbols used in this conversion.
[[nodiscard]] int updateControlPlaneConstraintsWithEntityMessage(
    const p4::v1::Entity &entity, const p4::config::v1::P4Info &p4Info,
    ControlPlaneConstraints &controlPlaneConstraints, const ::p4::v1::Update_Type &updateType,
    SymbolSet &symbolSet);

/// Convert a Protobuf Config object into a set of IR-based control-plane
/// constraints. Use the
/// @param irToIdMap to lookup the nodes associated with P4Runtime Ids.
/// @param symbolSet tracks the symbols used in this conversion.
[[nodiscard]] int updateControlPlaneConstraints(
    const ::p4runtime::flaytests::Config &protoControlPlaneConfig,
    const p4::config::v1::P4Info &p4Info, ControlPlaneConstraints &controlPlaneConstraints,
    SymbolSet &symbolSet);

}  // namespace P4Tools::Flay::P4Runtime

#endif /* BACKENDS_P4TOOLS_MODULES_FLAY_CONTROL_PLANE_P4RUNTIME_PROTOBUF_H_ */