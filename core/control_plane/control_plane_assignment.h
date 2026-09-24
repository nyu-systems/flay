#ifndef BACKENDS_P4TOOLS_MODULES_FLAY_CORE_CONTROL_PLANE_CONTROL_PLANE_ASSIGNMENT_H_
#define BACKENDS_P4TOOLS_MODULES_FLAY_CORE_CONTROL_PLANE_CONTROL_PLANE_ASSIGNMENT_H_

#include <z3++.h>

#include <algorithm>
#include <tuple>

#include "backends/p4tools/modules/flay/core/lib/z3_cache.h"
#include "ir/compare.h"
#include "ir/ir.h"
#include "ir/node.h"

namespace P4::P4Tools::Flay {

/// The set of concrete mappings of symbolic control plane variables for table match keys.
/// TODO: Make this an unordered set.
using ControlPlaneAssignmentPointerPair =
    std::pair<const IR::SymbolicVariable *, const IR::Expression *>;
using ControlPlaneAssignmentReferencePair =
    std::pair<std::reference_wrapper<const IR::SymbolicVariable>,
              std::reference_wrapper<const IR::Expression>>;
/// Lexicographical structural ordering of a symbol and its assigned value.
struct StructuralAssignmentLess {
    bool operator()(const ControlPlaneAssignmentPointerPair &s1,
                    const ControlPlaneAssignmentPointerPair &s2) const {
        return IR::structuralCompare(s1, s2) < 0;
    }
    bool operator()(const ControlPlaneAssignmentReferencePair &s1,
                    const ControlPlaneAssignmentReferencePair &s2) const {
        return IR::structuralCompare(std::tie(s1.first.get(), s1.second.get()),
                                     std::tie(s2.first.get(), s2.second.get())) < 0;
    }
};

using ControlPlaneAssignmentSet =
    ordered_map<std::reference_wrapper<const IR::SymbolicVariable>,
                std::reference_wrapper<const IR::Expression>, IR::StructuralLess>;

/// Compare assignments lexicographically in insertion order, with keys before values.
/// Structural equivalence, rather than equiv(), determines when to compare the next field.
inline std::weak_ordering structuralCompare(const ControlPlaneAssignmentSet &s1,
                                            const ControlPlaneAssignmentSet &s2) {
    return std::lexicographical_compare_three_way(
        s1.begin(), s1.end(), s2.begin(), s2.end(), [](const auto &a, const auto &b) {
            return IR::structuralCompare(std::tie(a.first.get(), a.second.get()),
                                         std::tie(b.first.get(), b.second.get()));
        });
}

}  // namespace P4::P4Tools::Flay

#endif /* BACKENDS_P4TOOLS_MODULES_FLAY_CORE_CONTROL_PLANE_CONTROL_PLANE_ASSIGNMENT_H_ */
