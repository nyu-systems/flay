#include "backends/p4tools/modules/flay/control_plane/control_plane_objects.h"

#include <queue>

namespace P4Tools::Flay {

TableMatchEntry::TableMatchEntry(const Constraint *actionAssignment, int32_t priority,
                                 const TableKeySet &matches)
    : actionAssignment(actionAssignment), priority(priority), matchExpression(actionAssignment) {
    // Precompute the match expresison in the constructor.
    for (const auto &match : matches) {
        const auto &symbolicVariable = match.first;
        const auto &assignment = match.second;
        matchExpression =
            new IR::LAnd(matchExpression, new IR::Equ(&symbolicVariable, &assignment));
    }
}

int32_t TableMatchEntry::getPriority() const { return priority; }

const IR::Expression *TableMatchEntry::getActionAssignment() const { return actionAssignment; }

bool TableMatchEntry::operator<(const ControlPlaneItem &other) const {
    // Table match entries are only compared based on the match expression.
    return typeid(*this) == typeid(other)
               ? matchExpression->operator<(
                     *(static_cast<const TableMatchEntry &>(other)).matchExpression)
               : typeid(*this).hash_code() < typeid(other).hash_code();
}

const IR::Expression *TableMatchEntry::computeControlPlaneConstraint() const {
    return matchExpression;
}

bool TableConfiguration::CompareTableMatch::operator()(const TableMatchEntry &left,
                                                       const TableMatchEntry &right) {
    return left.getPriority() > right.getPriority();
}

TableConfiguration::TableConfiguration(cstring tableName, TableMatchEntry defaultConfig,
                                       TableEntrySet tableEntries)
    : tableName(tableName),
      defaultConfig(std::move(defaultConfig)),
      tableEntries(std::move(tableEntries)) {}

bool TableConfiguration::operator<(const ControlPlaneItem &other) const {
    return typeid(*this) == typeid(other)
               ? tableName < static_cast<const TableConfiguration &>(other).tableName
               : typeid(*this).hash_code() < typeid(other).hash_code();
}

void TableConfiguration::addTableEntry(const TableMatchEntry &tableMatchEntry) {
    tableEntries.emplace(tableMatchEntry);
}

void TableConfiguration::removeTableEntry(const TableMatchEntry &tableMatchEntry) {
    tableEntries.erase(tableMatchEntry);
}

const IR::Expression *TableConfiguration::computeControlPlaneConstraint() const {
    const IR::Expression *matchExpression = defaultConfig.computeControlPlaneConstraint();
    if (tableEntries.size() == 0) {
        return matchExpression;
    }
    std::priority_queue sortedTableEntries(tableEntries.begin(), tableEntries.end(),
                                           CompareTableMatch());
    while (!sortedTableEntries.empty()) {
        const auto &tableEntry = sortedTableEntries.top().get();
        matchExpression = new IR::Mux(tableEntry.computeControlPlaneConstraint(),
                                      tableEntry.getActionAssignment(), matchExpression);
        sortedTableEntries.pop();
    }

    return matchExpression;
}

}  // namespace P4Tools::Flay
