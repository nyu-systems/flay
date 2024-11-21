#include "backends/p4tools/modules/flay/targets/nikss/psa/expression_resolver.h"

#include <functional>
#include <optional>

#include <boost/multiprecision/cpp_int.hpp>

#include "backends/p4tools/common/lib/variables.h"
#include "backends/p4tools/modules/flay/core/interpreter/externs.h"
#include "backends/p4tools/modules/flay/targets/nikss/psa/table_executor.h"
#include "ir/ir-generated.h"
#include "ir/irutils.h"

namespace P4::P4Tools::Flay::Nikss {

PsaExpressionResolver::PsaExpressionResolver(const ProgramInfo &programInfo,
                                             ControlPlaneConstraints &constraints,
                                             ExecutionState &executionState)
    : NikssBaseExpressionResolver(programInfo, constraints, executionState) {}

const IR::Expression *PsaExpressionResolver::processTable(const IR::P4Table *table) {
    return PsaTableExecutor(*table, *this).processTable();
}

// Provides implementations of Nikss externs.
namespace PsaExterns {

using namespace P4::literals;

const ExternMethodImpls EXTERN_METHOD_IMPLS({
    /* ======================================================================================
     *  Random.read
     * ======================================================================================
     */
    {"Random.read"_cs,
     {},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         const auto *externDecl = externInfo.state.findDecl(&externInfo.externObjectRef)
                                      ->checkedTo<IR::Declaration_Instance>();
         const auto *externType = externDecl->type->checkedTo<IR::Type_Specialized>();
         BUG_CHECK(externType->arguments->size() == 1, "Expected 1 type argument, got %1%",
                   externType->arguments->size());
         const auto *valueType = externType->arguments->at(0);  // T
         auto randomLabel = externInfo.externObjectRef.path->toString() + "_" +
                            externInfo.methodName + "_" +
                            std::to_string(externInfo.originalCall.clone_id);
         return ToolsVariables::getSymbolicVariable(valueType, randomLabel);
     }},
    /* ======================================================================================
     *  assume
     *  For the purposes of compiling and executing P4 programs on a target
     *  device, assert and assume are identical, including the use of the
     *  --ndebug p4c option to elide them.  See documentation for assert.
     *  The reason that assume exists as a separate function from assert is
     *  because they are expected to be used differently by formal
     *  verification tools.  For some formal tools, the goal is to try to
     *  find example packets and sets of installed table entries that cause
     *  an assert statement condition to be false.
     *  Suppose you run such a tool on your program, and the example packet
     *  given is an MPLS packet, i.e. hdr.ethernet.etherType == 0x8847.
     *  You look at the example, and indeed it does cause an assert
     *  condition to be false.  However, your plan is to deploy your P4
     *  program in a network in places where no MPLS packets can occur.
     *  You could add extra conditions to your P4 program to handle the
     *  processing of such a packet cleanly, without assertions failing,
     *  but you would prefer to tell the tool "such example packets are not
     *  applicable in my scenario -- never show them to me".  By adding a
     *  statement:
     *      assume(hdr.ethernet.etherType != 0x8847);
     *  at an appropriate place in your program, the formal tool should
     *  never show you such examples -- only ones that make all such assume
     *  conditions true.
     *  The reason that assume statements behave the same as assert
     *  statements when compiled to a target device is that if the
     *  condition ever evaluates to false when operating in a network, it
     *  is likely that your assumption was wrong, and should be reexamined.
     * ======================================================================================
     */
    {"*method.assume"_cs,
     {"check"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) {
         // TODO: Consider the exit case?
         return nullptr;
     }},
    /* ======================================================================================
     *  assert
     *  Calling assert when the argument is true has no effect, except any
     *  effect that might occur due to evaluation of the argument (but see
     *  below).  If the argument is false, the precise behavior is
     *  target-specific, but the intent is to record or log which assert
     *  statement failed, and optionally other information about the
     *  failure.
     *  For example, on the simple_switch target, executing an assert
     *  statement with a false argument causes a log message with the file
     *  name and line number of the assert statement to be printed, and
     *  then the simple_switch process exits.
     *  If you provide the --ndebug command line option to p4c when
     *  compiling, the compiled program behaves as if all assert statements
     *  were not present in the source code.
     *  We strongly recommend that you avoid using expressions as an
     *  argument to an assert call that can have side effects, e.g. an
     *  extern method or function call that has side effects.  p4c will
     *  allow you to do this with no warning given.  We recommend this
     *  because, if you follow this advice, your program will behave the
     *  same way when assert statements are removed.
     * ======================================================================================
     */
    {"*method.assert"_cs,
     {"check"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) {
         // TODO: Consider the exit case?
         return nullptr;
     }},
    /* ======================================================================================
     *  Register.write
     * ======================================================================================
     */
    {"Register.write"_cs,
     {"index"_cs, "value"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) {
         // TODO: Implement and actually keep track of the writes.
         return nullptr;
     }},
    /* ======================================================================================
     *  Register.read
     * ======================================================================================
     */
    {"Register.read"_cs,
     {"index"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         const auto *externDecl = externInfo.state.findDecl(&externInfo.externObjectRef)
                                      ->checkedTo<IR::Declaration_Instance>();
         const auto *externType = externDecl->type->checkedTo<IR::Type_Specialized>();
         BUG_CHECK(externType->arguments->size() == 2, "Expected 2 type arguments, got %1%",
                   externType->arguments->size());
         const auto *valueType = externType->arguments->at(0);  // T
         auto registerLabel = externInfo.externObjectRef.path->toString() + "_" +
                              externInfo.methodName + "_" +
                              std::to_string(externInfo.originalCall.clone_id);
         return ToolsVariables::getSymbolicVariable(valueType, registerLabel);
     }},
    /* ======================================================================================
     *  Counter.count
     * ======================================================================================
     */
    {"Counter.count"_cs,
     {"index"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) { return nullptr; }},
    /* ======================================================================================
     *  DirectCounter.count
     */
    // TODO: Count currently has no effect in the symbolic interpreter.
    {"DirectCounter.count"_cs,
     {},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) { return nullptr; }},
    /* ======================================================================================
     *  Meter.execute
     * ======================================================================================
     */
    {"Meter.execute"_cs,
     {"index"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto randomLabel = externInfo.externObjectRef.path->toString() + "_" +
                            externInfo.methodName + "_" +
                            std::to_string(externInfo.originalCall.clone_id);
         // TODO: We are assuming 32-bit enum values here.
         return ToolsVariables::getSymbolicVariable(IR::Type_Bits::get(32), randomLabel);
     }},
    /* ======================================================================================
     *  Meter.execute
     * ======================================================================================
     */
    {"Meter.execute"_cs,
     {"index"_cs, "color"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto randomLabel = externInfo.externObjectRef.path->toString() + "_" +
                            externInfo.methodName + "_" +
                            std::to_string(externInfo.originalCall.clone_id);
         // TODO: We are assuming 32-bit enum values here.
         return ToolsVariables::getSymbolicVariable(IR::Type_Bits::get(32), randomLabel);
     }},
    /* ======================================================================================
     *  DirectMeter.execute
     * ======================================================================================
     */
    {"DirectMeter.execute"_cs,
     {},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto randomLabel = externInfo.externObjectRef.path->toString() + "_" +
                            externInfo.methodName + "_" +
                            std::to_string(externInfo.originalCall.clone_id);
         // TODO: We are assuming 32-bit enum values here.
         return ToolsVariables::getSymbolicVariable(IR::Type_Bits::get(32), randomLabel);
     }},
    /* ======================================================================================
     *  DirectMeter.execute
     * ======================================================================================
     */
    {"DirectMeter.execute"_cs,
     {"color"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto randomLabel = externInfo.externObjectRef.path->toString() + "_" +
                            externInfo.methodName + "_" +
                            std::to_string(externInfo.originalCall.clone_id);
         // TODO: We are assuming 32-bit enum values here.
         return ToolsVariables::getSymbolicVariable(IR::Type_Bits::get(32), randomLabel);
     }},
    /* ======================================================================================
     *  digest
     * ======================================================================================
     */
    {"Digest.pack"_cs,
     {"data"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) { return nullptr; }},
    /* ======================================================================================
     *  clone
     *  During the IngressDeparser execution, psa_clone_i2e returns true
     *  if and only if a clone of the ingress packet is being made to
     *  egress for the packet being processed.  If there are any
     *  assignments to the out parameter clone_i2e_meta in the
     *  IngressDeparser, they must be inside an if statement that only
     *  allows those assignments to execute if psa_clone_i2e(istd) returns
     *  true.  psa_clone_i2e can be implemented by returning istd.clone
     * ======================================================================================
     */
    // TODO: Implement
    {"*method.psa_clone_i2e"_cs,
     {"istd"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto randomLabel = externInfo.externObjectRef.path->toString() + "_" +
                            externInfo.methodName + "_" +
                            std::to_string(externInfo.originalCall.clone_id);
         return ToolsVariables::getSymbolicVariable(IR::Type_Boolean::get(), randomLabel);
     }},
    /* ======================================================================================
     *  psa_resubmit
     * During the IngressDeparser execution, psa_resubmit returns true if
     * and only if the packet is being resubmitted.  If there are any
     * assignments to the out parameter resubmit_meta in the
     * IngressDeparser, they must be inside an if statement that only
     * allows those assignments to execute if psa_resubmit(istd) returns
     * true.  psa_resubmit can be implemented by returning (!istd.drop &&
     * istd.resubmit)
     * ======================================================================================
     */
    // TODO: Implement
    {"*method.psa_resubmit"_cs,
     {"istd"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto randomLabel = externInfo.externObjectRef.path->toString() + "_" +
                            externInfo.methodName + "_" +
                            std::to_string(externInfo.originalCall.clone_id);
         return ToolsVariables::getSymbolicVariable(IR::Type_Boolean::get(), randomLabel);
     }},
    /* ======================================================================================
     *  psa_normal
     * During the IngressDeparser execution, psa_normal returns true if
     * and only if the packet is being sent 'normally' as unicast or
     * multicast to egress.  If there are any assignments to the out
     * parameter normal_meta in the IngressDeparser, they must be inside
     * an if statement that only allows those assignments to execute if
     * psa_normal(istd) returns true.  psa_normal can be implemented by
     * returning (!istd.drop && !istd.resubmit)
     * ======================================================================================
     */
    // TODO: Implement
    {"*method.psa_normal"_cs,
     {"istd"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto randomLabel = externInfo.externObjectRef.path->toString() + "_" +
                            externInfo.methodName + "_" +
                            std::to_string(externInfo.originalCall.clone_id);
         return ToolsVariables::getSymbolicVariable(IR::Type_Boolean::get(), randomLabel);
     }},
    /* ======================================================================================
     *  psa_clone_e2e
     * During the EgressDeparser execution, psa_clone_e2e returns true if
     * and only if a clone of the egress packet is being made to egress
     * for the packet being processed.  If there are any assignments to
     * the out parameter clone_e2e_meta in the EgressDeparser, they must
     * be inside an if statement that only allows those assignments to
     * execute if psa_clone_e2e(istd) returns true.  psa_clone_e2e can be
     * implemented by returning istd.clone
     * ======================================================================================
     */
    // TODO: Implement
    {"*method.psa_clone_e2e"_cs,
     {"istd"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto randomLabel = externInfo.externObjectRef.path->toString() + "_" +
                            externInfo.methodName + "_" +
                            std::to_string(externInfo.originalCall.clone_id);
         return ToolsVariables::getSymbolicVariable(IR::Type_Boolean::get(), randomLabel);
     }},
    /* ======================================================================================
     *  psa_recirculate
     * During the EgressDeparser execution, psa_recirculate returns true
     * if and only if the packet is being recirculated.  If there are any
     * assignments to recirculate_meta in the EgressDeparser, they must
     * be inside an if statement that only allows those assignments to
     * execute if psa_recirculate(istd) returns true.  psa_recirculate
     * can be implemented by returning (!istd.drop && (edstd.egress_port
     * == PSA_PORT_RECIRCULATE))
     * ======================================================================================
     */
    // TODO: Implement
    {"*method.psa_recirculate"_cs,
     {"istd"_cs, "edstd"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto randomLabel = externInfo.externObjectRef.path->toString() + "_" +
                            externInfo.methodName + "_" +
                            std::to_string(externInfo.originalCall.clone_id);
         return ToolsVariables::getSymbolicVariable(IR::Type_Boolean::get(), randomLabel);
     }},
    /* ======================================================================================
     *  Hash.get_hash
     * Compute the hash for data.
     * @param data The data over which to calculate the hash.
     * @return The hash value.
     * ======================================================================================
     */
    {"Hash.get_hash"_cs,
     {"data"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         const auto *externDecl = externInfo.state.findDecl(&externInfo.externObjectRef)
                                      ->checkedTo<IR::Declaration_Instance>();
         const auto *externType = externDecl->type->checkedTo<IR::Type_Specialized>();
         BUG_CHECK(externType->arguments->size() == 1, "Expected 1 type argument, got %1%",
                   externType->arguments->size());
         const auto *valueType = externType->arguments->at(0);  // O

         auto hashLabel = externInfo.externObjectRef.path->toString() + "_" +
                          externInfo.methodName + "_" +
                          std::to_string(externInfo.originalCall.clone_id);
         return ToolsVariables::getSymbolicVariable(valueType, hashLabel);
     }},
    /* ======================================================================================
     *  Hash.get_hash
     * Compute the hash for data, with modulo by max, then add base.
     * @param base Minimum return value.
     * @param data The data over which to calculate the hash.
     * @param max The hash value is divided by max to get modulo.
     *        An implementation may limit the largest value supported,
     *        e.g. to a value like 32, or 256, and may also only
     *        support powers of 2 for this value.  P4 developers should
     *        limit their choice to such values if they wish to
     *        maximize portability.
     * @return (base + (h % max)) where h is the hash value.
     * ======================================================================================
     */
    {"Hash.get_hash"_cs,
     {"base"_cs, "data"_cs, "max"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         const auto *externDecl = externInfo.state.findDecl(&externInfo.externObjectRef)
                                      ->checkedTo<IR::Declaration_Instance>();
         const auto *externType = externDecl->type->checkedTo<IR::Type_Specialized>();
         BUG_CHECK(externType->arguments->size() == 1, "Expected 1 type argument, got %1%",
                   externType->arguments->size());
         const auto *valueType = externType->arguments->at(0);  // O

         auto hashLabel = externInfo.externObjectRef.path->toString() + "_" +
                          externInfo.methodName + "_" +
                          std::to_string(externInfo.originalCall.clone_id);
         return ToolsVariables::getSymbolicVariable(valueType, hashLabel);
     }},
    /* ======================================================================================
     *  Checksum.clear
     * Reset internal state and prepare unit for computation.
     * Every instance of a Checksum object is automatically initialized as
     * if clear() had been called on it. This initialization happens every
     * time the object is instantiated, that is, whenever the parser or control
     * containing the Checksum object are applied.
     * All state maintained by the Checksum object is independent per packet.
     * ======================================================================================
     */
    {"Checksum.clear"_cs,
     {},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) { return nullptr; }},
    /* ======================================================================================
     * Checksum.update
     * Add data to checksum

     * ======================================================================================
     */
    {"Checksum.update"_cs,
     {"data"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) { return nullptr; }},
    /* ======================================================================================
     * Checksum.get
     * Get checksum for data added (and not removed) since last clear
     * ======================================================================================
     */
    {"Checksum.get"_cs,
     {},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         const auto *externDecl = externInfo.state.findDecl(&externInfo.externObjectRef)
                                      ->checkedTo<IR::Declaration_Instance>();
         const auto *externType = externDecl->type->checkedTo<IR::Type_Specialized>();
         BUG_CHECK(externType->arguments->size() == 1, "Expected 1 type argument, got %1%",
                   externType->arguments->size());
         const auto *valueType = externType->arguments->at(0);  // W
         // We do not calculate the checksum for now and instead use a dummy.
         auto verifyChecksumLabel = externInfo.externObjectRef.path->toString() + "_" +
                                    externInfo.methodName + "_" +
                                    std::to_string(externInfo.originalCall.clone_id);
         const auto *hashCalc = ToolsVariables::getSymbolicVariable(valueType, verifyChecksumLabel);
         return hashCalc;
     }},
    /* ======================================================================================
     *  InternetChecksum.clear
     * Reset internal state and prepare unit for computation.  Every
     * instance of an InternetChecksum object is automatically
     * initialized as if clear() had been called on it, once for each
     * time the parser or control it is instantiated within is
     * executed.  All state maintained by it is independent per packet.
     * ======================================================================================
     */
    {"InternetChecksum.clear"_cs,
     {},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) { return nullptr; }},
    /* ======================================================================================
     * InternetChecksum.add
     * Add data to checksum.  data must be a multiple of 16 bits long.
     * ======================================================================================
     */
    {"InternetChecksum.add"_cs,
     {"data"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) { return nullptr; }},
    /* ======================================================================================
     * InternetChecksum.subtract
     * Subtract data from existing checksum.  data must be a multiple of
     * 16 bits long.
    ======================================================================================
    */
    {"InternetChecksum.subtract"_cs,
     {"data"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) { return nullptr; }},
    /* ======================================================================================
     * InternetChecksum.get
     * Get checksum for data added (and not removed) since last clear
     * ======================================================================================
     */
    {"InternetChecksum.get"_cs,
     {},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         // We do not calculate the checksum for now and instead use a dummy.
         const auto *sixteenBitType = IR::Type_Bits::get(16);
         auto verifyChecksumLabel = externInfo.externObjectRef.path->toString() + "_" +
                                    externInfo.methodName + "_" +
                                    std::to_string(externInfo.originalCall.clone_id);
         const auto *hashCalc =
             ToolsVariables::getSymbolicVariable(sixteenBitType, verifyChecksumLabel);
         return hashCalc;
     }},
    /* ======================================================================================
     * InternetChecksum.get_state
     * Get current state of checksum computation.  The return value is
     * only intended to be used for a future call to the set_state
     * method.
     * ======================================================================================
     */
    {"InternetChecksum.get_state"_cs,
     {},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         // We do not calculate the checksum for now and instead use a dummy.
         const auto *sixteenBitType = IR::Type_Bits::get(16);
         auto verifyChecksumLabel = externInfo.externObjectRef.path->toString() + "_" +
                                    externInfo.methodName + "_" +
                                    std::to_string(externInfo.originalCall.clone_id);
         const auto *hashCalc =
             ToolsVariables::getSymbolicVariable(sixteenBitType, verifyChecksumLabel);
         return hashCalc;
     }},
    /* ======================================================================================
     * InternetChecksum.`set_state
     * Subtract data from existing checksum.  data must be a multiple of
     * 16 bits long.
    ======================================================================================
    */
    {"InternetChecksum.set_state"_cs,
     {"data"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) { return nullptr; }},
});
}  // namespace PsaExterns

const IR::Expression *PsaExpressionResolver::processExtern(
    const ExternMethodImpls::ExternInfo &externInfo) {
    auto method = PsaExterns::EXTERN_METHOD_IMPLS.find(
        externInfo.externObjectRef, externInfo.methodName, externInfo.externArgs);
    if (method.has_value()) {
        return method.value()(externInfo);
    }
    return NikssBaseExpressionResolver::processExtern(externInfo);
}

}  // namespace P4::P4Tools::Flay::Nikss
