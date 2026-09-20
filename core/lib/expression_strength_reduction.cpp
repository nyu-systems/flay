#include "backends/p4tools/modules/flay/core/lib/expression_strength_reduction.h"

namespace P4::P4Tools {

ExpressionStrengthReduction::ExpressionStrengthReduction() {
    visitDagOnce = true;
    setName("StrengthReduction");
}

bool ExpressionStrengthReduction::isOne(const IR::Expression *expr) {
    const auto *cst = expr->to<IR::Constant>();
    if (cst == nullptr) {
        return false;
    }
    return cst->value == 1;
}

bool ExpressionStrengthReduction::isZero(const IR::Expression *expr) {
    if (const auto *bt = expr->type->to<IR::Type_Bits>()) {
        if (bt->width_bits() == 0) {
            return true;
        }
    }
    const auto *cst = expr->to<IR::Constant>();
    if (cst == nullptr) {
        return false;
    }
    return cst->value == 0;
}

bool ExpressionStrengthReduction::isTrue(const IR::Expression *expr) {
    const auto *cst = expr->to<IR::BoolLiteral>();
    if (cst == nullptr) {
        return false;
    }
    return cst->value;
}

bool ExpressionStrengthReduction::isFalse(const IR::Expression *expr) {
    const auto *cst = expr->to<IR::BoolLiteral>();
    if (cst == nullptr) {
        return false;
    }
    return !cst->value;
}

bool ExpressionStrengthReduction::isAllOnes(const IR::Expression *expr) {
    const auto *cst = expr->to<IR::Constant>();
    if (cst == nullptr) {
        return false;
    }
    big_int value = cst->value;
    if (value <= 0) {
        return false;
    }
    auto bitcnt = bitcount(value);
    return bitcnt == static_cast<uint64_t>(expr->type->width_bits());
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::Cmpl *expr) {
    if (const auto *a = expr->expr->to<IR::Cmpl>()) {
        return a->expr;
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::BAnd *expr) {
    if (isAllOnes(expr->left)) {
        return expr->right;
    }
    if (isAllOnes(expr->right)) {
        return expr->left;
    }
    const auto *l = expr->left->to<IR::Cmpl>();
    const auto *r = expr->right->to<IR::Cmpl>();
    if ((l != nullptr) && (r != nullptr)) {
        return new IR::Cmpl(expr->srcInfo, expr->type,
                            new IR::BOr(expr->srcInfo, expr->type, l->expr, r->expr));
    }

    if (isZero(expr->left)) {
        return expr->left;
    }
    if (isZero(expr->right)) {
        return expr->right;
    }
    if (expr->left->equiv(*expr->right)) {
        return expr->left;
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::BOr *expr) {
    if (isZero(expr->left)) {
        return expr->right;
    }
    if (isZero(expr->right)) {
        return expr->left;
    }
    const auto *l = expr->left->to<IR::Cmpl>();
    const auto *r = expr->right->to<IR::Cmpl>();
    if ((l != nullptr) && (r != nullptr)) {
        return new IR::Cmpl(expr->srcInfo, expr->type,
                            new IR::BAnd(expr->srcInfo, expr->type, l->expr, r->expr));
    }
    if (expr->left->equiv(*expr->right)) {
        return expr->left;
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::Equ *expr) {
    // a == true is the same as a
    if (isTrue(expr->left)) {
        return expr->right;
    }
    if (isTrue(expr->right)) {
        return expr->left;
    }
    // a == false is the same as !a
    if (isFalse(expr->left)) {
        return new IR::LNot(expr->srcInfo, expr->type, expr->right);
    }
    if (isFalse(expr->right)) {
        return new IR::LNot(expr->srcInfo, expr->type, expr->left);
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::Neq *expr) {
    // a != true is the same as !a
    if (isTrue(expr->left)) {
        return new IR::LNot(expr->srcInfo, expr->type, expr->right);
    }
    if (isTrue(expr->right)) {
        return new IR::LNot(expr->srcInfo, expr->type, expr->left);
    }
    // a != false is the same as a
    if (isFalse(expr->left)) {
        return expr->right;
    }
    if (isFalse(expr->right)) {
        return expr->left;
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::BXor *expr) {
    if (isZero(expr->left)) {
        return expr->right;
    }
    if (isZero(expr->right)) {
        return expr->left;
    }
    bool cmpl = false;
    if (const auto *l = expr->left->to<IR::Cmpl>()) {
        expr->left = l->expr;
        cmpl = !cmpl;
    }
    if (const auto *r = expr->right->to<IR::Cmpl>()) {
        expr->right = r->expr;
        cmpl = !cmpl;
    }
    if (cmpl) {
        return new IR::Cmpl(expr->srcInfo, expr->type, expr);
    }
    if (expr->left->equiv(*expr->right) && (expr->left->type != nullptr) &&
        !expr->left->type->is<IR::Type_Unknown>()) {
        // we assume that this type is right
        return new IR::Constant(expr->srcInfo, expr->left->type, 0);
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::LAnd *expr) {
    if (isFalse(expr->left)) {
        return expr->left;
    }
    if (isFalse(expr->right)) {
        return expr->right;
    }
    if (isTrue(expr->left)) {
        return expr->right;
    }
    if (isTrue(expr->right)) {
        return expr->left;
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::LOr *expr) {
    if (isTrue(expr->left)) {
        return expr->left;
    }
    if (isTrue(expr->right)) {
        return expr->right;
    }
    if (isFalse(expr->left)) {
        return expr->right;
    }
    if (isFalse(expr->right)) {
        return expr->left;
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::LNot *expr) {
    if (const auto *e = expr->expr->to<IR::LNot>()) {
        return e->expr;
    }
    if (const auto *e = expr->expr->to<IR::Equ>()) {
        return new IR::Neq(expr->srcInfo, expr->type, e->left, e->right);
    }
    if (const auto *e = expr->expr->to<IR::Neq>()) {
        return new IR::Equ(expr->srcInfo, expr->type, e->left, e->right);
    }
    if (const auto *e = expr->expr->to<IR::Leq>()) {
        return new IR::Grt(expr->srcInfo, expr->type, e->left, e->right);
    }
    if (const auto *e = expr->expr->to<IR::Geq>()) {
        return new IR::Lss(expr->srcInfo, expr->type, e->left, e->right);
    }
    if (const auto *e = expr->expr->to<IR::Lss>()) {
        return new IR::Geq(expr->srcInfo, expr->type, e->left, e->right);
    }
    if (const auto *e = expr->expr->to<IR::Grt>()) {
        return new IR::Leq(expr->srcInfo, expr->type, e->left, e->right);
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::Sub *expr) {
    if (isZero(expr->right)) {
        return expr->left;
    }
    if (isZero(expr->left)) {
        return new IR::Neg(expr->srcInfo, expr->type, expr->right);
    }
    if (expr->left->equiv(*expr->right) && (expr->left->type != nullptr) &&
        !expr->left->type->is<IR::Type_Unknown>()) {
        return new IR::Constant(expr->srcInfo, expr->left->type, 0);
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::Add *expr) {
    if (isZero(expr->right)) {
        return expr->left;
    }
    if (isZero(expr->left)) {
        return expr->right;
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::UPlus *expr) { return expr->expr; }

const IR::Node *ExpressionStrengthReduction::postorder(IR::Shl *expr) {
    if (isZero(expr->right)) {
        return expr->left;
    }
    if (const auto *sh2 = expr->left->to<IR::Shl>()) {
        if (sh2->right->type->is<IR::Type_InfInt>() && expr->right->type->is<IR::Type_InfInt>()) {
            // (a << b) << c is a << (b + c)
            auto *result =
                new IR::Shl(expr->srcInfo, sh2->left->type, sh2->left,
                            new IR::Add(expr->srcInfo, sh2->right->type, sh2->right, expr->right));
            LOG3("Replace " << expr << " with " << result);
            return result;
        }
    }

    if (isZero(expr->left)) {
        return expr->left;
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::Shr *expr) {
    if (isZero(expr->right)) {
        return expr->left;
    }
    if (const auto *sh2 = expr->left->to<IR::Shr>()) {
        if (sh2->right->type->is<IR::Type_InfInt>() && expr->right->type->is<IR::Type_InfInt>()) {
            // (a >> b) >> c is a >> (b + c)
            auto *result =
                new IR::Shr(expr->srcInfo, sh2->left->type, sh2->left,
                            new IR::Add(expr->srcInfo, sh2->right->type, sh2->right, expr->right));
            LOG3("Replace " << expr << " with " << result);
            return result;
        }
    }
    if (isZero(expr->left)) {
        return expr->left;
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::Mul *expr) {
    if (isOne(expr->left)) {
        return expr->right;
    }
    if (isOne(expr->right)) {
        return expr->left;
    }
    if (isZero(expr->left)) {
        return expr->left;
    }
    if (isZero(expr->right)) {
        return expr->right;
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::Div *expr) {
    if (isZero(expr->right)) {
        error(ErrorType::ERR_EXPRESSION, "%1%: Division by zero", expr);
        return expr;
    }
    if (isOne(expr->right)) {
        return expr->left;
    }

    if (isZero(expr->left)) {
        return expr->left;
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::Mod *expr) {
    if (isZero(expr->right)) {
        error(ErrorType::ERR_EXPRESSION, "%1%: Modulo by zero", expr);
        return expr;
    }
    if (isZero(expr->left)) {
        return expr->left;
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::Mux *expr) {
    if (isTrue(expr->e1) && isFalse(expr->e2)) {
        return expr->e0;
    }
    if (isFalse(expr->e1) && isTrue(expr->e2)) {
        return new IR::LNot(expr->srcInfo, expr->type, expr->e0);
    }
    if (const auto *lnot = expr->e0->to<IR::LNot>()) {
        expr->e0 = lnot->expr;
        const auto *tmp = expr->e1;
        expr->e1 = expr->e2;
        expr->e2 = tmp;
        return expr;
    }
    if (expr->e1->equiv(*expr->e2)) {
        return expr->e1;
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::Mask *mask) {
    // a &&& 0xFFFF = a
    if (isAllOnes(mask->right)) {
        return mask->left;
    }
    return mask;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::Range *range) {
    // Range a..a is the same as a
    if (const auto *c0 = range->left->to<IR::Constant>()) {
        if (const auto *c1 = range->right->to<IR::Constant>()) {
            if (c0->value == c1->value) {
                return c0;
            }
        }
    }
    return range;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::Concat *expr) {
    if (const auto *bt = expr->left->type->to<IR::Type_Bits>()) {
        if (bt->width_bits() == 0) {
            return expr->right;
        }
    }
    if (const auto *bt = expr->right->type->to<IR::Type_Bits>()) {
        if (bt->width_bits() == 0) {
            return expr->left;
        }
    }
    return expr;
}

const IR::Node *ExpressionStrengthReduction::postorder(IR::ArrayIndex *expr) {
    if (const auto *array = expr->left->to<IR::ArrayExpression>()) {
        if (const auto *cst = expr->right->to<IR::Constant>()) {
            auto index = cst->asInt();
            if (index < 0 || static_cast<size_t>(index) >= array->components.size()) {
                error(ErrorType::ERR_EXPRESSION, "%1%: Index %2% out of bounds", index, expr);
                return expr;
            }
            return array->components.at(index);
        }
    }
    return expr;
}

}  // namespace P4::P4Tools
