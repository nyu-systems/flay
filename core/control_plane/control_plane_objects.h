#ifndef BACKENDS_P4TOOLS_MODULES_FLAY_CORE_CONTROL_PLANE_CONTROL_PLANE_OBJECTS_H_
#define BACKENDS_P4TOOLS_MODULES_FLAY_CORE_CONTROL_PLANE_CONTROL_PLANE_OBJECTS_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <utility>

#include "backends/p4tools/modules/flay/core/control_plane/control_plane_item.h"
#include "ir/ir.h"
#include "ir/irutils.h"

namespace P4::P4Tools::ControlPlaneState {

/// @returns the symbolic boolean variable indicating whether this particular parser value set has
/// been configured by the control plane.
const IR::SymbolicVariable *getParserValueSetConfigured(cstring parserValueSetName);

/// @returns the symbolic string variable that represents the default action that is active for a
/// particular table.
const IR::SymbolicVariable *getDefaultActionVariable(cstring tableName);

}  // namespace P4::P4Tools::ControlPlaneState

namespace P4::P4Tools::Flay {

/// How many entries per table we can support before we fall back to making the entire table
/// configuration symbolic.
/// TODO: Consider making this an option?
constexpr size_t kMaxEntriesPerTable = 50;

/**************************************************************************************************
TableMatchKeys
**************************************************************************************************/
class TableMatchKey : public Z3ControlPlaneItem {
    /// The name of the table associated with this key.
    cstring _tableName;

    /// The control plane identifier for this match key.
    cstring _name;

    /// The computed key expression.
    const IR::Expression *_computedKey = nullptr;

 protected:
    explicit TableMatchKey(cstring tableName, cstring name);

 public:
    /// @returns the name of the table associated with this key.
    [[nodiscard]] cstring tableName() const;

    /// @returns the control plane identifier for this match key.
    [[nodiscard]] cstring name() const;

    /// @returns the computed key expression.
    [[nodiscard]] const IR::Expression *computedKey() const { return _computedKey; }

    /// Sets the computed key expression.
    void setComputedKey(const IR::Expression *key) {
        _computedKey = key;
        Z3Cache::set(key);
    }

    bool operator<(const ControlPlaneItem &other) const override;
    [[nodiscard]] ControlPlaneAssignmentSet computeControlPlaneAssignments() const override;
    [[nodiscard]] Z3ControlPlaneAssignmentSet computeZ3ControlPlaneAssignments() const override;

    [[nodiscard]] virtual const IR::Expression *computeControlPlaneConstraint() const {
        P4C_UNIMPLEMENTED("computeControlPlaneConstraint");
    }

    [[nodiscard]] virtual std::optional<z3::expr> computeZ3ControlPlaneConstraint() const {
        P4C_UNIMPLEMENTED("computeZ3ControlPlaneConstraint");
    }

    DECLARE_TYPEINFO(TableMatchKey);
};

/// An exact match kind on a key field means that the value of the field in the table specifies
/// exactly the value the lookup key field must have in order to match. This is applicable for all
/// legal key fields whose types support equality comparisons.
class ExactTableMatchKey : public TableMatchKey {
    /// The symbolic variable that represents the table match key.
    const IR::SymbolicVariable *_variable;

    /// The value expression the key must match on.
    const IR::Expression *_keyExpression;

 public:
    explicit ExactTableMatchKey(cstring tableName, cstring name,
                                const IR::Expression *keyExpression);

    /// @returns the symbolic variable that represents the table match key.
    [[nodiscard]] const IR::SymbolicVariable *variable() const;

    /// @returns the value expression the key must match on.
    [[nodiscard]] const IR::Expression *keyExpression() const;

    [[nodiscard]] const IR::Expression *computeControlPlaneConstraint() const override;
    [[nodiscard]] std::optional<z3::expr> computeZ3ControlPlaneConstraint() const override;

