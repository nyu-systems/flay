#ifndef BACKENDS_P4TOOLS_MODULES_FLAY_CORE_INTERPRETER_PARSER_STEPPER_H_
#define BACKENDS_P4TOOLS_MODULES_FLAY_CORE_INTERPRETER_PARSER_STEPPER_H_

#include <functional>
#include <map>
#include <vector>

#include "backends/p4tools/modules/flay/core/interpreter/execution_state.h"
#include "backends/p4tools/modules/flay/core/interpreter/program_info.h"
#include "backends/p4tools/modules/flay/core/interpreter/stepper.h"
#include "ir/ir.h"
#include "ir/node.h"
#include "ir/visitor.h"

namespace P4::P4Tools::Flay {

class ParserStepper : public Inspector {
 public:
    /// A parser exit state is a state in which the parser has finished parsing.
    /// This could either be a reject or an accept.
    using ParserExitState = std::reference_wrapper<const ExecutionState>;

 private:
    /// Reference to the main stepper calling this parser stepper.
    std::reference_wrapper<FlayStepper> stepper;

    /// The list of parser exit states associated with this parser.
    std::vector<ParserExitState> parserExitStates;

    /// Incoming symbolic states, joined before their destination is interpreted.
    std::map<const IR::ParserState *, ExecutionState *> incomingStates;

    /// Schedule an incoming state in the parser namespace (outside any state namespace).
    void enqueue(const IR::ParserState *destination, ExecutionState &state);

    /// Add an exit state to the parser.
    void addParserExitState(const ExecutionState &state);

    /// Interpret each reachable parser state once, after joining all incoming paths.
    void processParserStates(const IR::P4Parser *parser);

    /// Schedule select destinations under mutually exclusive first-match conditions.
    void processSelectExpression(const IR::SelectExpression *selectExpr);

    /// Visitor methods.
    bool preorder(const IR::Node *node) override;
    bool preorder(const IR::P4Parser *parser) override;
    bool preorder(const IR::ParserState *parserState) override;

 protected:
    /// @returns the current execution state.
    ExecutionState &getExecutionState() const;

    /// @returns the program info associated with the current target.
    virtual const ProgramInfo &getProgramInfo() const;

 public:
    explicit ParserStepper(FlayStepper &stepper);

    /// @returns the parser exit states associated with this parser stepper.
    const std::vector<ParserExitState> &getParserExitStates() const;
};

}  // namespace P4::P4Tools::Flay

#endif /* BACKENDS_P4TOOLS_MODULES_FLAY_CORE_INTERPRETER_PARSER_STEPPER_H_ */
