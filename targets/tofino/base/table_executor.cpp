
#include "backends/p4tools/modules/flay/targets/tofino/base/table_executor.h"

#include <boost/multiprecision/cpp_int.hpp>

#include "backends/p4tools/modules/flay/core/interpreter/expression_resolver.h"
#include "backends/p4tools/modules/flay/targets/tofino/constants.h"
#include "lib/cstring.h"
#include "lib/exceptions.h"

namespace P4::P4Tools::Flay::Tofino {

TofinoBaseTableExecutor::TofinoBaseTableExecutor(const IR::P4Table &table,
                                                 ExpressionResolver &callingResolver)
    : TableExecutor(table, callingResolver) {}

const TableMatchKey *TofinoBaseTableExecutor::computeTargetMatchType(
    const IR::KeyElement *keyField) const {
    auto tableName = getP4Table().controlPlaneName();
    const auto *keyExpression = keyField->expression;
    const auto matchType = keyField->matchType->toString();
    const auto *nameAnnot = keyField->getAnnotation(IR::Annotation::nameAnnotation);
    // Some hidden tables do not have any key name annotations.
    BUG_CHECK(nameAnnot != nullptr /* || properties.tableIsImmutable*/,
              "Non-constant table key without an annotation");
    cstring fieldName;
    if (nameAnnot != nullptr) {
        fieldName = nameAnnot->getName();
    }

    // TODO: We consider optional match types to be a no-op, but we could make them exact matches.
    if (matchType == TofinoBaseConstants::MATCH_KIND_OPT) {
        // We can recover from taint by simply not adding the optional match.
        // Create a new symbolic variable that corresponds to the key expression.
        return new OptionalMatchKey(symbolicTablePrefix(), fieldName, keyExpression);
    }
    // Action selector entries are not part of the match but we still need to create a key.
    if (matchType == TofinoBaseConstants::MATCH_KIND_SELECTOR) {
        cstring keyName = tableName + "_selector_" + fieldName;
        return new SelectorMatchKey(symbolicTablePrefix(), keyName, keyExpression);
    }
    if (matchType == TofinoBaseConstants::MATCH_KIND_RANGE) {
        return new RangeTableMatchKey(symbolicTablePrefix(), fieldName, keyExpression);
    }
    // If the custom match type does not match, delete to the core match types.
    return TableExecutor::computeTargetMatchType(keyField);
}

}  // namespace P4::P4Tools::Flay::Tofino
