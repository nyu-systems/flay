#include "backends/p4tools/modules/flay/core/control_plane/control_plane_objects.h"

#include <cstdlib>
#include <utility>

#include "backends/p4tools/common/lib/symbolic_variables.h"
#include "backends/p4tools/common/lib/variables.h"
#include "backends/p4tools/modules/flay/core/control_plane/substitute_variable.h"
#include "backends/p4tools/modules/flay/core/lib/simplify_expression.h"
#include "backends/p4tools/modules/flay/core/lib/z3_cache.h"
#include "ir/irutils.h"
#include "lib/timer.h"

namespace P4::P4Tools::ControlPlaneState {

const IR::SymbolicVariable *getParserValueSetConfigured(cstring parserValueSetName) {
    return ToolsVariables::getSymbolicVariable(IR::Type_Boolean::get(),
                                               "pvs_configured_" + parserValueSetName);
}

const IR::SymbolicVariable *getDefaultActionVariable(cstring tableName) {
    return new IR::SymbolicVariable(IR::Type_String::get(), tableName + "_default_action");
}

}  // namespace P4::P4Tools::ControlPlaneState

namespace P4::P4Tools::Flay {

/**************************************************************************************************
TableMatchKeys
**************************************************************************************************/

TableMatchKey::TableMatchKey(cstring tableName, cstring name)
    : _tableName(tableName), _name(name) {};

cstring TableMatchKey::tableName() const { return _tableName; }

cstring TableMatchKey::name() const { return _name; }

bool TableMatchKey::operator<(const ControlPlaneItem &other) const {
    return typeid(*this) == typeid(other) ? _name < other.as<TableMatchKey>()._name
                                          : typeid(*this).hash_code() < typeid(other).hash_code();
}

ControlPlaneAssignmentSet TableMatchKey::computeControlPlaneAssignments() const {
    P4C_UNIMPLEMENTED("computeControlPlaneAssignments");
}

Z3ControlPlaneAssignmentSet TableMatchKey::computeZ3ControlPlaneAssignments() const {
    P4C_UNIMPLEMENTED("computeZ3ControlPlaneAssignments");
}

ExactTableMatchKey::ExactTableMatchKey(cstring tableName, cstring name, const IR::Expression *value)
    : TableMatchKey(tableName, name),
      _variable(ControlPlaneState::getTableKey(tableName, name, value->type)),
      _keyExpression(value) {
    setComputedKey(new IR::Equ(_variable, value));
}

const IR::SymbolicVariable *ExactTableMatchKey::variable() const { return _variable; }

const IR::Expression *ExactTableMatchKey::keyExpression() const { return _keyExpression; }

const IR::Expression *ExactTableMatchKey::computeControlPlaneConstraint() const {
    return computedKey();
}

std::optional<z3::expr> ExactTableMatchKey::computeZ3ControlPlaneConstraint() const {
    return Z3Cache::get(computedKey());
}

namespace {

const IR::Expression *createTernaryKey(const IR::SymbolicVariable *variable,
                                       const IR::SymbolicVariable *mask,
                                       const IR::Expression *keyExpression) {
    return new IR::Equ(new IR::BAnd(keyExpression, mask), new IR::BAnd(variable, mask));
}

}  // namespace

TernaryTableMatchKey::TernaryTableMatchKey(cstring tableName, cstring name,
                                           const IR::Expression *keyExpression)
    : TableMatchKey(tableName, name),
      _variable(ControlPlaneState::getTableKey(tableName, name, keyExpression->type)),
      _mask(ControlPlaneState::getTableTernaryMask(tableName, name, keyExpression->type)),
      _keyExpression(keyExpression) {
    setComputedKey(createTernaryKey(_variable, _mask, keyExpression));
}

const IR::SymbolicVariable *TernaryTableMatchKey::variable() const { return _variable; }

const IR::Expression *TernaryTableMatchKey::keyExpression() const { return _keyExpression; }

const IR::SymbolicVariable *TernaryTableMatchKey::mask() const { return _mask; }

const IR::Expression *TernaryTableMatchKey::computeControlPlaneConstraint() const {
    return computedKey();
}

std::optional<z3::expr> TernaryTableMatchKey::computeZ3ControlPlaneConstraint() const {
    return Z3Cache::get(computedKey());
}

namespace {

const IR::Expression *createLpmKey(const IR::SymbolicVariable *variable,
                                   const IR::SymbolicVariable *prefixVar,
                                   const IR::Expression *keyExpression) {
    // The maxReturn is the maximum vale for the given bit width. This value is
    // shifted by the mask variable to create a mask (and with that, a prefix).
    const auto *keyType = variable->type;
    auto keyWidth = keyType->width_bits();
    auto maxReturn = IR::getMaxBvVal(keyWidth);
    const IR::Expression *lpmMask = new IR::Shl(IR::Constant::get(keyType, maxReturn), prefixVar);
    return new IR::Equ(new IR::BAnd(keyExpression, lpmMask), new IR::BAnd(variable, lpmMask));
}

}  // namespace

LpmTableMatchKey::LpmTableMatchKey(cstring tableName, cstring name,
                                   const IR::Expression *keyExpression)
    : TableMatchKey(tableName, name),
      _variable(ControlPlaneState::getTableKey(tableName, name, keyExpression->type)),
      _keyExpression(keyExpression),
      _prefixVar(ControlPlaneState::getTableMatchLpmPrefix(tableName, name, keyExpression->type)) {
    setComputedKey(createLpmKey(_variable, _prefixVar, _keyExpression));
}

const IR::SymbolicVariable *LpmTableMatchKey::variable() const { return _variable; }

const IR::Expression *LpmTableMatchKey::keyExpression() const { return _keyExpression; }

const IR::SymbolicVariable *LpmTableMatchKey::prefix() const { return _prefixVar; }

const IR::Expression *LpmTableMatchKey::computeControlPlaneConstraint() const {
    return computedKey();
}

std::optional<z3::expr> LpmTableMatchKey::computeZ3ControlPlaneConstraint() const {
    return Z3Cache::get(computedKey());
}

OptionalMatchKey::OptionalMatchKey(cstring tableName, cstring name, const IR::Expression *value)
    : TableMatchKey(tableName, name),
      _variable(ControlPlaneState::getTableKey(tableName, name, value->type)),
      _keyExpression(value) {
    setComputedKey(new IR::Equ(_variable, value));
}

const IR::SymbolicVariable *OptionalMatchKey::variable() const { return _variable; }

const IR::Expression *OptionalMatchKey::keyExpression() const { return _keyExpression; }

const IR::Expression *OptionalMatchKey::computeControlPlaneConstraint() const {
    return computedKey();
}

std::optional<z3::expr> OptionalMatchKey::computeZ3ControlPlaneConstraint() const {
    return Z3Cache::get(computedKey());
}

SelectorMatchKey::SelectorMatchKey(cstring tableName, cstring name,
                                   const IR::Expression *keyExpression)
    : TableMatchKey(tableName, name),
      _variable(ControlPlaneState::getTableKey(tableName, name, keyExpression->type)),
      _keyExpression(keyExpression) {
    setComputedKey(new IR::Equ(_variable, keyExpression));
}

const IR::SymbolicVariable *SelectorMatchKey::variable() const { return _variable; }

const IR::Expression *SelectorMatchKey::keyExpression() const { return _keyExpression; }

const IR::Expression *SelectorMatchKey::computeControlPlaneConstraint() const {
    return computedKey();
}

std::optional<z3::expr> SelectorMatchKey::computeZ3ControlPlaneConstraint() const {
    return Z3Cache::get(computedKey());
}

namespace {

const IR::Expression *createRangeKey(const IR::SymbolicVariable *minKey,
                                     const IR::SymbolicVariable *maxKey,
                                     const IR::Expression *keyExpression) {
    return new IR::LAnd(
        new IR::LAnd(new IR::Lss(minKey, maxKey), new IR::Leq(minKey, keyExpression)),
        new IR::Leq(keyExpression, maxKey));
}

}  // namespace

RangeTableMatchKey::RangeTableMatchKey(cstring tableName, cstring name,
                                       const IR::Expression *keyExpression)