    DECLARE_TYPEINFO(ExactTableMatchKey);
};

/// A ternary match kind on a key field means that the field in the table specifies a set of values
/// for the key field using a value and a mask. The meaning of the (value, mask) pair is similar to
/// the P4 mask expressions, as described in Section 8.15.3: a key field k matches the table entry
/// when k & mask == value & mask.
class TernaryTableMatchKey : public TableMatchKey {
    /// The symbolic variable that represents the table match key.
    const IR::SymbolicVariable *_variable;

    /// The mask for the table match key.
    const IR::SymbolicVariable *_mask;

    /// The value expression the key must match on.
    const IR::Expression *_keyExpression;

 public:
    TernaryTableMatchKey(cstring tableName, cstring name, const IR::Expression *keyExpression);

    /// @returns the symbolic variable that represents the table match key.
    [[nodiscard]] const IR::SymbolicVariable *variable() const;

    /// @returns the value expression the key must match on.
    [[nodiscard]] const IR::Expression *keyExpression() const;

    /// @returns the mask for the table match key.
    [[nodiscard]] const IR::SymbolicVariable *mask() const;

    [[nodiscard]] const IR::Expression *computeControlPlaneConstraint() const override;
    [[nodiscard]] std::optional<z3::expr> computeZ3ControlPlaneConstraint() const override;

    DECLARE_TYPEINFO(TernaryTableMatchKey);
};

/// An lpm (longest prefix match) match kind on a key field is a specific type of ternary match
/// where the mask is required to have a form in binary that is a contiguous set of 1 bits followed
/// by a contiguous set of 0 bits. Masks with more 1 bits have automatically higher priorities. A
/// mask with all bits 0 is legal.
class LpmTableMatchKey : public TableMatchKey {
    /// The symbolic variable that represents the table match key.
    const IR::SymbolicVariable *_variable;

    /// The value expression the key must match on.
    const IR::Expression *_keyExpression;

    /// The symbolic variable that represents the prefix.
    const IR::SymbolicVariable *_prefixVar;

 public:
    LpmTableMatchKey(cstring tableName, cstring name, const IR::Expression *keyExpression);

    /// @returns the symbolic variable that represents the table match key.
    [[nodiscard]] const IR::SymbolicVariable *variable() const;

    /// @returns the value expression the key must match on.
    [[nodiscard]] const IR::Expression *keyExpression() const;

    /// @returns the symbolic variable that represents the prefix.
    [[nodiscard]] const IR::SymbolicVariable *prefix() const;

    [[nodiscard]] const IR::Expression *computeControlPlaneConstraint() const override;
    [[nodiscard]] std::optional<z3::expr> computeZ3ControlPlaneConstraint() const override;

    DECLARE_TYPEINFO(LpmTableMatchKey);
};

/// See
/// https://github.com/p4lang/behavioral-model/blob/main/docs/simple_switch.md#table-match-kinds-supported
class OptionalMatchKey : public TableMatchKey {
    /// The symbolic variable that represents the table match key.
    const IR::SymbolicVariable *_variable;

    /// The value expression the key must match on.
    const IR::Expression *_keyExpression;

 public:
    explicit OptionalMatchKey(cstring tableName, cstring name, const IR::Expression *keyExpression);

    /// @returns the symbolic variable that represents the table match key.
    [[nodiscard]] const IR::SymbolicVariable *variable() const;

    /// @returns the value expression the key must match on.
    [[nodiscard]] const IR::Expression *keyExpression() const;

    [[nodiscard]] const IR::Expression *computeControlPlaneConstraint() const override;
    [[nodiscard]] std::optional<z3::expr> computeZ3ControlPlaneConstraint() const override;

    DECLARE_TYPEINFO(OptionalMatchKey);
};

/// See
/// https://github.com/p4lang/behavioral-model/blob/main/docs/simple_switch.md#table-match-kinds-supported
class SelectorMatchKey : public TableMatchKey {
    /// The symbolic variable that represents the table match key.
    const IR::SymbolicVariable *_variable;

