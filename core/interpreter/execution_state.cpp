#include "backends/p4tools/modules/flay/core/interpreter/execution_state.h"

#include <utility>

#include "backends/p4tools/common/lib/symbolic_env.h"
#include "backends/p4tools/common/lib/variables.h"
#include "backends/p4tools/modules/flay/core/interpreter/substitute_placeholders.h"
#include "backends/p4tools/modules/flay/core/lib/simplify_expression.h"
#include "backends/p4tools/modules/flay/options.h"
#include "ir/id.h"
#include "ir/irutils.h"
#include "lib/exceptions.h"
#include "lib/source_file.h"
#include "lib/timer.h"

namespace P4::P4Tools::Flay {

ExecutionState::ExecutionState(const IR::P4Program *program)
    : AbstractExecutionState(program), executionCondition(nullptr) {}

const IR::PathExpression ExecutionState::PLACEHOLDER_LABEL = IR::PathExpression("*placeholder");

/* =============================================================================================
 *  Accessors
 * ============================================================================================= */

const IR::Expression *ExecutionState::createSymbolicExpression(const IR::Type *inputType,
                                                               cstring label) const {
    const auto *resolvedType = resolveType(inputType);
    if (const auto *structType = resolvedType->to<IR::Type_StructLike>()) {
        IR::IndexedVector<IR::NamedExpression> fields;
        for (const auto *field : structType->fields) {
            auto fieldLabel = label + "_" + field->name;
            fields.push_back(new IR::NamedExpression(
                field->name, createSymbolicExpression(field->type, fieldLabel)));
        }
        if (structType->is<IR::Type_Header>()) {
            cstring labelId = label + "_" + ToolsVariables::VALID;
            const auto *validity = new IR::DataPlaneVariable(IR::Type_Boolean::get(), labelId);
            // TODO: We keep the struct type anonymous because we do not know it.
            return new IR::HeaderExpression(structType, nullptr, fields, validity);
        }
        // TODO: We keep the struct type anonymous because we do not know it.
        return new IR::StructExpression(structType, nullptr, fields);
    }
    if (const auto *arrayType = resolvedType->to<IR::Type_Array>()) {
        IR::Vector<IR::Expression> fields;
        for (size_t idx = 0; idx < arrayType->getSize(); ++idx) {
            auto fieldLabel = label + "[" + std::to_string(idx) + "]";
            fields.push_back(createSymbolicExpression(arrayType->elementType, fieldLabel));
        }
        return new IR::ArrayExpression(resolvedType, fields, resolvedType);
    }
    if (const auto *tupleType = resolvedType->to<IR::Type_BaseList>()) {
        IR::Vector<IR::Expression> fields;
        for (size_t idx = 0; idx < tupleType->components.size(); ++idx) {
            auto fieldLabel = label + "[" + std::to_string(idx) + "]";
            fields.push_back(createSymbolicExpression(tupleType->components.at(idx), fieldLabel));
        }
        return new IR::ListExpression(resolvedType, fields);
    }
    if (resolvedType->is<IR::Type_Base>()) {
        return new IR::DataPlaneVariable(resolvedType, label);
    }
    P4C_UNIMPLEMENTED("Requesting a symbolic expression for %1% of type %2%", inputType,
                      inputType->node_type_name());
}

const IR::Expression *ExecutionState::get(const IR::StateVariable &var) const {
    const auto *varType = resolveType(var->type);
    // In some cases, we may reference a complex expression. Convert it to a struct expression.
    if (varType->is<IR::Type_StructLike>() || varType->is<IR::Type_Array>() ||
        varType->is<IR::Type_BaseList>()) {
        return convertToComplexExpression(var);
    }
    return env.get(var);
}

void ExecutionState::set(const IR::StateVariable &var, const IR::Expression *value) {
    env.set(var, value);
}

/* =============================================================================================
 *  State merging and execution conditions.
 * ============================================================================================= */

const IR::Expression *ExecutionState::getExecutionCondition() const {
    return (executionCondition != nullptr) ? executionCondition : IR::BoolLiteral::get(true);
}

void ExecutionState::pushExecutionCondition(const IR::Expression *cond) {
    if (executionCondition == nullptr) {
        executionCondition = cond;
    } else {
        executionCondition = SimplifyExpression::simplify(new IR::LAnd(executionCondition, cond));
    }
}

void ExecutionState::merge(const ExecutionState &mergeState, const IR::Expression *condition) {
    const auto *cond = condition != nullptr ? condition : mergeState.getExecutionCondition();
    cond = SimplifyExpression::simplify(cond);
    const auto &mergeEnv = mergeState.getSymbolicEnv();
    _nodeAnnotationMap.mergeAnnotationMapping(mergeState.nodeAnnotationMap());

    if (const auto *boolExpr = cond->to<IR::BoolLiteral>()) {
        // If the condition is false, do nothing. If it is true, set all the values.
        if (boolExpr->value) {
            for (const auto &envTuple : mergeEnv.getInternalMap()) {
                auto ref = envTuple.first;
                if (exists(ref)) {
                    set(ref, envTuple.second);
                }
            }
        }
        return;
    }
    for (const auto &envTuple : mergeEnv.getInternalMap()) {
        auto ref = envTuple.first;
        const auto *mergeExpr = envTuple.second;
        // Do not merge any variable that did not exist previously.
        if (exists(ref)) {
            const auto *currentExpr = get(ref);
            // Only merge when the current and the merged expression are different.
            if (!currentExpr->equiv(*mergeExpr)) {
                set(envTuple.first,
                    SimplifyExpression::produceSimplifiedMux(cond, mergeExpr, currentExpr));
            }
        }
    }
}

void ExecutionState::join(const ExecutionState &other) {
    const auto *condition = new IR::LOr(getExecutionCondition(), other.getExecutionCondition());
    merge(other);
    executionCondition = SimplifyExpression::simplify(condition);
}

const NodeAnnotationMap &ExecutionState::nodeAnnotationMap() const { return _nodeAnnotationMap; }

void ExecutionState::addReachabilityMapping(const IR::Node *node, const IR::Expression *cond) {
    bool notAlreadyInMap = _nodeAnnotationMap.initializeReachabilityMapping(
        node, new IR::LAnd(getExecutionCondition(), cond));
    if (!notAlreadyInMap && FlayOptions::get().isStrict()) {
        // Throw a fatal error if we try to add a duplicate mapping.
        // This can affect the correctness of the entire mapping.
        BUG("Reachability mapping for node %1% already exists. Every mapping must be uniquely "
            "identifiable under strict mode.",
            node);
    }
}

void ExecutionState::addExpressionMapping(const IR::Expression *expression,
                                          const IR::Expression *value) {
    // TODO: Think about better handling of these types of errors?
    if (!expression->type->is<IR::Type_Bits>()) {
        return;
    }

    bool notAlreadyInMap =
        _nodeAnnotationMap.initializeExpressionMapping(expression, value, getExecutionCondition());
    if (!notAlreadyInMap && FlayOptions::get().isStrict()) {
        // Throw a fatal error if we try to add a duplicate mapping.
        // This can affect the correctness of the entire mapping.
        BUG("Expression mapping for expression %1% already exists. Every mapping must be "
            "uniquely "
            "identifiable under strict mode.",
            expression);
    }
}

void ExecutionState::setPlaceholderValue(cstring label, const IR::Expression *value) {
    const auto *placeholderVar = new IR::Member(value->type, &PLACEHOLDER_LABEL, label);
    set(placeholderVar, value);
}

std::optional<const IR::Expression *> ExecutionState::getPlaceholderValue(
    const IR::Placeholder &placeholder) const {
    const auto *placeholderVar =
        new IR::Member(placeholder.type, &PLACEHOLDER_LABEL, placeholder.label);
    if (exists(placeholderVar)) {
        return get(placeholderVar);
    }
    return std::nullopt;
}

void ExecutionState::substitutePlaceholders() {
    if (!FlayOptions::get().usePlaceholders()) {
        return;
    }
    Util::ScopedTimer timer("Placeholder Substitution");
    auto substitute = SubstitutePlaceHolders(*this);
    _nodeAnnotationMap.substitutePlaceholders(substitute);
}

/* =========================================================================================
 *  Constructors
 * ========================================================================================= */

ExecutionState &ExecutionState::create(const IR::P4Program *program) {
    return *new ExecutionState(program);
}

ExecutionState &ExecutionState::clone() const { return *new ExecutionState(*this); }

}  // namespace P4::P4Tools::Flay