    : TableMatchKey(tableName, name),
      _range(Bmv2ControlPlaneState::getTableRange(tableName, name, keyExpression->type)),
      _keyExpression(keyExpression) {
    setComputedKey(createRangeKey(_range.first, _range.second, keyExpression));
}

const IR::SymbolicVariable *RangeTableMatchKey::minKey() const { return _range.first; }

const IR::SymbolicVariable *RangeTableMatchKey::maxKey() const { return _range.second; }

const IR::Expression *RangeTableMatchKey::keyExpression() const { return _keyExpression; }

const IR::Expression *RangeTableMatchKey::computeControlPlaneConstraint() const {
    return computedKey();
}

std::optional<z3::expr> RangeTableMatchKey::computeZ3ControlPlaneConstraint() const {
    return Z3Cache::get(computedKey());
}

/**************************************************************************************************
TableMatchEntry
**************************************************************************************************/

TableMatchEntry::TableMatchEntry(ControlPlaneAssignmentSet actionAssignment, int32_t priority,
                                 ControlPlaneAssignmentSet matches)
    : _actionAssignment(std::move(actionAssignment)),
      _priority(priority),
      _matches(std::move(matches)) {
    for (const auto &assignment : _matches) {
        _z3Matches.add(assignment.first, Z3Cache::set(&assignment.second.get()));
    }
    for (const auto &assignment : _actionAssignment) {
        _z3ActionAssignment.add(assignment.first, Z3Cache::set(&assignment.second.get()));
    }
}

int32_t TableMatchEntry::priority() const { return _priority; }

ControlPlaneAssignmentSet TableMatchEntry::actionAssignment() const { return _actionAssignment; }

Z3ControlPlaneAssignmentSet TableMatchEntry::z3ActionAssignment() const {
    return _z3ActionAssignment;
}

bool TableMatchEntry::operator<(const ControlPlaneItem &other) const {
    // Table match entries are only compared based on the match expression.
    return typeid(*this) == typeid(other)
               ? structuralCompare(_matches, other.as<TableMatchEntry>()._matches) < 0
               : typeid(*this).hash_code() < typeid(other).hash_code();
}

ControlPlaneAssignmentSet TableMatchEntry::computeControlPlaneAssignments() const {
    return _matches;
}

Z3ControlPlaneAssignmentSet TableMatchEntry::computeZ3ControlPlaneAssignments() const {
    return _z3Matches;
}

/**************************************************************************************************
TableDefaultAction
**************************************************************************************************/

TableDefaultAction::TableDefaultAction(ControlPlaneAssignmentSet actionAssignment)
    : _actionAssignment(std::move(actionAssignment)) {
    for (const auto &assignment : _actionAssignment) {
        _z3ActionAssignment.add(assignment.first, Z3Cache::set(&assignment.second.get()));
    }
}

bool TableDefaultAction::operator<(const ControlPlaneItem &other) const {
    // Table match entries are only compared based on the match expression.
    return typeid(*this) == typeid(other)
               ? structuralCompare(_actionAssignment,
                                   other.as<TableDefaultAction>()._actionAssignment) < 0
               : typeid(*this).hash_code() < typeid(other).hash_code();
}

ControlPlaneAssignmentSet TableDefaultAction::computeControlPlaneAssignments() const {
    return _actionAssignment;
}

Z3ControlPlaneAssignmentSet TableDefaultAction::computeZ3ControlPlaneAssignments() const {
    return _z3ActionAssignment;
}

/**************************************************************************************************
WildCardMatchEntry
**************************************************************************************************/

WildCardMatchEntry::WildCardMatchEntry(ControlPlaneAssignmentSet actionAssignment, int32_t priority)
    : TableMatchEntry(std::move(actionAssignment), priority, {}) {}

ControlPlaneAssignmentSet WildCardMatchEntry::computeControlPlaneAssignments() const { return {}; }

Z3ControlPlaneAssignmentSet WildCardMatchEntry::computeZ3ControlPlaneAssignments() const {
    return {};
}

/**************************************************************************************************
TableConfiguration
**************************************************************************************************/

const IR::Expression *TableConfiguration::buildKeyMatches(const KeyMap &keyMap) {
    if (keyMap.empty()) {
        return IR::BoolLiteral::get(false);
    }
    const IR::Expression *hitCondition = nullptr;
    for (const auto *key : keyMap) {
        const auto *matchExpr = key->computeControlPlaneConstraint();
        if (hitCondition == nullptr) {
            hitCondition = matchExpr;
        } else {
            hitCondition = new IR::LAnd(hitCondition, matchExpr);
        }
    }
    // The table can only "hit" when it is actually configured by the control-plane.
    // Pay attention to how we use "toString" for the path name here.
    // We need to match these choices correctly. TODO: Make this very explicit.
    return hitCondition;
}

bool TableConfiguration::CompareTableMatch::operator()(const TableMatchEntry &left,
                                                       const TableMatchEntry &right) {
    return left.priority() > right.priority();
}

TableConfiguration::TableConfiguration(cstring tableName, TableDefaultAction defaultTableAction,
                                       TableEntrySet tableEntries)
    : _tableName(tableName),
      _defaultTableAction(std::move(defaultTableAction)),
      _tableEntries(std::move(tableEntries)) {}

bool TableConfiguration::operator<(const ControlPlaneItem &other) const {
    return typeid(*this) == typeid(other) ? _tableName < other.as<TableConfiguration>()._tableName
                                          : typeid(*this).hash_code() < typeid(other).hash_code();
}

void TableConfiguration::setTableKeyMatch(const KeyMap &tableKeyMap) {
    _tableKeyMatch = SimplifyExpression::simplify(buildKeyMatches(tableKeyMap));
    // When we set the table key match, we also need to recompute the match of all table entries.
    auto z3TableKeyMatch = Z3Cache::set(_tableKeyMatch);
    for (const auto &tableMatchEntry : _tableEntries) {
        tableMatchEntry.get().setZ3Condition(z3TableKeyMatch);
    }
}

int TableConfiguration::addTableEntry(TableMatchEntry &tableMatchEntry, bool replace) {
    if (replace) {
        _tableEntries.erase(tableMatchEntry);
    }
    tableMatchEntry.setZ3Condition(Z3Cache::set(_tableKeyMatch));
    return _tableEntries.emplace(tableMatchEntry).second ? EXIT_SUCCESS : EXIT_FAILURE;
}

size_t TableConfiguration::deleteTableEntry(TableMatchEntry &tableMatchEntry) {
    return _tableEntries.erase(tableMatchEntry);
}

void TableConfiguration::clearTableEntries() { _tableEntries.clear(); }

void TableConfiguration::setDefaultTableAction(TableDefaultAction defaultTableAction) {
    _defaultTableAction = std::move(defaultTableAction);
}

ControlPlaneAssignmentSet TableConfiguration::computeControlPlaneAssignments() const {
    auto assignments = _defaultTableAction.computeControlPlaneAssignments();
    assignments.emplace(*ControlPlaneState::getTableActive(_tableName),
                        *IR::BoolLiteral::get(_tableEntries.size() > 0));
    if (_tableEntries.size() == 0) {
        return assignments;
    }

    // If we have more than kMaxEntriesPerTable entries we give up.
    // Set the entries symbolic and assume they can be executed freely.
    if (_tableEntries.size() > kMaxEntriesPerTable) {
        for (const auto &[symbol, assignment] : assignments) {
            const auto *symbolicVar =
                ToolsVariables::getSymbolicVariable(symbol.get().type, symbol.get().label + "*");
            auto it = assignments.find(symbol);
            if (it == assignments.end()) {
                assignments.emplace(symbol, *symbolicVar);
            } else {
                it->second = *symbolicVar;
            }
        }
        return assignments;
    }

    for (const auto &tableEntry : _tableEntries) {
        const auto &actionAssignments = tableEntry.get().actionAssignment();
        const auto keyAssignments = tableEntry.get().computeControlPlaneAssignments();
        const auto *constraint = _tableKeyMatch->apply(SubstituteSymbolicVariable(keyAssignments));

        for (const auto &[variable, assignment] : actionAssignments) {
            auto it = assignments.find(variable);
            if (it != assignments.end()) {
                it->second = *new IR::Mux(constraint, &assignment.get(), &it->second.get());
            } else {
                assignments.insert({variable, assignment});
            }
        }
    }
    return assignments;
}

Z3ControlPlaneAssignmentSet TableConfiguration::computeZ3ControlPlaneAssignments() const {
    auto assignments = _defaultTableAction.computeZ3ControlPlaneAssignments();
    assignments.add(*ControlPlaneState::getTableActive(_tableName),
                    Z3Cache::set(IR::BoolLiteral::get(_tableEntries.size() > 0)));
    if (_tableEntries.size() == 0) {
        return assignments;
    }

    // If we have more than kMaxEntriesPerTable entries we give up.
    // Set the entries symbolic and assume they can be executed freely.
    if (_tableEntries.size() > kMaxEntriesPerTable) {
        assignments.setAllSymbolic();
        return assignments;
    }

    Util::ScopedTimer timer("computeZ3ControlPlaneAssignments");
    auto z3TableKeyMatchOpt = Z3Cache::get(_tableKeyMatch);
    if (!z3TableKeyMatchOpt.has_value()) {
        error("Failed to get Z3 table key match");
        return assignments;
    }
    for (const auto &tableEntry : _tableEntries) {
        auto constraint = tableEntry.get()._z3Condition();
        if (!constraint.has_value()) {
            return assignments;
        }
        assignments.mergeConditionally(
            tableEntry.get().computeZ3ControlPlaneAssignments().substitute(constraint.value()),
            tableEntry.get().z3ActionAssignment());
    }
    return assignments;
}

/**************************************************************************************************
ParserValueSet
**************************************************************************************************/

ParserValueSet::ParserValueSet(cstring name) : _name(name) {}

bool ParserValueSet::operator<(const ControlPlaneItem &other) const {
    return typeid(*this) == typeid(other) ? _name < other.as<ParserValueSet>()._name
                                          : typeid(*this).hash_code() < typeid(other).hash_code();
}

ControlPlaneAssignmentSet ParserValueSet::computeControlPlaneAssignments() const {
    return {{*ControlPlaneState::getParserValueSetConfigured(_name), *IR::BoolLiteral::get(false)}};
}

Z3ControlPlaneAssignmentSet ParserValueSet::computeZ3ControlPlaneAssignments() const {
    Z3ControlPlaneAssignmentSet z3Assignments;
    z3Assignments.add(*ControlPlaneState::getParserValueSetConfigured(_name),
                      Z3Cache::set(IR::BoolLiteral::get(false)));
    return z3Assignments;
}

/**************************************************************************************************
ActionProfile
**************************************************************************************************/

bool ActionProfile::operator<(const ControlPlaneItem &other) const {
    // There is only one action profile active, we ignore the set of associated tables.
    return typeid(*this) == typeid(other) ? false
                                          : typeid(*this).hash_code() < typeid(other).hash_code();
}

const std::set<cstring> &ActionProfile::associatedTables() const { return _associatedTables; }

void ActionProfile::addAssociatedTable(cstring table) { _associatedTables.insert(table); }

ControlPlaneAssignmentSet ActionProfile::computeControlPlaneAssignments() const {
    // Action profiles are indirect and associated with the constraints of a table.
    return {};
}

Z3ControlPlaneAssignmentSet ActionProfile::computeZ3ControlPlaneAssignments() const {
    // Action profiles are indirect and associated with the constraints of a table.
    return {};
}

/**************************************************************************************************
ActionSelector
**************************************************************************************************/

bool ActionSelector::operator<(const ControlPlaneItem &other) const {
    // There is only one action selection active.
    // We ignore the set of associated tables and referenced profile.
    return typeid(*this) == typeid(other) ? false
                                          : typeid(*this).hash_code() < typeid(other).hash_code();
}

const std::set<cstring> &ActionSelector::associatedTables() const { return _associatedTables; }

void ActionSelector::addAssociatedTable(cstring table) {
    _associatedTables.insert(table);
    // Note that we also add the table to the associated profile.
    _actionProfile.get().addAssociatedTable(table);
}

const ActionProfile &ActionSelector::actionProfile() const { return _actionProfile; }

ControlPlaneAssignmentSet ActionSelector::computeControlPlaneAssignments() const {
    // Action selectors are indirect and associated with the constraints of a table.
    return {};
}

Z3ControlPlaneAssignmentSet ActionSelector::computeZ3ControlPlaneAssignments() const {
    // Action selectors are indirect and associated with the constraints of a table.
    return {};
}

/**************************************************************************************************
TableActionSelectorConfiguration
**************************************************************************************************/

TableActionSelectorConfiguration::TableActionSelectorConfiguration(
    cstring tableName, TableDefaultAction defaultTableAction, TableEntrySet tableEntries)
    : TableConfiguration(tableName, std::move(defaultTableAction), std::move(tableEntries)) {}

ControlPlaneAssignmentSet TableActionSelectorConfiguration::computeControlPlaneAssignments() const {
    // This does nothing currently.
    return {};
}

Z3ControlPlaneAssignmentSet TableActionSelectorConfiguration::computeZ3ControlPlaneAssignments()
    const {
    // This does nothing currently.
    return {};
}

}  // namespace P4::P4Tools::Flay
