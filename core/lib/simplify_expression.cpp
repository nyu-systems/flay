#include "backends/p4tools/modules/flay/core/lib/simplify_expression.h"

#include <optional>
#include <utility>

#include "backends/p4tools/modules/flay/core/lib/collapse_dataplane_variables.h"
#include "backends/p4tools/modules/flay/core/lib/expression_strength_reduction.h"
#include "backends/p4tools/modules/flay/options.h"
#include "frontends/common/constantFolding.h"
#include "ir/compare.h"
#include "ir/irutils.h"
#include "ir/pass_manager.h"
#include "lib/timer.h"

namespace P4::P4Tools {

namespace {

/// Simplifies mux expressions by folding the condition of the Mux down into its branches.
/// For example,
/// "|X(bool)| ? |X(bool)| ? |A(bit<8>)| : |B(bit<8>)| : |B(bit<8>)|"
/// turns into
/// "|X(bool)| ? |A(bit<8>)| : |B(bit<8>)|"
class FoldMuxConditionDown : public Transform {
 private:
    using ExpressionMap = std::map<const IR::Expression *, bool, IR::StructuralLess>;

    static std::optional<bool> optionalValue(const ExpressionMap &expressionMap,
                                             const IR::Expression *cond) {
        if (const auto *boolLiteral = cond->to<IR::BoolLiteral>()) {
            return boolLiteral->value;
        }
        auto it = expressionMap.find(cond);
        if (it != expressionMap.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    class FoldExpression : public Transform {
     private:
        ExpressionMap expressionMap;

     public:
        explicit FoldExpression(ExpressionMap expressionMap)
            : expressionMap(std::move(expressionMap)) {}
        FoldExpression() = default;

        const IR::Node *postorder(IR::LAnd *expr) override {
            auto leftOpt = optionalValue(expressionMap, expr->left);
            if (leftOpt.has_value()) {
                if (leftOpt.value()) {
                    auto rightOpt = optionalValue(expressionMap, expr->right);
                    return rightOpt.has_value()
                               ? IR::BoolLiteral::get(rightOpt.value(), expr->getSourceInfo())
                               : expr->right;
                }
                return IR::BoolLiteral::get(false, expr->getSourceInfo());
            }
            auto rightOpt = optionalValue(expressionMap, expr->right);
            if (rightOpt.has_value()) {
                return rightOpt.value() ? expr->left
                                        : IR::BoolLiteral::get(false, expr->getSourceInfo());
            }
            return expr;
        }

        const IR::Node *postorder(IR::LOr *expr) override {
            auto leftOpt = optionalValue(expressionMap, expr->left);
            if (leftOpt.has_value()) {
                if (leftOpt.value()) {
                    return IR::BoolLiteral::get(true, expr->getSourceInfo());
                }
                auto rightOpt = optionalValue(expressionMap, expr->right);
                return rightOpt.has_value()
                           ? IR::BoolLiteral::get(rightOpt.value(), expr->getSourceInfo())
                           : expr->right;
            }
            auto rightOpt = optionalValue(expressionMap, expr->right);
            if (rightOpt.has_value()) {
                return rightOpt.value() ? IR::BoolLiteral::get(true, expr->getSourceInfo())
                                        : expr->left;
            }
            return expr;
        }

        const IR::Node *postorder(IR::LNot *expr) override {
            auto exprIt = expressionMap.find(expr->expr);
            if (exprIt != expressionMap.end()) {
                return IR::BoolLiteral::get(!exprIt->second, expr->getSourceInfo());
            }

            return expr;
        }

        const IR::Node *postorder(IR::Mux *mux) override {
            auto condOpt = optionalValue(expressionMap, mux->e0);
            if (condOpt.has_value()) {
                return condOpt.value() ? mux->e1 : mux->e2;
            }
            if (mux->e1->equiv(*mux->e2)) {
                return mux->e1;
            }
            return mux;
        }
    };

    const IR::Node *preorder(IR::Mux *mux) override {
        // TODO: We should not need to use this prune but it is needed to avoid a massive slowdown
        // for reasons unclear to me.
        prune();
        mux->e0 = mux->e0->apply(FoldExpression());
        mux->e1 = mux->e1->apply(FoldExpression({{mux->e0, true}}));
        mux->e2 = mux->e2->apply(FoldExpression({{mux->e0, false}}));
        return mux;
    }

 public:
    // Rewriting a mux depends only on its own condition, not on the path used to reach it.
    // Keep the default DAG traversal so shared subexpressions are rewritten only once.
    FoldMuxConditionDown() = default;
};

/// Lifts conditions in mux expressions "upward" and converts the conditions into disjunctive or
/// conjunctive expressions.
/// For example,
/// "|X(bool)| ? |Y(bool)| ? |A(bit<8>)| : |B(bit<8>)| : |B(bit<8>)|"
/// turns into
/// "|X(bool)| && |Y(bool)| ? |A(bit<8>)| : |B(bit<8>)|"
/// and
/// "|X(bool)| ? |A(bit<8>)| : |Y(bool)| ? |A(bit<8>)| : |B(bit<8>)|"
/// into
/// "|X(bool)| || |Y(bool)| ? |A(bit<8>)| : |B(bit<8>)|".
class LiftMuxConditions : public Transform {
 public:
    LiftMuxConditions() = default;

    const IR::Node *preorder(IR::Mux *mux) override {
        if (const auto *trueMux = mux->e1->to<IR::Mux>()) {
            /// "|X(bool)| ? |Y(bool)| ? |B(bit<8>)| : |A(bit<8>)| : |B(bit<8>)|"
            /// turns into
            /// "|X(bool)| && !|Y(bool)| ? |A(bit<8>)| : |B(bit<8>)|"
            if (mux->e2->equiv(*trueMux->e1)) {
                mux->e0 = new IR::LAnd(mux->e0, new IR::LNot(trueMux->e0));
                mux->e1 = trueMux->e2;
                return mux;
            }
            /// "|X(bool)| ? |Y(bool)| ? |A(bit<8>)| : |B(bit<8>)| : |B(bit<8>)|"
            /// turns into
            /// "|X(bool)| && |Y(bool)| ? |A(bit<8>)| : |B(bit<8>)|"
            if (mux->e2->equiv(*trueMux->e2)) {
                mux->e0 = new IR::LAnd(mux->e0, trueMux->e0);
                mux->e1 = trueMux->e1;
                return mux;
            }
        }
        if (const auto *falseMux = mux->e2->to<IR::Mux>()) {
            /// "|X(bool)| ? |A(bit<8>)| : |Y(bool)| ? |A(bit<8>)| : |B(bit<8>)|"
            /// turns into
            /// "|X(bool)| || |Y(bool)| ? |A(bit<8>)| : |B(bit<8>)|"
            if (mux->e1->equiv(*falseMux->e1)) {
                mux->e0 = new IR::LOr(mux->e0, falseMux->e0);
                mux->e2 = falseMux->e2;
                return mux;
            }
            /// "|X(bool)| ? |B(bit<8>)| : |Y(bool)| ? |A(bit<8>)| : |B(bit<8>)|"
            /// turns into
            /// "|X(bool)| || !|Y(bool)| ? |B(bit<8>)| : |A(bit<8>)|"
            if (mux->e1->equiv(*falseMux->e2)) {
                mux->e0 = new IR::LOr(mux->e0, new IR::LNot(falseMux->e0));
                mux->e2 = falseMux->e1;
                return mux;
            }
        }
        return mux;
    }

    const IR::Node *postorder(IR::Mux *mux) override {
        if (mux->e1->equiv(*mux->e2)) {
            return mux->e1;
        }
        return mux;
    }
};

}  // namespace

namespace SimplifyExpression {

const IR::Expression *produceSimplifiedMux(const IR::Expression *cond,
                                           const IR::Expression *trueExpression,
                                           const IR::Expression *falseExpression) {
    Util::ScopedTimer timer("Mux optimization");
    return simplify(new IR::Mux(trueExpression->type, cond, trueExpression, falseExpression));
}

class ExpressionRewriter : public PassManager {
 public:
    ExpressionRewriter() {
        setName("ExpressionRewriter");
        // Lifted from frontends/p4/optimizeExpression.
        addPasses({
            // new P4::ConstantFolding(nullptr, nullptr, false),
            // new ExpressionStrengthReduction(),
            new FoldMuxConditionDown(),
            new LiftMuxConditions(),
        });
        auto &options = Flay::FlayOptions::get();
        if (options.collapseDataPlaneOperations() && !options.skipParsers()) {
            addPasses({new Flay::DataPlaneVariablePropagator()});
        }
    }
};

const IR::Expression *simplify(const IR::Expression *expr) {
    static ExpressionRewriter REWRITER;
    expr = expr->apply(REWRITER);
    BUG_CHECK(errorCount() == 0, "Encountered errors while trying to simplify expressions.");
    return expr;
}

}  // namespace SimplifyExpression

}  // namespace P4::P4Tools
