#include "backends/p4tools/modules/flay/core/interpreter/expression_resolver.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "backends/p4tools/common/compiler/convert_hs_index.h"
#include "backends/p4tools/common/lib/gen_eq.h"
#include "backends/p4tools/common/lib/symbolic_env.h"
#include "backends/p4tools/common/lib/variables.h"
#include "backends/p4tools/modules/flay/core/interpreter/externs.h"
#include "backends/p4tools/modules/flay/core/interpreter/target.h"
#include "ir/id.h"
#include "ir/indexed_vector.h"
#include "ir/irutils.h"
#include "ir/vector.h"
#include "lib/cstring.h"
#include "lib/exceptions.h"
#include "lib/null.h"
#include "midend/saturationElim.h"

namespace P4::P4Tools::Flay {

ExpressionResolver::ExpressionResolver(const ProgramInfo &programInfo,
                                       ControlPlaneConstraints &constraints,
                                       ExecutionState &executionState)
    : programInfo(programInfo),
      _controlPlaneConstraints(constraints),
      executionState(executionState) {
    visitDagOnce = false;
}

const ProgramInfo &ExpressionResolver::getProgramInfo() const { return programInfo.get(); }

ExecutionState &ExpressionResolver::getExecutionState() const { return executionState.get(); }

const IR::Expression *ExpressionResolver::getResult() {
    CHECK_NULL(result);
    return result;
}

ControlPlaneConstraints &ExpressionResolver::controlPlaneConstraints() const {
    return _controlPlaneConstraints;
}

/* =============================================================================================
 *  Visitor functions
 * ============================================================================================= */

bool ExpressionResolver::preorder(const IR::Node *node) {
    P4C_UNIMPLEMENTED("Node %1% of type %2% not implemented in the expression resolver.", node,
                      node->node_type_name());
}

bool ExpressionResolver::preorder(const IR::Literal *lit) {
    result = lit;
    return false;
}

bool ExpressionResolver::preorder(const IR::PathExpression *path) {
    result = getExecutionState().get(ToolsVariables::convertReference(path));
    getExecutionState().addExpressionMapping(path, result);
    return false;
}

const IR::Expression *ExpressionResolver::checkStructLike(const IR::Member *member) {
    std::vector<const IR::ID *> ids;
    const IR::Expression *expr = member;
    // Add a mapping if the member tail is hit or miss.
    bool addMapping =
        member->member == IR::Type_Table::hit || member->member == IR::Type_Table::miss;
    while (const auto *subMember = expr->to<IR::Member>()) {
        ids.emplace_back(&subMember->member);
        expr = subMember->expr;
    }
    // If the member is a method call, resolve it. The result MUST be a struct expression.
    if (const auto *methodCallExpr = expr->to<IR::MethodCallExpression>()) {
        expr = computeResult(methodCallExpr)->checkedTo<IR::StructExpression>();
    }
    // If the expression is a struct expression, try to resolve the member contained in it
    // iteratively.
    if (const auto *structExpr = expr->to<IR::StructExpression>()) {
        while (!ids.empty()) {
            const auto *ref = ids.back();
            ids.pop_back();
            const auto *expr = structExpr->getField(*ref)->expression;
            if (const auto *se = expr->to<IR::StructExpression>()) {
                structExpr = se;
            } else {
                if (addMapping) {
                    getExecutionState().addReachabilityMapping(member, expr);
                }
                return expr;
            }
        }
    }
    return nullptr;
}

bool ExpressionResolver::preorder(const IR::Member *member) {
    // Some members may access struct expression, not actual state. Resolve these first.
    const auto *structExpr = checkStructLike(member);
    if (structExpr != nullptr) {
        result = structExpr;
    } else {
        result = getExecutionState().get(member);
    }
    getExecutionState().addExpressionMapping(member, result);
    return false;
}

bool ExpressionResolver::preorder(const IR::ArrayIndex *arrIndex) {
    const auto *right = arrIndex->right;
    bool hasChanged = false;
    if (!SymbolicEnv::isSymbolicValue(right)) {
        right = computeResult(right);
        hasChanged = true;
    }
    if (hasChanged) {
        auto *newIndex = arrIndex->clone();
        newIndex->right = right;
        arrIndex = newIndex;
    }
    result = getExecutionState().get(arrIndex);
    return false;
}

bool ExpressionResolver::preorder(const IR::Operation_Unary *op) {
    const auto *expr = op->expr;
    bool hasChanged = false;
    if (!SymbolicEnv::isSymbolicValue(expr)) {
        expr = computeResult(expr);
        hasChanged = true;
    }

    if (hasChanged) {
        auto *newOp = op->clone();
        newOp->expr = expr;
        result = newOp;
        return false;
    }
    result = op;
    return false;
}

bool ExpressionResolver::preorder(const IR::Operation_Binary *op) {
    const auto *left = op->left;
    const auto *right = op->right;
    bool hasChanged = false;
    if (!SymbolicEnv::isSymbolicValue(left)) {
        left = computeResult(left);
        hasChanged = true;
    }

    if (!SymbolicEnv::isSymbolicValue(right)) {
        right = computeResult(right);
        hasChanged = true;
    }

    // Equals operations are a little special since they may involve complex objects such as lists.
    if (op->is<IR::Equ>() || op->is<IR::Neq>()) {
        result = GenEq::equate(left, right);
        if (op->is<IR::Neq>()) {
            result = new IR::LNot(result);
        }
        return false;
    }

    if (hasChanged) {
        auto *newOp = op->clone();
        newOp->left = left;
        newOp->right = right;
        result = newOp;
    } else {
        result = op;
    }

    // Handle saturating arithmetic expressions by translating them into Mux expressions.
    if (P4::SaturationElim::isSaturationOperation(result)) {
        result = P4::SaturationElim::eliminate(result->checkedTo<IR::Operation_Binary>());
    }
    return false;
}

bool ExpressionResolver::preorder(const IR::Operation_Ternary *op) {
    const auto *e0 = op->e0;
    const auto *e1 = op->e1;
    const auto *e2 = op->e2;
    bool hasChanged = false;
    if (!SymbolicEnv::isSymbolicValue(e0)) {
        e0 = computeResult(e0);
        hasChanged = true;
    }

    if (!SymbolicEnv::isSymbolicValue(e1)) {
        e1 = computeResult(e1);
        hasChanged = true;
    }

    if (!SymbolicEnv::isSymbolicValue(e2)) {
        e2 = computeResult(e2);
        hasChanged = true;
    }
    if (hasChanged) {
        auto *newOp = op->clone();
        newOp->e0 = e0;
        newOp->e1 = e1;
        newOp->e2 = e2;
        result = newOp;
        return false;
    }
    result = op;
    return false;
}

bool ExpressionResolver::preorder(const IR::Mux *mux) {
    const auto *cond = mux->e0;
    const auto *ifExpr = mux->e1;
    const auto *elseExpr = mux->e2;
    bool hasChanged = false;
    if (!SymbolicEnv::isSymbolicValue(cond)) {
        cond = computeResult(cond);
        hasChanged = true;
    }

    if (!SymbolicEnv::isSymbolicValue(elseExpr)) {
        elseExpr = computeResult(elseExpr);
        hasChanged = true;
    }

    if (!SymbolicEnv::isSymbolicValue(ifExpr)) {
        // Execute the case where the condition is true.
        // TODO: This whole sequence is quite bug-prone. We should clean this up.
        auto &oldState = getExecutionState();
        auto &trueState = oldState.clone();
        trueState.pushExecutionCondition(cond);
        executionState = trueState;
        ifExpr = computeResult(ifExpr);
        oldState.merge(trueState);
        executionState = oldState;
        hasChanged = true;
    }

    if (hasChanged) {
        auto *newOp = mux->clone();
        newOp->e0 = cond;
        newOp->e1 = ifExpr;
        newOp->e2 = elseExpr;
        result = newOp;
        return false;
    }
    result = mux;
    return false;
}

bool ExpressionResolver::preorder(const IR::BaseListExpression *listExpr) {
    IR::Vector<IR::Expression> components;
    bool hasChanged = false;
    for (const auto *expr : listExpr->components) {
        if (!SymbolicEnv::isSymbolicValue(expr)) {
            expr = computeResult(expr);
            hasChanged = true;
        }
        components.push_back(expr);
    }
    if (hasChanged) {
        auto *newListExpr = listExpr->clone();
        newListExpr->components = components;
        result = newListExpr;
        return false;
    }
    result = listExpr;
    return false;
}

bool ExpressionResolver::preorder(const IR::StructExpression *structExpr) {
    IR::IndexedVector<IR::NamedExpression> components;
    // StructExpressions are a little different, in that we always replace them.
    // This is because we need to account for nested headerExpressions.
    // TODO: Maybe we should just replace this structures with a compiler pass?
    for (const auto *field : structExpr->components) {
        const auto *expr = field->expression;
        expr = computeResult(expr);
        auto *newField = field->clone();
        newField->expression = expr;
        components.push_back(newField);
    }
    // If the struct expression type is a header, then it is always valid.
    const auto *resolvedType = getExecutionState().resolveType(structExpr->type);
    if (resolvedType->is<IR::Type_Header>()) {
        // TODO: Do not use nullptr here and instead the real type.
        result = new IR::HeaderExpression(nullptr, components, IR::BoolLiteral::get(true));
        return false;
    }
    auto *newStructExpr = structExpr->clone();
    newStructExpr->components = components;
    result = newStructExpr;
    return false;
}

bool ExpressionResolver::preorder(const IR::MethodCallExpression *call) {
    auto &state = getExecutionState();

    // Resolve all arguments to the method call.
    IR::Vector<IR::Argument> resolvedArgs;
    const auto *method = call->method->type->checkedTo<IR::Type_MethodBase>();
    const auto &methodParams = method->parameters->parameters;
    const auto *callArguments = call->arguments;
    for (size_t idx = 0; idx < callArguments->size(); ++idx) {
        const auto *arg = callArguments->at(idx);
        const auto *param = methodParams.at(idx);
        const IR::Expression *computedExpr;
        if (param->type->is<IR::Type_Extern>()) {
            // Parameters of `Type_Extern` will be parsed (if necessary) in the lambda handlers.
            computedExpr = nullptr;
        } else {
            computedExpr = computeResult(arg->expression);
            if (param->direction == IR::Direction::InOut ||
                param->direction == IR::Direction::Out) {
                auto stateVar = ToolsVariables::convertReference(arg->expression);
                computedExpr = new IR::InOutReference(stateVar, computedExpr);
            }
        }
        auto *newArg = arg->clone();
        newArg->expression = computedExpr;
        resolvedArgs.push_back(newArg);
    }

    // Handle method calls. These are either table invocations or extern calls.
    if (call->method->type->is<IR::Type_Method>()) {
        // Assume that all cases of path expressions are extern calls.
        if (const auto *path = call->method->to<IR::PathExpression>()) {
            static auto METHOD_DUMMY =
                IR::PathExpression(new IR::Type_Extern("*method"), new IR::Path("*method"));
            result = processExtern({*call, METHOD_DUMMY, path->path->name, &resolvedArgs,
                                    programInfo, controlPlaneConstraints(), state});
            return false;
        }

        if (const auto *method = call->method->to<IR::Member>()) {
            // Case where call->method is a Member expression. For table invocations, the
            // qualifier of the member determines the table being invoked. For extern calls,
            // the qualifier determines the extern object containing the method being invoked.
            BUG_CHECK(method->expr, "Method call has unexpected format: %1%", call);

            // Handle extern calls. They may also be of Type_SpecializedCanonical.
            if (method->expr->type->is<IR::Type_Extern>() ||
                method->expr->type->is<IR::Type_SpecializedCanonical>()) {
                const auto *path = method->expr->checkedTo<IR::PathExpression>();
                result = processExtern({*call, *path, method->member, &resolvedArgs, programInfo,
                                        controlPlaneConstraints(), state});
                return false;
            }

            // Handle table calls.
            if (const auto *table = state.findTable(method)) {
                result = processTable(table);
                return false;
            }

            // Handle calls to header methods.
            if (method->expr->type->is<IR::Type_Header>() ||
                method->expr->type->is<IR::Type_HeaderUnion>()) {
                if (method->member == "setValid") {
                    const auto &headerRefValidity = ToolsVariables::getHeaderValidity(method->expr);
                    auto headerRef = ToolsVariables::convertReference(method->expr);
                    state.initializeStructLike(FlayTarget::get(), headerRef, false);
                    state.set(headerRefValidity, IR::BoolLiteral::get(true));
                    return false;
                }
                if (method->member == "setInvalid") {
                    const auto &headerRefValidity = ToolsVariables::getHeaderValidity(method->expr);
                    auto headerRef = ToolsVariables::convertReference(method->expr);
                    state.initializeStructLike(FlayTarget::get(), headerRef, false);
                    return false;
                }
                if (method->member == "isValid") {
                    const auto &headerRefValidity = ToolsVariables::getHeaderValidity(method->expr);
                    result = state.get(headerRefValidity);
                    return false;
                }
                P4C_UNIMPLEMENTED("Unknown method call on header instance: %1%", call);
            }

            if (const auto *stackType = method->expr->type->to<IR::Type_Array>()) {
                const bool isPush = method->member == IR::Type_Array::push_front;
                if (!isPush && method->member != IR::Type_Array::pop_front) {
                    P4C_UNIMPLEMENTED("Unknown method call on stack instance: %1%", call);
                }
                BUG_CHECK(resolvedArgs.size() == 1, "Expected one argument for %1%", call);
                const auto *count = resolvedArgs.at(0)->expression->checkedTo<IR::Constant>();
                BUG_CHECK(count->value >= 0, "Negative header stack shift: %1%", call);
                const auto size = stackType->getSize();
                // Clamp before converting: P4 counts can be larger than machine integers.
                const auto shift = count->value >= size ? size : count->asUnsigned();
                if (shift == 0) return false;
                const auto *elementType = state.resolveType(stackType->elementType);
                auto element = [&](size_t index) {
                    return HSIndexToMember::produceStackIndex(elementType, method->expr, index);
                };
                // Copy backwards for push and forwards for pop to preserve unread elements.
                // setStructLike copies symbolic field values and header validity together.
                for (size_t offset = 0; offset < size; ++offset) {
                    const auto index = isPush ? size - 1 - offset : offset;
                    const auto *destination = element(index);
                    if (isPush ? index >= shift : index < size - shift) {
                        const auto source = isPush ? index - shift : index + shift;
                        state.setStructLike(destination, element(source));
                    } else {
                        state.initializeStructLike(FlayTarget::get(), destination, false);
                    }
                }
                // p4c forbids push/pop in parsers; next/last/lastIndex are parser-only.
                // No observable parser cursor remains to update here.
                return false;
            }

            P4C_UNIMPLEMENTED("Unknown method member expression: %1% of type %2%", method->expr,
                              method->expr->type);
        }

        P4C_UNIMPLEMENTED("Unknown method call: %1% of type %2%", call->method,
                          call->method->node_type_name());
    } else if (call->method->type->is<IR::Type_Action>()) {
        // Handle action calls. Actions are called by tables and are not inlined, unlike
        // functions.
        const auto *actionType = state.getP4Action(call);
        TableExecutor::callAction(programInfo, controlPlaneConstraints(), state, actionType,
                                  resolvedArgs);
        return false;
    }
    P4C_UNIMPLEMENTED("Unknown method call expression: %1%", call);
}

/// Provides implementations of P4 core externs.
namespace CoreExterns {

using namespace P4::literals;

static const ExternMethodImpls EXTERN_METHOD_IMPLS(
    {{"packet_in.extract"_cs,
      {"hdr"_cs},
      [](const ExternMethodImpls::ExternInfo &externInfo) {
          const auto *args = externInfo.externArgs;
          const auto &externObjectRef = externInfo.externObjectRef;
          const auto &methodName = externInfo.methodName;
          auto &state = externInfo.state;

          // This argument is the structure being written by the extract.
          const auto &extractRef = args->at(0)->expression->checkedTo<IR::InOutReference>()->ref;
          extractRef->type->checkedTo<IR::Type_Header>();
          // First, set the validity.
          auto extractLabel =
              externObjectRef.path->toString() + "_" + methodName + "_" + extractRef->toString();
          state.set(ToolsVariables::getHeaderValidity(extractRef),
                    new IR::DataPlaneVariable(IR::Type_Boolean::get(), extractLabel));
          // Then, set the fields.
          const auto flatFields = state.getFlatFields(extractRef);
          for (const auto &field : flatFields) {
              auto extractFieldLabel = externObjectRef.path->toString() + "_" + methodName + "_" +
                                       extractRef->toString() + "_" + field.toString();
              state.set(field, new IR::DataPlaneVariable(field->type, extractFieldLabel));
          }
          return nullptr;
      }},
     {"packet_in.extract"_cs,
      {"hdr"_cs, "sizeInBits"_cs},
      [](const ExternMethodImpls::ExternInfo &externInfo) {
          const auto *args = externInfo.externArgs;
          const auto &externObjectRef = externInfo.externObjectRef;
          const auto &methodName = externInfo.methodName;
          auto &state = externInfo.state;

          // This argument is the structure being written by the extract.
          const auto &extractRef = args->at(0)->expression->checkedTo<IR::InOutReference>()->ref;
          extractRef->type->checkedTo<IR::Type_Header>();
          // First, set the validity.
          auto extractLabel =
              externObjectRef.path->toString() + "_" + methodName + "_" + extractRef->toString();
          state.set(ToolsVariables::getHeaderValidity(extractRef),
                    new IR::DataPlaneVariable(IR::Type_Boolean::get(), extractLabel));
          // Then, set the fields.
          const auto flatFields = state.getFlatFields(extractRef);
          for (const auto &field : flatFields) {
              auto extractFieldLabel = externObjectRef.path->toString() + "_" + methodName + "_" +
                                       extractRef->toString() + "_" + field.toString();
              // For now, we ignore the assigned size in our calculations and always use the
              // maximum size.
              // TODO: Figure out a way to exploit sizeInBits?
              if (const auto *varbitType = field->type->to<IR::Extracted_Varbits>()) {
                  auto *assignedType = varbitType->clone();
                  assignedType->assignedSize = assignedType->size;
                  auto *typedField = field.clone();
                  typedField->type = assignedType;
                  state.set(*typedField,
                            new IR::DataPlaneVariable(typedField->type, extractFieldLabel));
              } else {
                  state.set(field, new IR::DataPlaneVariable(field->type, extractFieldLabel));
              }
          }
          return nullptr;
      }},
     {"packet_in.lookahead"_cs,
      {},
      [](const ExternMethodImpls::ExternInfo &externInfo) {
          auto &state = externInfo.state;
          const auto *typeArgs = externInfo.originalCall.typeArguments;
          BUG_CHECK(typeArgs->size() == 1, "Lookahead should have exactly one type argument.");
          // TODO: We currently just create a dummy variable, but this is not correct.
          // Since we look ahead, subsequent extracts and advance calls are influenced by the
          // decision.
          const auto *lookaheadType = externInfo.originalCall.typeArguments->at(0);
          const auto &externObjectRef = externInfo.externObjectRef;
          const auto &methodName = externInfo.methodName;
          auto lookaheadLabel = externObjectRef.path->toString() + "_" + methodName + "_" +
                                std::to_string(externInfo.originalCall.clone_id);
          return state.createSymbolicExpression(lookaheadType, lookaheadLabel);
      }},
     {"packet_in.advance"_cs,
      {"sizeInBits"_cs},
      [](const ExternMethodImpls::ExternInfo & /*externInfo*/) {
          // Advance is a no-op for now.
          return nullptr;
      }},
     {"packet_in.length"_cs,
      {},
      [](const ExternMethodImpls::ExternInfo &externInfo) {
          // Return a symbolic length for now..
          auto randomLabel = externInfo.externObjectRef.path->toString() + "_" +
                             externInfo.methodName + "_" +
                             std::to_string(externInfo.originalCall.clone_id);
          return ToolsVariables::getSymbolicVariable(IR::Type_Bits::get(32), randomLabel);
      }},
     {"packet_out.emit"_cs,
      {"hdr"_cs},
      [](const ExternMethodImpls::ExternInfo & /*externInfo*/) {
          // Emit is a no-op for now.
          return nullptr;
      }},
     /* ======================================================================================
      *  verify
      *  The verify statement provides a simple form of error handling.
      *  If the first argument is true, then executing the statement has no side-effect.
      *  However, if the first argument is false, it causes an immediate transition to
      *  reject, which causes immediate parsing termination; at the same time, the
      *  parserError associated with the parser is set to the value of the second
      *  argument.
      * ======================================================================================
      */
     {"*method.verify"_cs,
      {"bool"_cs, "error"_cs},
      [](const ExternMethodImpls::ExternInfo & /*externInfo*/) {
          // TODO: Implement the error case.
          return nullptr;
      }}});
}  // namespace CoreExterns

/* =============================================================================================
 *  Extern implementations
 * ============================================================================================= */

const IR::Expression *ExpressionResolver::processExtern(
    const ExternMethodImpls::ExternInfo &externInfo) {
    auto method = CoreExterns::EXTERN_METHOD_IMPLS.find(
        externInfo.externObjectRef, externInfo.methodName, externInfo.externArgs);
    if (method.has_value()) {
        return method.value()(externInfo);
    }
    P4C_UNIMPLEMENTED("Unknown or unimplemented extern method: %1%.%2%",
                      externInfo.externObjectRef.toString(), externInfo.methodName);
}

const IR::Expression *ExpressionResolver::computeResult(const IR::Node *node) {
    node->apply_visitor_preorder(*this);
    return getResult();
}

}  // namespace P4::P4Tools::Flay
