#ifndef BACKENDS_P4TOOLS_MODULES_FLAY_CORE_CONTROL_PLANE_SYMBOLS_H_
#define BACKENDS_P4TOOLS_MODULES_FLAY_CORE_CONTROL_PLANE_SYMBOLS_H_

#include "ir/compare.h"
#include "ir/ir.h"
#include "ir/irutils.h"

namespace P4::P4Tools::Flay {

/// Utility function to compare IR nodes in a set. We use their source info.
struct SourceIdCmp {
    bool operator()(const IR::Node *s1, const IR::Node *s2) const {
        if (s1->getSourceInfo() == s2->getSourceInfo()) {
            return s1->clone_id < s2->clone_id;
        }
        return s1->getSourceInfo() < s2->getSourceInfo();
    }
};

/// Data structures which simplify the handling of symbolic variables.
using SymbolSet = std::set<std::reference_wrapper<const IR::SymbolicVariable>, IR::StructuralLess>;
using SymbolMap = std::map<std::reference_wrapper<const IR::SymbolicVariable>,
                           std::set<const IR::Node *>, IR::StructuralLess>;
using NodeSet = std::set<const IR::Node *, SourceIdCmp>;
using ExpressionSet = std::set<const IR::Expression *, SourceIdCmp>;

/// Collects all IR::SymbolicVariables in a given IR::Node.
class SymbolCollector : public Inspector {
 private:
    /// The set of symbolic variables.
    SymbolSet _collectedSymbols;

 public:
    SymbolCollector() = default;

    void postorder(const IR::SymbolicVariable *symbolicVariable) override {
        _collectedSymbols.insert(*symbolicVariable);
    }

    /// Returns the collected  symbolic variables.
    [[nodiscard]] const SymbolSet &collectedSymbols() const { return _collectedSymbols; }

    [[nodiscard]] static const SymbolSet &collectSymbols(const IR::Node &node) {
        SymbolCollector symbolCollector;
        node.apply(symbolCollector);
        return symbolCollector.collectedSymbols();
    }
};

}  // namespace P4::P4Tools::Flay

#endif /* BACKENDS_P4TOOLS_MODULES_FLAY_CORE_CONTROL_PLANE_SYMBOLS_H_ */
