#ifndef BACKENDS_P4TOOLS_MODULES_FLAY_CORE_INTERPRETER_EXECUTION_STATE_H_
#define BACKENDS_P4TOOLS_MODULES_FLAY_CORE_INTERPRETER_EXECUTION_STATE_H_

#include <optional>

#include "backends/p4tools/common/core/abstract_execution_state.h"
#include "backends/p4tools/modules/flay/core/interpreter/node_map.h"
#include "ir/ir.h"
#include "ir/node.h"

namespace P4::P4Tools::Flay {

/// Represents state of execution after having reached a program point.
class ExecutionState : public AbstractExecutionState {
 private:
    /// The condition necessary to reach this particular execution state. Defaults
    /// to true.
    const IR::Expression *executionCondition;

    /// Keeps track of the annotations on individual nodes in the program, for example reachability.
    NodeAnnotationMap _nodeAnnotationMap;

    /// A static label for placeholder variables used in Flay.
    static const IR::PathExpression PLACEHOLDER_LABEL;
    /* =========================================================================================
     *  Accessors
     * =========================================================================================
     */
 public:
    /// @returns the value associated with the given state variable.
    [[nodiscard]] const IR::Expression *get(const IR::StateVariable &var) const override;

    /// Sets the symbolic value of the given state variable to the given value.
    /// Constant folding is done on the given value before updating the symbolic
    /// state.
    void set(const IR::StateVariable &var, const IR::Expression *value) override;

    /// @returns a symbolic expression using the id and label provided.
    /// Also handles complex expressions such as structs or headers.
    const IR::Expression *createSymbolicExpression(const IR::Type *inputType, cstring label) const;

    /* =========================================================================================
     *  State merging and execution conditions.
     * =========================================================================================
     */
    /// Push an execution condition into this particular state.
    void pushExecutionCondition(const IR::Expression *cond);

    /// @returns the execution condition associated with this state.
    [[nodiscard]] const IR::Expression *getExecutionCondition() const;

    /// Merge another execution state under its path condition, or under an explicit condition
    /// when the caller has established that it covers the incoming state's domain.
    void merge(const ExecutionState &mergeState, const IR::Expression *condition = nullptr);

    /// Join mutually exclusive incoming paths at a common program point. Unlike merge(),
    /// this also widens the execution condition to cover both incoming paths.
    void join(const ExecutionState &other);

    /// @returns the node annotation map associated with this state.
    [[nodiscard]] const NodeAnnotationMap &nodeAnnotationMap() const;

    /// Map the conditions to be reachable to a particular program node.
    void addReachabilityMapping(const IR::Node *node, const IR::Expression *cond);

    /// Map the interpreter value to a particular expression in the program.
    void addExpressionMapping(const IR::Expression *expression, const IR::Expression *value);

    /// Convenience function to set the value of a placeholder variables in the
    /// symbolic environment. An example where placeholder variables are necessary
    /// is recirculation. We can not immediately know the conditions to cover a
    /// particular program node and need to assign a placeholder.
    void setPlaceholderValue(cstring label, const IR::Expression *value);

    /// @returns the assigned value for a particular placeholder in this execution
    /// state. Returns std::nullopt if no value was found.
    [[nodiscard]] std::optional<const IR::Expression *> getPlaceholderValue(
        const IR::Placeholder &placeholder) const;

    /// Substitute IR::Placeholder expression in all reachability conditions
    /// presents in this execution state.
    void substitutePlaceholders();

    /* =========================================================================================
     *  Constructors
     * =========================================================================================
     */
    /// Creates an initial execution state for the given program.
    explicit ExecutionState(const IR::P4Program *program);

    /// Allocate a new execution state object with the same state as this object.
    /// Returns a reference, not a pointer.
    [[nodiscard]] ExecutionState &clone() const override;

    /// Create a new execution state object from the input program.
    /// Returns a reference not a pointer.
    [[nodiscard]] static ExecutionState &create(const IR::P4Program *program);

    /// Do not accidentally copy-assign the execution state.
    ExecutionState &operator=(const ExecutionState &) = delete;

    ExecutionState &operator=(ExecutionState &&) = delete;

    ExecutionState(ExecutionState &&) = default;

    ~ExecutionState() override = default;

 private:
    /// Execution state needs to be explicitly copied using the @ref clone call..
    ExecutionState(const ExecutionState &) = default;
};

}  // namespace P4::P4Tools::Flay

#endif /* BACKENDS_P4TOOLS_MODULES_FLAY_CORE_INTERPRETER_EXECUTION_STATE_H_ */