    /// The value expression the key must match on.
    const IR::Expression *_keyExpression;

 public:
    explicit SelectorMatchKey(cstring tableName, cstring name, const IR::Expression *keyExpression);

    /// @returns the symbolic variable that represents the table match key.
    [[nodiscard]] const IR::SymbolicVariable *variable() const;

    /// @returns the value expression the key must match on.
    [[nodiscard]] const IR::Expression *keyExpression() const;

    [[nodiscard]] const IR::Expression *computeControlPlaneConstraint() const override;
    [[nodiscard]] std::optional<z3::expr> computeZ3ControlPlaneConstraint() const override;

    DECLARE_TYPEINFO(SelectorMatchKey);
};

class RangeTableMatchKey : public TableMatchKey {
    /// The symbolic variable that represents the minimum and maximum range of the key.
    std::pair<const IR::SymbolicVariable *, const IR::SymbolicVariable *> _range;

    /// The value expression the key must match on.
    const IR::Expression *_keyExpression;

 public:
    RangeTableMatchKey(cstring tableName, cstring name, const IR::Expression *keyExpression);

    /// @returns the symbolic variable that represents the minimum table match key.
    [[nodiscard]] const IR::SymbolicVariable *minKey() const;

    /// @returns the symbolic variable that represents the maximum table match key.
    [[nodiscard]] const IR::SymbolicVariable *maxKey() const;

    /// @returns the value expression the key must match on.
    [[nodiscard]] const IR::Expression *keyExpression() const;

    [[nodiscard]] const IR::Expression *computeControlPlaneConstraint() const override;
    [[nodiscard]] std::optional<z3::expr> computeZ3ControlPlaneConstraint() const override;

    DECLARE_TYPEINFO(RangeTableMatchKey);
};

/**************************************************************************************************
TableMatchEntry
**************************************************************************************************/

class TableMatchEntry : public Z3ControlPlaneItem {
 protected:
    /// The action that will be executed by this entry.
    ControlPlaneAssignmentSet _actionAssignment;

    /// The action that will be executed by this entry in Z3 form.
    Z3ControlPlaneAssignmentSet _z3ActionAssignment;

    /// The priority of this entry.
    int32_t _priority;

    /// The set of control plane assignments produced by this entry.
    ControlPlaneAssignmentSet _matches;

    /// The action that will be executed by this entry in Z3 form.
    Z3ControlPlaneAssignmentSet _z3Matches;

    /// The condition of this entry. Can only be set by the parent table configuration.
    std::optional<z3::expr> _condition;

 public:
    explicit TableMatchEntry(ControlPlaneAssignmentSet actionAssignment, int32_t priority,
                             ControlPlaneAssignmentSet matches);

    /// @returns the action that will be executed by this entry.
    [[nodiscard]] ControlPlaneAssignmentSet actionAssignment() const;

    /// @returns the action that will be executed by this entry.
    [[nodiscard]] Z3ControlPlaneAssignmentSet z3ActionAssignment() const;

    /// @returns the priority of this entry.
    [[nodiscard]] int32_t priority() const;

    /// @returns the condition to execute this entry. Set by the parent table.
    std::optional<z3::expr> _z3Condition() const {
        if (_condition.has_value()) {
            return _condition.value();
        }
        error("Condition not set for entry.");
        return std::nullopt;
    }

    void setZ3Condition(z3::expr condition) { _condition = _z3Matches.substitute(condition); }

    bool operator<(const ControlPlaneItem &other) const override;

    [[nodiscard]] ControlPlaneAssignmentSet computeControlPlaneAssignments() const override;
    [[nodiscard]] Z3ControlPlaneAssignmentSet computeZ3ControlPlaneAssignments() const override;

    DECLARE_TYPEINFO(TableMatchEntry);
};

/**************************************************************************************************
TableDefaultAction
**************************************************************************************************/

class TableDefaultAction : public Z3ControlPlaneItem {
    /// The action that will be executed by this entry.
    ControlPlaneAssignmentSet _actionAssignment;

