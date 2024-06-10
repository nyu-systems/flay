#ifndef BACKENDS_P4TOOLS_MODULES_FLAY_CONTROL_PLANE_CONTROL_PLANE_OBJECTS_H_
#define BACKENDS_P4TOOLS_MODULES_FLAY_CONTROL_PLANE_CONTROL_PLANE_OBJECTS_H_

#include <cstdint>
#include <functional>
#include <set>
#include <utility>

#include "backends/p4tools/modules/flay/control_plane/control_plane_item.h"
#include "ir/ir.h"
#include "ir/irutils.h"
#include "ir/solver.h"

namespace P4Tools::ControlPlaneState {

/// @returns the symbolic boolean variable indicating whether this particular parser value set has
/// been configured by the control plane.
const IR::SymbolicVariable *getParserValueSetConfigured(cstring parserValueSetName);

/// @returns the symbolic string variable that represents the default action that is active for a
/// particular table.
const IR::SymbolicVariable *getDefaultActionVariable(cstring tableName);

}  // namespace P4Tools::ControlPlaneState

namespace P4Tools::Flay {

/// The set of concrete mappings of symbolic control plane variables for table match keys.
/// TODO: Make this an unordered set.
using TableKeyPointerPair = std::pair<const IR::SymbolicVariable *, const IR::Literal *>;
using TableKeyReferencePair = std::pair<std::reference_wrapper<const IR::SymbolicVariable>,
                                        std::reference_wrapper<const IR::Literal>>;
struct IsSemanticallyLessPairComparator {
    bool operator()(const TableKeyPointerPair &s1, const TableKeyPointerPair &s2) const {
        if (!s1.first->equiv(*s2.first)) {
            return s1.first->isSemanticallyLess(*s2.first);
        }
        return s1.second->isSemanticallyLess(*s2.second);
    }
    bool operator()(const TableKeyReferencePair &s1, const TableKeyReferencePair &s2) const {
        if (!s1.first.get().equiv(s2.first)) {
            return s1.first.get().isSemanticallyLess(s2.first);
        }
        return s1.second.get().isSemanticallyLess(s2.second);
    }
};
using TableKeySet = std::set<TableKeyReferencePair, IsSemanticallyLessPairComparator>;

/**************************************************************************************************
TableMatchEntry
**************************************************************************************************/

class TableMatchEntry : public ControlPlaneItem {
 protected:
    /// The action that will be executed by this entry.
    const Constraint *actionAssignment;

    /// The priority of this entry.
    int32_t priority;

    /// The expression which needs to be true to execute the action.
    const IR::Expression *matchExpression;

    /// Computes an expression from a set of matches.
    [[nodiscard]] static const IR::Expression *computeMatchExpression(const TableKeySet &matches);

 public:
    explicit TableMatchEntry(const Constraint *actionAssignment, int32_t priority,
                             const TableKeySet &matches);

    /// @returns the action that will be executed by this entry.
    [[nodiscard]] const Constraint *getActionAssignment() const;

    /// @returns the priority of this entry.
    [[nodiscard]] int32_t getPriority() const;

    bool operator<(const ControlPlaneItem &other) const override;

    [[nodiscard]] const IR::Expression *computeControlPlaneConstraint() const override;

    DECLARE_TYPEINFO(TableMatchEntry);
};

/**************************************************************************************************
WildCardMatchEntry
**************************************************************************************************/

/// A wildcard table match entry can be used to match all possible actions and does not impose
/// constraints on key values.
class WildCardMatchEntry : public TableMatchEntry {
 public:
    explicit WildCardMatchEntry(const Constraint *actionAssignment, int32_t priority);

    bool operator<(const ControlPlaneItem &other) const override;

    [[nodiscard]] const IR::Expression *computeControlPlaneConstraint() const override;

    DECLARE_TYPEINFO(WildCardMatchEntry);
};

/**************************************************************************************************
TableDefaultAction
**************************************************************************************************/

class TableDefaultAction : public ControlPlaneItem {
    /// The action that will be executed by this entry.
    const Constraint *actionAssignment_;

 public:
    explicit TableDefaultAction(const Constraint *actionAssignment)
        : actionAssignment_(actionAssignment) {}

    /// @returns the action that will be executed by this entry.
    [[nodiscard]] const Constraint *getActionAssignment() const { return actionAssignment_; }

    bool operator<(const ControlPlaneItem &other) const override {
        // Table match entries are only compared based on the match expression.
        return typeid(*this) == typeid(other)
                   ? actionAssignment_->isSemanticallyLess(
                         *(dynamic_cast<const TableDefaultAction &>(other)).actionAssignment_)
                   : typeid(*this).hash_code() < typeid(other).hash_code();
    }

    [[nodiscard]] const IR::Expression *computeControlPlaneConstraint() const override {
        return actionAssignment_;
    }

    DECLARE_TYPEINFO(TableDefaultAction);
};

/**************************************************************************************************
TableConfiguration
**************************************************************************************************/

/// The active set of table entries. Sorted by type.
using TableEntrySet =
    std::set<std::reference_wrapper<const TableMatchEntry>, std::less<const TableMatchEntry>>;

/// Concrete configuration of a control plane table. May contain arbitrary many table match entries.
class TableConfiguration : public ControlPlaneItem {
    /// The control plane name of the table that is being configured.
    cstring tableName_;

