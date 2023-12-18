#include "backends/p4tools/modules/flay/control_plane/protobuf/protobuf.h"

#include <fcntl.h>

#include <google/protobuf/text_format.h>

#include "backends/p4tools/common/lib/logging.h"
#include "backends/p4tools/modules/flay/control_plane/symbolic_state.h"
#include "ir/ir-generated.h"
#include "ir/irutils.h"

namespace P4Tools::Flay {

big_int ProtobufDeserializer::protoValueToBigInt(const std::string &valueString) {
    big_int value;
    boost::multiprecision::import_bits(value, valueString.begin(), valueString.end());
    return value;
}

flaytests::Config ProtobufDeserializer::deserializeProtobufConfig(
    const std::filesystem::path &inputFile) {
    flaytests::Config protoControlPlaneConfig;

    // Parse the input file into the Protobuf object.
    int fd = open(inputFile.c_str(), O_RDONLY);
    google::protobuf::io::ZeroCopyInputStream *input =
        new google::protobuf::io::FileInputStream(fd);

    if (google::protobuf::TextFormat::Parse(input, &protoControlPlaneConfig)) {
        printInfo("Parsed configuration: %1%", protoControlPlaneConfig.DebugString());
    } else {
        std::cerr << "Message not valid (partial content: "
                  << protoControlPlaneConfig.ShortDebugString() << ")\n";
    }
    // Close the open file.
    close(fd);
    return protoControlPlaneConfig;
}

void ProtobufDeserializer::fillTableMatch(const p4::v1::FieldMatch &field, cstring tableName,
                                          cstring keyFieldName, const IR::Expression &keyExpr,
                                          TableMatchEntry &tableMatchEntry) {
    const auto *keySymbol = ControlPlaneState::getTableKey(tableName, keyFieldName, keyExpr.type);
    if (field.has_exact()) {
        auto value = protoValueToBigInt(field.exact().value());
        if (keyExpr.type->is<IR::Type_Boolean>()) {
            tableMatchEntry.addMatch(*keySymbol, *new IR::BoolLiteral(value == 1));
        } else {
            tableMatchEntry.addMatch(*keySymbol, *IR::getConstant(keyExpr.type, value));
        }
    } else if (field.has_lpm()) {
        const auto *lpmPrefixSymbol =
            ControlPlaneState::getTableMatchLpmPrefix(tableName, keyFieldName, keyExpr.type);
        auto value = protoValueToBigInt(field.lpm().value());
        int prefix = field.lpm().prefix_len();
        tableMatchEntry.addMatch(*keySymbol, *IR::getConstant(keyExpr.type, value));
        tableMatchEntry.addMatch(*lpmPrefixSymbol, *IR::getConstant(keyExpr.type, prefix));
    } else if (field.has_ternary()) {
        const auto *maskSymbol =
            ControlPlaneState::getTableTernaryMask(tableName, keyFieldName, keyExpr.type);
        auto value = protoValueToBigInt(field.ternary().value());
        auto mask = protoValueToBigInt(field.ternary().mask());
        tableMatchEntry.addMatch(*keySymbol, *IR::getConstant(keyExpr.type, value));
        tableMatchEntry.addMatch(*maskSymbol, *IR::getConstant(keyExpr.type, mask));
    } else {
        P4C_UNIMPLEMENTED("Unsupported table match type %1%.", field.DebugString().c_str());
    }
}

const IR::Expression *ProtobufDeserializer::convertTableAction(const p4::v1::Action &tblAction,
                                                               cstring tableName,
                                                               const IR::P4Action &p4Action) {
    const auto *tableActionID = ControlPlaneState::getTableActionChoice(tableName);

    auto actionName = p4Action.controlPlaneName();
    const auto *actionAssignment = new IR::StringLiteral(actionName);
    const IR::Expression *actionExpr = new IR::Equ(tableActionID, actionAssignment);
    for (const auto &paramConfig : tblAction.params()) {
        const auto *param = p4Action.parameters->getParameter(paramConfig.param_id() - 1);
        auto paramName = param->controlPlaneName();
        const auto &actionArg =
            ControlPlaneState::getTableActionArg(tableName, actionName, paramName, param->type);
        big_int value;
        auto fieldString = paramConfig.value();
        boost::multiprecision::import_bits(value, fieldString.begin(), fieldString.end());
        const auto *actionVal = IR::getConstant(param->type, value);
        actionExpr = new IR::LAnd(actionExpr, new IR::Equ(actionArg, actionVal));
    }
    return actionExpr;
}

void ProtobufDeserializer::convertTableEntry(const P4RuntimeIdtoIrNodeMap &irToIdMap,
                                             const p4::v1::TableEntry &tableEntry,
                                             ControlPlaneConstraints &controlPlaneConstraints) {
    auto tblId = tableEntry.table_id();
    const auto *tbl = irToIdMap.at(tblId)->checkedTo<IR::P4Table>();
    auto tableName = tbl->controlPlaneName();
    // const auto *tableConfiguredVar = ControlPlaneState::getTableActive(tableName);
    // controlPlaneConstraints[*tableConfiguredVar] = IR::getBoolLiteral(true);

    const auto *key = tbl->getKey();
    std::map<uint32_t, const IR::KeyElement *> idMap;
    for (const auto *keyElement : key->keyElements) {
        const auto *idAnno = keyElement->getAnnotation("id");
        CHECK_NULL(idAnno);
        uint32_t idx = idAnno->expr.at(0)->checkedTo<IR::Constant>()->asUnsigned();
        idMap.emplace(idx, keyElement);
    }

    BUG_CHECK(tableEntry.action().has_action(), "Table entry has no action.");

    auto tblAction = tableEntry.action().action();
    auto actionId = tblAction.action_id();
    const auto *p4Action = irToIdMap.at(actionId)->checkedTo<IR::P4Action>();
    const auto *actionExpr = convertTableAction(tblAction, tableName, *p4Action);
    auto *tableMatchEntry = new TableMatchEntry(actionExpr, tableEntry.priority());

    for (const auto &field : tableEntry.match()) {
        auto fieldId = field.field_id();
        const auto *keyField = idMap.at(fieldId);
        const auto *keyExpr = keyField->expression;
        const auto *nameAnnot = keyField->getAnnotation("name");
        // Some hidden tables do not have any key name annotations.
        BUG_CHECK(nameAnnot != nullptr /* || properties.tableIsImmutable*/,
                  "Non-constant table key without an annotation");
        cstring keyFieldName;
        if (nameAnnot != nullptr) {
            keyFieldName = nameAnnot->getName();
        }
        fillTableMatch(field, tableName, keyFieldName, *keyExpr, *tableMatchEntry);
    }
    auto it = controlPlaneConstraints.find(tableName);
    if (it == controlPlaneConstraints.end()) {
        error(
            "Configuration for table %1% not found in the control plane constraints. It should "
            "have already been initialized at this point.",
            tableName);
    } else {
        it->second.get().checkedTo<TableConfiguration>()->addTableEntry(*tableMatchEntry);
    }
}

void ProtobufDeserializer::updateControlPlaneConstraintsWithEntityMessage(
    const p4::v1::Entity &entity, const P4RuntimeIdtoIrNodeMap &irToIdMap,
    ControlPlaneConstraints &controlPlaneConstraints) {
    if (entity.has_table_entry()) {
        const auto &tableEntry = entity.table_entry();
        convertTableEntry(irToIdMap, tableEntry, controlPlaneConstraints);
    } else {
        P4C_UNIMPLEMENTED("Unsupported control plane entry %1%.", entity.DebugString().c_str());
    }
}

void ProtobufDeserializer::updateControlPlaneConstraints(
    const flaytests::Config &protoControlPlaneConfig, const P4RuntimeIdtoIrNodeMap &irToIdMap,
    ControlPlaneConstraints &controlPlaneConstraints) {
    for (const auto &entity : protoControlPlaneConfig.entities()) {
        updateControlPlaneConstraintsWithEntityMessage(entity, irToIdMap, controlPlaneConstraints);
    }
}

std::optional<p4::v1::Entity> ProtobufDeserializer::parseEntity(const std::string &message) {
    p4::v1::Entity entity;
    if (google::protobuf::TextFormat::ParseFromString(message, &entity)) {
        printInfo("Parsed entity: %1%", entity.DebugString());
    } else {
        std::cerr << "Message not valid (partial content: " << entity.ShortDebugString() << ")\n";
        return std::nullopt;
    }
    return entity;
}

}  // namespace P4Tools::Flay
