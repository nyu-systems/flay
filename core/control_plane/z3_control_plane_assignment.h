#ifndef BACKENDS_P4TOOLS_MODULES_FLAY_CORE_CONTROL_PLANE_Z3_CONTROL_PLANE_ASSIGNMENT_H_
#define BACKENDS_P4TOOLS_MODULES_FLAY_CORE_CONTROL_PLANE_Z3_CONTROL_PLANE_ASSIGNMENT_H_

#include <z3++.h>

#include "backends/p4tools/common/lib/variables.h"
#include "backends/p4tools/modules/flay/core/control_plane/control_plane_assignment.h"
#include "backends/p4tools/modules/flay/core/lib/z3_cache.h"
#include "ir/ir.h"

namespace P4::P4Tools::Flay {

/// The Z3 version of a ControlPlaneAssignmentSet. A little bit more restricted.
class Z3ControlPlaneAssignmentSet
    : private ordered_map<std::reference_wrapper<const IR::SymbolicVariable>, z3::expr,
                          IR::StructuralLess> {
 public:
    Z3ControlPlaneAssignmentSet() = default;

    /// Return the number of entries in the set.
    [[nodiscard]] size_t size() const {
        return ordered_map<std::reference_wrapper<const IR::SymbolicVariable>, z3::expr,
                           IR::StructuralLess>::size();
    }

    /// Add a new variable to the set. If the variable is already in the set, returns false.
    bool add(const IR::SymbolicVariable &var, z3::expr assignment) {
        auto result = emplace(var, assignment);
        if (!result.second) {
            error("Entry for `%1%` already in the set", var);
        }
        return result.second;
    }

    /// Replaces an assignment in the set with a symbolic wildcard that can have any value.
    void setSymbolic(const IR::SymbolicVariable &var) {
        auto symbolicVar =
            Z3Cache::set(ToolsVariables::getSymbolicVariable(var.type, var.label + "*"));
        auto it = find(var);
        if (it == end()) {
            emplace(var, symbolicVar);
        } else {
            it->second = symbolicVar;
        }
    }

    /// Set all assignments in this set to a symbolic wildcard that can have any value.
    void setAllSymbolic() {
        for (const auto &[symbol, assignment] : *this) {
            setSymbolic(symbol.get());
        }
    }

    /// Add a new variable to the set. If the variable is already in the set it is wrapped in an ite
    /// statement.
    void addConditionally(const IR::SymbolicVariable &var, const z3::expr &condition,
                          const z3::expr &assignment) {
        auto it = find(var);
        if (it == end()) {
            /// There is only one possible value for the variable. Use it as default.
            /// TODO: Can we make this assumption and still be semantically correct?
            emplace(var, assignment);
            /// Wildcard the result if it does not exist yet.
            // emplace(var, z3::ite(condition, assignment, Z3Cache::set(&var)));
        } else {
            it->second = z3::ite(condition, assignment, it->second).simplify();
        }
    }

    /// Clear the set.
    void clear() {
        ordered_map<std::reference_wrapper<const IR::SymbolicVariable>, z3::expr,
                    IR::StructuralLess>::clear();
    }

    /// Substitutes the given expression with the variables contained in the set.
    /// Use the context of the given expression to build the set.
    [[nodiscard]] z3::expr substitute(z3::expr &toSubstitute) const {
        /// TODO: This is a bit inefficient since we are rebuilding the vectors with each
        /// substitution. The problem is that the vectors are not maps and addConditionally may
        /// conflict.
        z3::expr_vector substitutionVariables(toSubstitute.ctx());
        z3::expr_vector substitutionAssignments(toSubstitute.ctx());
        for (const auto &match : *this) {
            substitutionVariables.push_back(Z3Cache::set(&match.first.get()));
            substitutionAssignments.push_back(match.second);
        }
        return toSubstitute.substitute(substitutionVariables, substitutionAssignments).simplify();
    }

    /// Merges the other set into this one.
    void merge(const Z3ControlPlaneAssignmentSet &other) {
        for (const auto &match : other) {
            add(match.first.get(), match.second);
        }
    }

    /// Merges the other set into this one. Translate the expression in the other set into Z3.
    void merge(const ControlPlaneAssignmentSet &other) {
        for (const auto &match : other) {
            add(match.first.get(), Z3Cache::set(&match.second.get()));
        }
    }

    /// Merges the other set into this one using the provided condition.
    void mergeConditionally(const z3::expr &condition, const Z3ControlPlaneAssignmentSet &other) {
        for (const auto &match : other) {
            addConditionally(match.first.get(), condition, match.second);
        }
    }

    /// Merges the other set into this one using the provided condition. Translate the expression in
    /// the other set into Z3.
    void mergeConditionally(const z3::expr &condition, const ControlPlaneAssignmentSet &other) {
        for (const auto &match : other) {
            addConditionally(match.first.get(), condition, Z3Cache::set(&match.second.get()));
        }
    }
};

}  // namespace P4::P4Tools::Flay

#endif /* BACKENDS_P4TOOLS_MODULES_FLAY_CORE_CONTROL_PLANE_Z3_CONTROL_PLANE_ASSIGNMENT_H_ */