    /// The action that will be executed by this entry in Z3 form.
    Z3ControlPlaneAssignmentSet _z3ActionAssignment;

 public:
    explicit TableDefaultAction(ControlPlaneAssignmentSet actionAssignment);

    bool operator<(const ControlPlaneItem &other) const override;

    [[nodiscard]] ControlPlaneAssignmentSet computeControlPlaneAssignments() const override;
    [[nodiscard]] Z3ControlPlaneAssignmentSet computeZ3ControlPlaneAssignments() const override;

    DECLARE_TYPEINFO(TableDefaultAction);
};

/**************************************************************************************************
WildCardMatchEntry
**************************************************************************************************/

/// A wildcard table match entry can be used to match all possible actions and does not impose
/// constraints on key values.
class WildCardMatchEntry : public TableMatchEntry {
 public:
    explicit WildCardMatchEntry(ControlPlaneAssignmentSet actionAssignment, int32_t priority);

    [[nodiscard]] ControlPlaneAssignmentSet computeControlPlaneAssignments() const override;
    [[nodiscard]] Z3ControlPlaneAssignmentSet computeZ3ControlPlaneAssignments() const override;

    DECLARE_TYPEINFO(WildCardMatchEntry);
};

/**************************************************************************************************
TableConfiguration
**************************************************************************************************/

/// The active set of table entries. Sorted by type.
using TableEntrySet = std::set<std::reference_wrapper<TableMatchEntry>, std::less<TableMatchEntry>>;

using KeyMap = std::vector<const TableMatchKey *>;

/// Concrete configuration of a control plane table. May contain arbitrary many table match
/// entries.
class TableConfiguration : public Z3ControlPlaneItem {
    /// The control plane name of the table that is being configured.
    cstring _tableName;

    /// The default behavior of the table when it is not configured.
    TableDefaultAction _defaultTableAction;

    /// The set of table entries in the configuration.
    TableEntrySet _tableEntries;

    /// The match key expression for the table . This is derived from the data-plane analysis.
    const IR::Expression *_tableKeyMatch = IR::BoolLiteral::get(false);

    /// Second-order sorting function for table entries. Sorts entries by priority.
    class CompareTableMatch {
     public:
        bool operator()(const TableMatchEntry &left, const TableMatchEntry &right);
    };

    /// Produce a single key match expression from a map of keys.
    static const IR::Expression *buildKeyMatches(const KeyMap &keyMap);

 public:
    explicit TableConfiguration(cstring tableName, TableDefaultAction defaultTableAction,
                                TableEntrySet tableEntries);

    bool operator<(const ControlPlaneItem &other) const override;

    /// Set the table key match expression.
    void setTableKeyMatch(const KeyMap &tableKeyMap);

    /// Adds a new table entry.
    int addTableEntry(TableMatchEntry &tableMatchEntry, bool replace);

    /// Delete an existing table entry.
    size_t deleteTableEntry(TableMatchEntry &tableMatchEntry);

    /// Clear all table entries.
    void clearTableEntries();

    /// Set the default action for this table.
    void setDefaultTableAction(TableDefaultAction defaultTableAction);

    [[nodiscard]] ControlPlaneAssignmentSet computeControlPlaneAssignments() const override;
    [[nodiscard]] Z3ControlPlaneAssignmentSet computeZ3ControlPlaneAssignments() const override;

    DECLARE_TYPEINFO(TableConfiguration);
};

/**************************************************************************************************
ParserValueSet
**************************************************************************************************/

/// Implements a parser value set as specified in
/// https://p4.org/p4-spec/docs/P4-16-working-spec.html#sec-value-set.
/// TODO: Actually implement all the elments in the value set.
class ParserValueSet : public Z3ControlPlaneItem {
    cstring _name;

 public:
    explicit ParserValueSet(cstring name);

    bool operator<(const ControlPlaneItem &other) const override;

