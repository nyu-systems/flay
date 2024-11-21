#include "backends/p4tools/modules/flay/core/control_plane/id_to_ir_map.h"

#include "control-plane/p4RuntimeSymbolTable.h"

namespace P4::P4Tools::Flay {

P4RuntimeIdtoIrNodeMap P4RuntimeToIRMapper::getP4RuntimeIdtoIrNodeMap() const { return idToIrMap; }

bool P4RuntimeToIRMapper::preorder(const IR::P4Table *table) {
    if (P4::ControlPlaneAPI::isHidden(table)) {
        return false;
    }
    auto tableName = table->controlPlaneName();
    auto p4RuntimeId = p4InfoMaps.lookUpP4RuntimeId(tableName);
    if (p4RuntimeId.has_value()) {
        idToIrMap.emplace(p4RuntimeId.value(), table);
        const auto *key = table->getKey();
        if (key == nullptr) {
            return false;
        }
        for (const auto *keyElement : key->keyElements) {
            if (keyElement->matchType->path->name == "selector") {
                continue;
            }
            const auto *nameAnnot = keyElement->getAnnotation(IR::Annotation::nameAnnotation);
            // Some hidden tables do not have any key name annotations.
            if (nameAnnot == nullptr) {
                error("Non-constant table key without an annotation");
                return false;
            }
            auto combinedName = tableName + "_" + nameAnnot->getName();
            auto p4RuntimeKeyId = p4InfoMaps.lookUpP4RuntimeId(combinedName);
            if (p4RuntimeKeyId.has_value()) {
                idToIrMap.emplace(P4::ControlPlaneAPI::szudzikPairing(p4RuntimeId.value(),
                                                                      p4RuntimeKeyId.value()),
                                  keyElement);
            } else {
                error("%1% not found in the P4Runtime ID map.", keyElement);
                return false;
            }
        }
    } else {
        error("%1% not found in the P4Runtime ID map.", table);
    }

    return false;
}

bool P4RuntimeToIRMapper::preorder(const IR::Type_Header *hdr) {
    if (!P4::ControlPlaneAPI::isControllerHeader(hdr) || P4::ControlPlaneAPI::isHidden(hdr)) {
        return false;
    }
    const auto *controllerHeaderAnnotation = hdr->getAnnotation(cstring("controller_header"));
    auto headerName = controllerHeaderAnnotation->getUnparsed().at(0)->text;
    auto p4RuntimeId = p4InfoMaps.lookUpP4RuntimeId(headerName);
    if (p4RuntimeId.has_value()) {
        idToIrMap.emplace(p4RuntimeId.value(), hdr);
    } else {
        error("%1% not found in the P4Runtime ID map.", hdr);
    }
    return false;
}

bool P4RuntimeToIRMapper::preorder(const IR::P4ValueSet *valueSet) {
    if (P4::ControlPlaneAPI::isHidden(valueSet)) {
        return false;
    }
    auto p4RuntimeId = p4InfoMaps.lookUpP4RuntimeId(valueSet->controlPlaneName());
    if (p4RuntimeId.has_value()) {
        idToIrMap.emplace(p4RuntimeId.value(), valueSet);
    } else {
        error("%1% not found in the P4Runtime ID map.", valueSet);
    }
    return false;
}

bool P4RuntimeToIRMapper::preorder(const IR::P4Action *action) {
    if (P4::ControlPlaneAPI::isHidden(action)) {
        return false;
    }
    auto actionName = action->controlPlaneName();
    auto p4RuntimeId = p4InfoMaps.lookUpP4RuntimeId(actionName);
    if (p4RuntimeId.has_value()) {
        idToIrMap.emplace(p4RuntimeId.value(), action);
        for (const auto *param : action->getParameters()->parameters) {
            auto combinedName = actionName + "_" + param->controlPlaneName();
            auto paramP4RuntimeId = p4InfoMaps.lookUpP4RuntimeId(combinedName);
            if (p4RuntimeId.has_value()) {
                idToIrMap.emplace(P4::ControlPlaneAPI::szudzikPairing(p4RuntimeId.value(),
                                                                      paramP4RuntimeId.value()),
                                  param);
            } else {
                error("Parameter %1% not found in the P4Runtime ID map.", param);
                return false;
            }
        }
    } else {
        error("P4 action %1% not found in the P4Runtime ID map.", action);
    }
    return false;
}

}  // namespace P4::P4Tools::Flay
