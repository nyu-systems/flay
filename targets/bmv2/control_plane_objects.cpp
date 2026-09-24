#include "backends/p4tools/modules/flay/targets/bmv2/control_plane_objects.h"

#include "backends/p4tools/common/lib/symbolic_variables.h"
#include "backends/p4tools/common/lib/variables.h"
#include "ir/irutils.h"

namespace P4::P4Tools::Flay::V1Model {

bool CloneSession::operator<(const ControlPlaneItem &other) const {
    // There is only one clone session active at a time. We ignore the session id.
    return typeid(*this) == typeid(other) ? false
                                          : typeid(*this).hash_code() < typeid(other).hash_code();
}

void CloneSession::setSessionId(uint32_t sessionId) {
    _controlPlaneAssignments.clear();
    _z3ControlPlaneAssignments.clear();

    _sessionId = sessionId;
    const auto *cloneActive = ToolsVariables::getSymbolicVariable(IR::Type_Boolean::get(),
                                                                  cstring("clone_session_active"));
    auto sessionIdHasValue = _sessionId.has_value();
    _controlPlaneAssignments.emplace(*cloneActive, *IR::BoolLiteral::get(sessionIdHasValue));
    if (sessionIdHasValue) {
        _controlPlaneAssignments.emplace(
            *Bmv2ControlPlaneState::getCloneSessionId(IR::Type_Bits::get(32)),
            *IR::Constant::get(IR::Type_Bits::get(32), _sessionId.value()));
    }
    _z3ControlPlaneAssignments.merge(_controlPlaneAssignments);
}

ControlPlaneAssignmentSet CloneSession::computeControlPlaneAssignments() const {
    return _controlPlaneAssignments;
}

Z3ControlPlaneAssignmentSet CloneSession::computeZ3ControlPlaneAssignments() const {
    return _z3ControlPlaneAssignments;
}

}  // namespace P4::P4Tools::Flay::V1Model