    /// The default behavior of the table when it is not configured.
    TableDefaultAction defaultTableAction_;

    /// The set of table entries in the configuration.
    TableEntrySet tableEntries_;

    /// Second-order sorting function for table entries. Sorts entries by priority.
    class CompareTableMatch {
     public:
        bool operator()(const TableMatchEntry &left, const TableMatchEntry &right);
    };

 public:
    explicit TableConfiguration(cstring tableName, TableDefaultAction defaultTableAction,
                                TableEntrySet tableEntries);

    bool operator<(const ControlPlaneItem &other) const override;

    /// Adds a new table entry.
    int addTableEntry(const TableMatchEntry &tableMatchEntry, bool replace);

    /// Delete an existing table entry.
    size_t deleteTableEntry(const TableMatchEntry &tableMatchEntry);

    /// Clear all table entries.
    void clearTableEntries();

    /// Set the default action for this table.
    void setDefaultTableAction(TableDefaultAction defaultTableAction);

    [[nodiscard]] const IR::Expression *computeControlPlaneConstraint() const override;

    DECLARE_TYPEINFO(TableConfiguration);
};

/**************************************************************************************************
ParserValueSet
**************************************************************************************************/

/// Implements a parser value set as specified in
/// https://p4.org/p4-spec/docs/P4-16-working-spec.html#sec-value-set.
/// TODO: Actually implement all the elments in the value set.
class ParserValueSet : public ControlPlaneItem {
    cstring _name;

 public:
    explicit ParserValueSet(cstring name);

    bool operator<(const ControlPlaneItem &other) const override;

    [[nodiscard]] const IR::Expression *computeControlPlaneConstraint() const override;

    DECLARE_TYPEINFO(ParserValueSet);
};

/**************************************************************************************************
ActionProfile
**************************************************************************************************/

/// An action profile. Action profiles are programmed like a table, but each table associated
/// with the respective table shares the action profile configuration. Hence, we use a set of table
/// control plane names to represent this data structure.
class ActionProfile : public ControlPlaneItem {
    /// The control plane name of the action profile.
    cstring _name;

    /// The control plane names of the tables associated with this action profile.
    std::set<cstring> _associatedTables;

 public:
    explicit ActionProfile(cstring name) : _name(name){};
    explicit ActionProfile(cstring name, std::set<cstring> associatedTables)
        : _name(name), _associatedTables(std::move(associatedTables)) {}

    bool operator<(const ControlPlaneItem &other) const override;

    /// @returns the control plane name of the action profile.
    [[nodiscard]] cstring name() const { return _name; }

    /// Get the set of control plane names of the tables associated with this action profile.
    [[nodiscard]] const std::set<cstring> &associatedTables() const;

    /// Add the control plane name of a  table to the set of associated tables.
    void addAssociatedTable(cstring table);

    [[nodiscard]] const IR::Expression *computeControlPlaneConstraint() const override;

    DECLARE_TYPEINFO(ActionProfile);
};

/**************************************************************************************************
ActionSelector
**************************************************************************************************/

/// An action selector. Action selectors are programmed like a table, but each table associated
/// with the respective table shares the action selector configuration. Hence, we use a set of table
/// control plane names to represent this data structure.
class ActionSelector : public ControlPlaneItem {
    /// The reference to the action profile associated with the selector.
    std::reference_wrapper<ActionProfile> _actionProfile;

    /// The control plane names of the tables associated with this action profile.
    std::set<cstring> _associatedTables;

 public:
    explicit ActionSelector(ActionProfile &actionProfile) : _actionProfile(actionProfile){};
    explicit ActionSelector(ActionProfile &actionProfile, std::set<cstring> associatedTables)
        : _actionProfile(actionProfile), _associatedTables(std::move(associatedTables)) {}

    bool operator<(const ControlPlaneItem &other) const override;

    /// Get the reference to the action profile associated with the selector.
    [[nodiscard]] const ActionProfile &actionProfile() const;

    /// Get the set of control plane names of the tables associated with this action profile.
    [[nodiscard]] const std::set<cstring> &associatedTables() const;

    /// Add the control plane name of a  table to the set of associated tables.
    void addAssociatedTable(cstring table);

    [[nodiscard]] const IR::Expression *computeControlPlaneConstraint() const override;

    DECLARE_TYPEINFO(ActionSelector);
};

/**************************************************************************************************
TableActionSelectorConfiguration
**************************************************************************************************/

class TableActionSelectorConfiguration : public TableConfiguration {
 public:
    explicit TableActionSelectorConfiguration(cstring tableName,
                                              TableDefaultAction defaultTableAction,
                                              TableEntrySet tableEntries)
        : TableConfiguration(tableName, std::move(defaultTableAction), std::move(tableEntries)) {}

    [[nodiscard]] const IR::Expression *computeControlPlaneConstraint() const override {
        // This does nothing currently.
        return IR::BoolLiteral::get(true);
    }

    DECLARE_TYPEINFO(TableActionSelectorConfiguration);
};

}  // namespace P4Tools::Flay

#endif /* BACKENDS_P4TOOLS_MODULES_FLAY_CONTROL_PLANE_CONTROL_PLANE_OBJECTS_H_ */