    [[nodiscard]] ControlPlaneAssignmentSet computeControlPlaneAssignments() const override;
    [[nodiscard]] Z3ControlPlaneAssignmentSet computeZ3ControlPlaneAssignments() const override;

    DECLARE_TYPEINFO(ParserValueSet);
};

/**************************************************************************************************
ActionProfile
**************************************************************************************************/

/// An action profile. Action profiles are programmed like a table, but each table associated
/// with the respective table shares the action profile configuration. Hence, we use a set of
/// table control plane names to represent this data structure.
class ActionProfile : public Z3ControlPlaneItem {
    /// The control plane name of the action profile.
    cstring _name;

    /// The control plane names of the tables associated with this action profile.
    std::set<cstring> _associatedTables;

 public:
    explicit ActionProfile(cstring name) : _name(name) {};
    explicit ActionProfile(cstring name, std::set<cstring> associatedTables)
        : _name(name), _associatedTables(std::move(associatedTables)) {}

    bool operator<(const ControlPlaneItem &other) const override;

    /// @returns the control plane name of the action profile.
    [[nodiscard]] cstring name() const { return _name; }

    /// Get the set of control plane names of the tables associated with this action profile.
    [[nodiscard]] const std::set<cstring> &associatedTables() const;

    /// Add the control plane name of a  table to the set of associated tables.
    void addAssociatedTable(cstring table);

    [[nodiscard]] ControlPlaneAssignmentSet computeControlPlaneAssignments() const override;
    [[nodiscard]] Z3ControlPlaneAssignmentSet computeZ3ControlPlaneAssignments() const override;

    DECLARE_TYPEINFO(ActionProfile);
};

/**************************************************************************************************
ActionSelector
**************************************************************************************************/

/// An action selector. Action selectors are programmed like a table, but each table associated
/// with the respective table shares the action selector configuration. Hence, we use a set of
/// table control plane names to represent this data structure.
class ActionSelector : public Z3ControlPlaneItem {
    /// The reference to the action profile associated with the selector.
    std::reference_wrapper<ActionProfile> _actionProfile;

    /// The control plane names of the tables associated with this action profile.
    std::set<cstring> _associatedTables;

 public:
    explicit ActionSelector(ActionProfile &actionProfile) : _actionProfile(actionProfile) {};
    explicit ActionSelector(ActionProfile &actionProfile, std::set<cstring> associatedTables)
        : _actionProfile(actionProfile), _associatedTables(std::move(associatedTables)) {}

    bool operator<(const ControlPlaneItem &other) const override;

    /// Get the reference to the action profile associated with the selector.
    [[nodiscard]] const ActionProfile &actionProfile() const;

    /// Get the set of control plane names of the tables associated with this action profile.
    [[nodiscard]] const std::set<cstring> &associatedTables() const;

    /// Add the control plane name of a  table to the set of associated tables.
    void addAssociatedTable(cstring table);

    [[nodiscard]] ControlPlaneAssignmentSet computeControlPlaneAssignments() const override;
    [[nodiscard]] Z3ControlPlaneAssignmentSet computeZ3ControlPlaneAssignments() const override;

    DECLARE_TYPEINFO(ActionSelector);
};

/**************************************************************************************************
TableActionSelectorConfiguration
**************************************************************************************************/
class TableActionSelectorConfiguration : public TableConfiguration {
 public:
    explicit TableActionSelectorConfiguration(cstring tableName,
                                              TableDefaultAction defaultTableAction,
                                              TableEntrySet tableEntries);

    [[nodiscard]] ControlPlaneAssignmentSet computeControlPlaneAssignments() const override;
    [[nodiscard]] Z3ControlPlaneAssignmentSet computeZ3ControlPlaneAssignments() const override;

    DECLARE_TYPEINFO(TableActionSelectorConfiguration);
};

}  // namespace P4::P4Tools::Flay

#endif /* BACKENDS_P4TOOLS_MODULES_FLAY_CORE_CONTROL_PLANE_CONTROL_PLANE_OBJECTS_H_ */
