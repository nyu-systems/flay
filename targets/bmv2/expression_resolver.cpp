#include "backends/p4tools/modules/flay/targets/bmv2/expression_resolver.h"

#include <functional>
#include <optional>
#include <string>

#include <boost/multiprecision/cpp_int.hpp>

#include "backends/p4tools/common/lib/symbolic_variables.h"
#include "backends/p4tools/common/lib/variables.h"
#include "backends/p4tools/modules/flay/core/interpreter/externs.h"
#include "backends/p4tools/modules/flay/targets/bmv2/constants.h"
#include "backends/p4tools/modules/flay/targets/bmv2/table_executor.h"
#include "ir/id.h"
#include "ir/irutils.h"
#include "ir/vector.h"
#include "lib/cstring.h"
#include "lib/exceptions.h"

namespace P4::P4Tools::Flay::V1Model {

V1ModelExpressionResolver::V1ModelExpressionResolver(const ProgramInfo &programInfo,
                                                     ControlPlaneConstraints &constraints,
                                                     ExecutionState &executionState)
    : ExpressionResolver(programInfo, constraints, executionState) {}

const IR::Expression *V1ModelExpressionResolver::processTable(const IR::P4Table *table) {
    return V1ModelTableExecutor(*table, *this).processTable();
}

// Provides implementations of BMv2 externs.
namespace Bmv2V1modelExterns {

using namespace P4::literals;

const ExternMethodImpls EXTERN_METHOD_IMPLS({
    {"*method.mark_to_drop"_cs,
     {"standard_metadata"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto &state = externInfo.state;

         const auto *nineBitType = IR::Type_Bits::get(9);
         const auto &metadataLabel =
             externInfo.externArgs->at(0)->expression->checkedTo<IR::InOutReference>()->ref;

         // Use an assignment to set egress_spec to true.
         // This variable will be processed in the deparser.
         const auto *portVar = new IR::Member(nineBitType, metadataLabel, "egress_spec");
         state.set(portVar, IR::Constant::get(nineBitType, V1ModelConstants::DROP_PORT));
         return nullptr;
     }},
    /* ======================================================================================
     *  random
     *  Generate a random number in the range lo..hi, inclusive, and write
     *  it to the result parameter.
     * ======================================================================================
     */
    {"*method.random"_cs,
     {"result"_cs, "lo"_cs, "hi"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto &state = externInfo.state;
         const auto &fieldRef =
             externInfo.externArgs->at(0)->expression->checkedTo<IR::InOutReference>()->ref;
         auto randomLabel = externInfo.externObjectRef.path->toString() + "_" +
                            externInfo.methodName + "_" +
                            std::to_string(externInfo.originalCall.clone_id);
         state.set(fieldRef, ToolsVariables::getSymbolicVariable(fieldRef->type, randomLabel));
         return nullptr;
     }},
    /* ======================================================================================
     *  log_msg
     *  Log user defined messages
     *  Example: log_msg("User defined message");
     *  or log_msg("Value1 = {}, Value2 = {}"_cs,{value1, value2});
     * ======================================================================================
     */
    {"*method.log_msg"_cs,
     {"msg"_cs, "args"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) { return nullptr; }},
    /* ======================================================================================
     *  log_msg
     *  Log user defined messages
     *  Example: log_msg("User defined message");
     *  or log_msg("Value1 = {}, Value2 = {}"_cs,{value1, value2});
     * ======================================================================================
     */
    {"*method.log_msg"_cs,
     {"msg"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) {
         // Log msg is a no-op.
         return nullptr;
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
     *  register.write
     *
     *  write() writes the state of the register array at the specified
     *  index, with the value provided by the value parameter.
     *
     *  If you wish to perform a read() followed later by a write() to
     *  the same register array element, and you wish the
     *  read-modify-write sequence to be atomic relative to other
     *  processed packets, then there may be parallel implementations
     *  of the v1model architecture for which you must execute them in
     *  a P4_16 block annotated with an @atomic annotation.  See the
     *  P4_16 language specification description of the @atomic
     *  annotation for more details.
     *
     *  @param index The index of the register array element to be
     *               written, normally a value in the range [0,
     *               size-1].  If index >= size, no register state will
     *               be updated.
     *  @param value Only types T that are bit<W> are currently
     *               supported.  When index is in range, this
     *               parameter's value is written into the register
     *               array element specified by index.
     *
     * ======================================================================================
     */
    {"register.write"_cs,
     {"index"_cs, "value"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) {
         // TODO: Implement and actually keep track of the writes.
         return nullptr;
     }},
    /* ======================================================================================
     *  register.read
     *
     *  read() reads the state of the register array stored at the
     *  specified index, and returns it as the value written to the
     *  result parameter.
     *
     *  @param index The index of the register array element to be
     *               read, normally a value in the range [0, size-1].
     *  @param result Only types T that are bit<W> are currently
     *               supported.  When index is in range, the value of
     *               result becomes the value read from the register
     *               array element.  When index >= size, the final
     *               value of result is not specified, and should be
     *               ignored by the caller.
     *
     * ======================================================================================
     */
    {"register.read"_cs,
     {"result"_cs, "index"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto &state = externInfo.state;

         // TODO: Implement and actually keep track of the writes.
         const auto &resultVar =
             externInfo.externArgs->at(0)->expression->checkedTo<IR::InOutReference>()->ref;

         auto registerLabel = externInfo.externObjectRef.path->toString() + "_" +
                              externInfo.methodName + "_" +
                              std::to_string(externInfo.originalCall.clone_id);
         const auto *hashCalc = ToolsVariables::getSymbolicVariable(resultVar->type, registerLabel);
         state.set(resultVar, hashCalc);
         return nullptr;
     }},
    /* ======================================================================================
     *  counter.count
     *  A counter object is created by calling its constructor.  This
     *  creates an array of counter states, with the number of counter
     *  states specified by the size parameter.  The array indices are
     *  in the range [0, size-1].
     *
     *  You must provide a choice of whether to maintain only a packet
     *  count (CounterType.packets), only a byte count
     *  (CounterType.bytes), or both (CounterType.packets_and_bytes).
     *
     *  Counters can be updated from your P4 program, but can only be
     *  read from the control plane.  If you need something that can be
     *  both read and written from the P4 program, consider using a
     *  register.
     *  count() causes the counter state with the specified index to be
     *  read, modified, and written back, atomically relative to the
     *  processing of other packets, updating the packet count, byte
     *  count, or both, depending upon the CounterType of the counter
     *  instance used when it was constructed.
     *
     *  @param index The index of the counter state in the array to be
     *               updated, normally a value in the range [0,
     *               size-1].  If index >= size, no counter state will be
     *               updated.
     * ======================================================================================
     */
    {"counter.count"_cs,
     {"index"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) { return nullptr; }},
    /* ======================================================================================
     *  direct_counter.count
     *  A direct_counter object is created by calling its constructor.
     *  You must provide a choice of whether to maintain only a packet
     *  count (CounterType.packets), only a byte count
     *  (CounterType.bytes), or both (CounterType.packets_and_bytes).
     *  After constructing the object, you can associate it with at
     *  most one table, by adding the following table property to the
     *  definition of that table:
     *
     *      counters = <object_name>;
     *
     *  Counters can be updated from your P4 program, but can only be
     *  read from the control plane.  If you need something that can be
     *  both read and written from the P4 program, consider using a
     *  register.
     *  The count() method is actually unnecessary in the v1model
     *  architecture.  This is because after a direct_counter object
     *  has been associated with a table as described in the
     *  documentation for the direct_counter constructor, every time
     *  the table is applied and a table entry is matched, the counter
     *  state associated with the matching entry is read, modified, and
     *  written back, atomically relative to the processing of other
     *  packets, regardless of whether the count() method is called in
     *  the body of that action.
     * ======================================================================================
     */
    // TODO: Count currently has no effect in the symbolic interpreter.
    {"direct_counter.count"_cs,
     {},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) { return nullptr; }},
    /* ======================================================================================
     *  meter.execute_meter
     *  A meter object is created by calling its constructor.  This
     *  creates an array of meter states, with the number of meter
     *  states specified by the size parameter.  The array indices are
     *  in the range [0, size-1].  For example, if in your system you
     *  have 128 different "flows" numbered from 0 up to 127, and you
     *  want to meter each of those flows independently of each other,
     *  you could do so by creating a meter object with size=128.
     *
     *  You must provide a choice of whether to meter based on the
     *  number of packets, regardless of their size
     *  (MeterType.packets), or based upon the number of bytes the
     *  packets contain (MeterType.bytes).
     *  execute_meter() causes the meter state with the specified index
     *  to be read, modified, and written back, atomically relative to
     *  the processing of other packets, and an integer encoding of one
     *  of the colors green, yellow, or red to be written to the result
     *  out parameter.
     *  @param index The index of the meter state in the array to be
     *               updated, normally a value in the range [0,
     *               size-1].  If index >= size, no meter state will be
     *               updated.
     *  @param result Type T must be bit<W> with W >= 2.  When index is
     *               in range, the value of result will be assigned 0
     *               for color GREEN, 1 for color YELLOW, and 2 for
     *               color RED (see RFC 2697 and RFC 2698 for the
     *               meaning of these colors).  When index is out of
     *               range, the final value of result is not specified,
     *  and should be ignored by the caller.
     * ======================================================================================
     */
    {"meter.execute_meter"_cs,
     {"index"_cs, "result"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) { return nullptr; }},
    /* ======================================================================================
     *  direct_meter.read
     *  A direct_meter object is created by calling its constructor.
     *  You must provide a choice of whether to meter based on the
     *  number of packets, regardless of their size
     *  (MeterType.packets), or based upon the number of bytes the
     *  packets contain (MeterType.bytes).  After constructing the
     *  object, you can associate it with at most one table, by adding
     *  the following table property to the definition of that table:
     *
     *      meters = <object_name>;
     *  After a direct_meter object has been associated with a table as
     *  described in the documentation for the direct_meter
     *  constructor, every time the table is applied and a table entry
     *  is matched, the meter state associated with the matching entry
     *  is read, modified, and written back, atomically relative to the
     *  processing of other packets, regardless of whether the read()
     *  method is called in the body of that action.
     *
     *  read() may only be called within an action executed as a result
     *  of matching a table entry, of a table that has a direct_meter
     *  associated with it.  Calling read() causes an integer encoding
     *  of one of the colors green, yellow, or red to be written to the
     *  result out parameter.
     *
     *  @param result Type T must be bit<W> with W >= 2.  The value of
     *               result will be assigned 0 for color GREEN, 1 for
     *               color YELLOW, and 2 for color RED (see RFC 2697
     *               and RFC 2698 for the meaning of these colors).
     * ======================================================================================
     */
    {"direct_meter.read"_cs,
     {"result"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) { return nullptr; }},
    /* ======================================================================================
     *  digest
     *  Calling digest causes a message containing the values specified in
     *  the data parameter to be sent to the control plane software.  It is
     *  similar to sending a clone of the packet to the control plane
     *  software, except that it can be more efficient because the messages
     *  are typically smaller than packets, and many such small digest
     *  messages are typically coalesced together into a larger "batch"
     *  which the control plane software processes all at once.
     *
     *  The value of the fields that are sent in the message to the control
     *  plane is the value they have at the time the digest call occurs,
     *  even if those field values are changed by later ingress control
     *  code.  See Note 3.
     *
     *  Calling digest is only supported in the ingress control.  There is
     *  no way to undo its effects once it has been called.
     *
     *  If the type T is a named struct, the name is used to generate the
     *  control plane API.
     *
     *  The BMv2 implementation of the v1model architecture ignores the
     *  value of the receiver parameter.
     * ======================================================================================
     */
    {"*method.digest"_cs,
     {"receiver"_cs, "data"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) { return nullptr; }},
    /* ======================================================================================
     *  clone_preserving_field_list
     *  Calling clone_preserving_field_list during execution of the ingress
     *  or egress control will cause the packet to be cloned, sometimes
     *  also called mirroring, i.e. zero or more copies of the packet are
     *  made, and each will later begin egress processing as an independent
     *  packet from the original packet.  The original packet continues
     *  with its normal next steps independent of the clone(s).
     *
     *  The session parameter is an integer identifying a clone session id
     *  (sometimes called a mirror session id).  The control plane software
     *  must configure each session you wish to use, or else no clones will
     *  be made using that session.  Typically this will involve the
     *  control plane software specifying one output port to which the
     *  cloned packet should be sent, or a list of (port, egress_rid) pairs
     *  to which a separate clone should be created for each, similar to
     *  multicast packets.
     *
     *  Cloned packets can be distinguished from others by the value of the
     *  standard_metadata instance_type field.
     *
     *  The user metadata fields that are tagged with @field_list(index) will be
     *  sent to the parser together with a clone of the packet.
     *
     *  If clone_preserving_field_list is called during ingress processing,
     *  the first parameter must be CloneType.I2E.  If
     *  clone_preserving_field_list is called during egress processing, the
     *  first parameter must be CloneType.E2E.
     *
     *  There is no way to undo its effects once it has been called.  If
     *  there are multiple calls to clone_preserving_field_list and/or
     *  clone during a single execution of the same ingress (or egress)
     *  control, only the last clone session and index are used.  See the
     *  v1model architecture documentation (Note 1) for more details.
     * ======================================================================================
     */
    {"*method.clone_preserving_field_list"_cs,
     {"type"_cs, "session"_cs, "data"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto &state = externInfo.state;

         auto cloneType =
             externInfo.externArgs->at(0)->expression->checkedTo<IR::Constant>()->asUint64();
         const auto *sessionID = externInfo.externArgs->at(1)->expression;

         const auto *instanceBitType = IR::Type_Bits::get(32);
         auto *isCloned = new IR::LAnd(
             Bmv2ControlPlaneState::getCloneActive(),
             new IR::Equ(sessionID, Bmv2ControlPlaneState::getCloneSessionId(sessionID->type)));
         const IR::Constant *instanceTypeConst = nullptr;
         if (cloneType == V1ModelConstants::CloneType::I2E) {
             instanceTypeConst = IR::Constant::get(
                 instanceBitType, V1ModelConstants::PKT_INSTANCE_TYPE_INGRESS_CLONE);
         } else if (cloneType == V1ModelConstants::CloneType::E2E) {
             instanceTypeConst = IR::Constant::get(
                 instanceBitType, V1ModelConstants::PKT_INSTANCE_TYPE_EGRESS_CLONE);
         } else {
             P4C_UNIMPLEMENTED("Unsupported clone type %1%.", cloneType);
         }
         // Initialize instance_type with a place holder.
         auto *mux = new IR::Mux(
             isCloned, instanceTypeConst,
             IR::Constant::get(instanceBitType, V1ModelConstants::PKT_INSTANCE_TYPE_NORMAL));
         state.setPlaceholderValue("standard_metadata.instance_type"_cs, mux);
         return nullptr;
     }},
    /* ======================================================================================
     *  resubmit_preserving_field_list
     *  Calling resubmit_preserving_field_list during execution of the
     *  ingress control will cause the packet to be resubmitted, i.e. it
     *  will begin processing again with the parser, with the contents of
     *  the packet exactly as they were when it last began parsing.  The
     *  only difference is in the value of the standard_metadata
     *  instance_type field, and any user-defined metadata fields that the
     *  resubmit_preserving_field_list operation causes to be preserved.
     *
     *  The user metadata fields that are tagged with @field_list(index) will
     *  be sent to the parser together with the packet.
     *
     *  Calling resubmit_preserving_field_list is only supported in the
     *  ingress control.  There is no way to undo its effects once it has
     *  been called.  If resubmit_preserving_field_list is called multiple
     *  times during a single execution of the ingress control, only one
     *  packet is resubmitted, and only the user-defined metadata fields
     *  specified by the field list index from the last such call are
     *  preserved.  See the v1model architecture documentation (Note 1) for
     *  more details.
     *
     *  For example, the user metadata fields can be annotated as follows:
     *  struct UM {
     *     @field_list(1)
     *     bit<32> x;
     *     @field_list(1, 2)
     *     bit<32> y;
     *     bit<32> z;
     *  }
     *
     *  Calling resubmit_preserving_field_list(1) will resubmit the packet
     *  and preserve fields x and y of the user metadata.  Calling
     *  resubmit_preserving_field_list(2) will only preserve field y.
     * ======================================================================================
     */
    {"*method.resubmit_preserving_field_list"_cs,
     {"data"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) { return nullptr; }},
    /* ======================================================================================
     *  recirculate_preserving_field_list
     * Calling recirculate_preserving_field_list during execution of the
     * egress control will cause the packet to be recirculated, i.e. it
     * will begin processing again with the parser, with the contents of
     * the packet as they are created by the deparser.  Recirculated
     * packets can be distinguished from new packets in ingress processing
     * by the value of the standard_metadata instance_type field.  The
     * caller may request that some user-defined metadata fields be
     * preserved with the recirculated packet.
     * The user metadata fields that are tagged with @field_list(index) will be
     * sent to the parser together with the packet.
     * Calling recirculate_preserving_field_list is only supported in the
     * egress control.  There is no way to undo its effects once it has
     * been called.  If recirculate_preserving_field_list is called
     * multiple times during a single execution of the egress control,
     * only one packet is recirculated, and only the user-defined metadata
     * fields specified by the field list index from the last such call
     * are preserved.  See the v1model architecture documentation (Note 1)
     * for more details.
     * ======================================================================================
     */
    {"*method.recirculate_preserving_field_list"_cs,
     {"index"_cs},
     [](const ExternMethodImpls::ExternInfo & /*externInfo*/) { return nullptr; }},
    /* ======================================================================================
     *  clone
     *  clone is in most ways identical to the clone_preserving_field_list
     *  operation, with the only difference being that it never preserves
     *  any user-defined metadata fields with the cloned packet.  It is
     *  equivalent to calling clone_preserving_field_list with the same
     *  type and session parameter values, with empty data.
     * ======================================================================================
     */
    {"*method.clone"_cs,
     {"type"_cs, "session"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto &state = externInfo.state;

         auto cloneType =
             externInfo.externArgs->at(0)->expression->checkedTo<IR::Constant>()->asUint64();
         const auto *sessionID = externInfo.externArgs->at(1)->expression;

         const auto *instanceBitType = IR::Type_Bits::get(32);
         auto *isCloned = new IR::LAnd(
             Bmv2ControlPlaneState::getCloneActive(),
             new IR::Equ(sessionID, Bmv2ControlPlaneState::getCloneSessionId(sessionID->type)));
         const IR::Constant *instanceTypeConst = nullptr;
         if (cloneType == V1ModelConstants::CloneType::I2E) {
             instanceTypeConst = IR::Constant::get(
                 instanceBitType, V1ModelConstants::PKT_INSTANCE_TYPE_INGRESS_CLONE);
         } else if (cloneType == V1ModelConstants::CloneType::E2E) {
             instanceTypeConst = IR::Constant::get(
                 instanceBitType, V1ModelConstants::PKT_INSTANCE_TYPE_EGRESS_CLONE);
         } else {
             P4C_UNIMPLEMENTED("Unsupported clone type %1%.", cloneType);
         }
         // Initialize instance_type with a place holder.
         auto *mux = new IR::Mux(
             isCloned, instanceTypeConst,
             IR::Constant::get(instanceBitType, V1ModelConstants::PKT_INSTANCE_TYPE_NORMAL));
         state.setPlaceholderValue("standard_metadata.instance_type"_cs, mux);
         return nullptr;
     }},

    /* ======================================================================================
     *  hash
     *  Calculate a hash function of the value specified by the data
     *  parameter.  The value written to the out parameter named result
     *  will always be in the range [base, base+max-1] inclusive, if max >=
     *  1.  If max=0, the value written to result will always be base.
     *  Note that the types of all of the parameters may be the same as, or
     *  different from, each other, and thus their bit widths are allowed
     *  to be different.
     *  @param O          Must be a type bit<W>
     *  @param D          Must be a tuple type where all the fields are bit-fields (type
     * bit<W> or int<W>) or varbits.
     *  @param T          Must be a type bit<W>
     *  @param M          Must be a type bit<W>
     * ======================================================================================
     */
    {"*method.hash"_cs,
     {"result"_cs, "algo"_cs, "base"_cs, "data"_cs, "max"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto &state = externInfo.state;

         // TODO: We can actually make this more complex.
         const auto &resultVar =
             externInfo.externArgs->at(0)->expression->checkedTo<IR::InOutReference>()->ref;

         auto hashLabel = externInfo.externObjectRef.path->toString() + "_" +
                          externInfo.methodName + "_" +
                          std::to_string(externInfo.originalCall.clone_id);
         const auto *hashCalc = ToolsVariables::getSymbolicVariable(resultVar->type, hashLabel);
         state.set(resultVar, hashCalc);
         return nullptr;
     }},
    /* ======================================================================================
     * Checksum16.get
     * ======================================================================================
     */
    {"Checksum16.get"_cs,
     {"data"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto checksumLabel = externInfo.externObjectRef.path->toString() + "_" +
                              externInfo.methodName + "_" +
                              std::to_string(externInfo.originalCall.clone_id);
         const auto *hashCalc =
             ToolsVariables::getSymbolicVariable(IR::Type_Bits::get(16), checksumLabel);
         return hashCalc;
     }},
    /* ======================================================================================
     * verify_checksum
     *  Verifies the checksum of the supplied data.  If this method detects
     *  that a checksum of the data is not correct, then the value of the
     *  standard_metadata checksum_error field will be equal to 1 when the
     *  packet begins ingress processing.
     *
     *  Calling verify_checksum is only supported in the VerifyChecksum
     *  control.
     *
     *  @param T          Must be a tuple type where all the tuple elements
     *                    are of type bit<W>, int<W>, or varbit<W>.  The
     *                    total length of the fields must be a multiple of
     *                    the output size.
     *  @param O          Checksum type; must be bit<X> type.
     *  @param condition  If 'false' the verification always succeeds.
     *  @param data       Data whose checksum is verified.
     *  @param checksum   Expected checksum of the data; note that it must
     *                    be a left-value.
     *  @param algo       Algorithm to use for checksum (not all algorithms
     *                    may be supported).  Must be a compile-time
     *                    constant.
     * ======================================================================================
     */
    {"*method.verify_checksum"_cs,
     {"condition"_cs, "data"_cs, "checksum"_cs, "algo"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto &state = externInfo.state;

         // We do not calculate the checksum for now and instead use a dummy.
         const auto *cond = externInfo.externArgs->at(0)->expression;

         const auto *oneBitType = IR::Type_Bits::get(1);
         const auto *checksumErr = new IR::Member(
             oneBitType, new IR::PathExpression("*standard_metadata"), "checksum_error");
         auto verifyChecksumLabel = externInfo.externObjectRef.path->toString() + "_" +
                                    externInfo.methodName + "_" +
                                    std::to_string(externInfo.originalCall.clone_id);
         const auto *hashCalc =
             ToolsVariables::getSymbolicVariable(oneBitType, verifyChecksumLabel);
         state.set(checksumErr,
                   new IR::Mux(oneBitType, cond, hashCalc, IR::Constant::get(oneBitType, 0)));
         return nullptr;
     }},
    /* ======================================================================================
     * verify_checksum_with_payload
     *  verify_checksum_with_payload is identical in all ways to
     *  verify_checksum, except that it includes the payload of the packet
     *  in the checksum calculation.  The payload is defined as "all bytes
     *  of the packet which were not parsed by the parser".
     *  Calling verify_checksum_with_payload is only supported in the
     *  VerifyChecksum control.
     * ======================================================================================
     */
    {"*method.verify_checksum_with_payload"_cs,
     {"condition"_cs, "data"_cs, "checksum"_cs, "algo"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto &state = externInfo.state;

         // We do not calculate the checksum for now and instead use a dummy.
         const auto *cond = externInfo.externArgs->at(0)->expression;

         const auto *oneBitType = IR::Type_Bits::get(1);
         const auto *checksumErr = new IR::Member(
             oneBitType, new IR::PathExpression("*standard_metadata"), "checksum_error");
         auto verifyChecksumLabel = externInfo.externObjectRef.path->toString() + "_" +
                                    externInfo.methodName + "_" +
                                    std::to_string(externInfo.originalCall.clone_id);
         const auto *hashCalc =
             ToolsVariables::getSymbolicVariable(oneBitType, verifyChecksumLabel);
         state.set(checksumErr,
                   new IR::Mux(oneBitType, cond, hashCalc, IR::Constant::get(oneBitType, 0)));
         return nullptr;
     }},
    /* ======================================================================================
     * update_checksum
     *  Computes the checksum of the supplied data and writes it to the
     *  checksum parameter.
     *  Calling update_checksum is only supported in the ComputeChecksum
     *  control.
     *  @param T          Must be a tuple type where all the tuple elements
     *                    are of type bit<W>, int<W>, or varbit<W>.  The
     *                    total length of the fields must be a multiple of
     *                    the output size.
     *  @param O          Output type; must be bit<X> type.
     *  @param condition  If 'false' the checksum parameter is not changed
     *  @param data       Data whose checksum is computed.
     *  @param checksum   Checksum of the data.
     *  @param algo       Algorithm to use for checksum (not all algorithms
     *                    may be supported).  Must be a compile-time
     *                    constant.
     * ======================================================================================
     */
    {"*method.update_checksum"_cs,
     {"condition"_cs, "data"_cs, "checksum"_cs, "algo"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto &state = externInfo.state;

         // We do not calculate the checksum
         // for now and instead use a dummy.
         const auto *cond = externInfo.externArgs->at(0)->expression;

         const auto &checksumVar =
             externInfo.externArgs->at(2)->expression->checkedTo<IR::InOutReference>()->ref;
         auto verifyChecksumLabel = externInfo.externObjectRef.path->toString() + "_" +
                                    externInfo.methodName + "_" +
                                    std::to_string(externInfo.originalCall.clone_id);
         const auto *hashCalc =
             ToolsVariables::getSymbolicVariable(checksumVar.type, verifyChecksumLabel);
         const auto *checksumCurrentVal = state.get(checksumVar);
         state.set(checksumVar, new IR::Mux(checksumVar.type, cond, hashCalc, checksumCurrentVal));
         return nullptr;
     }},
    /* ======================================================================================
     * update_checksum_with_payload
     *  update_checksum_with_payload is identical in all ways to
     *  update_checksum, except that it includes the payload of the packet
     *  in the checksum calculation.  The payload is defined as "all bytes
     *  of the packet which were not parsed by the parser".
     *  Calling update_checksum_with_payload is only supported in the
     *  ComputeChecksum control.
     * ======================================================================================
     */
    {"*method.update_checksum_with_payload"_cs,
     {"condition"_cs, "data"_cs, "checksum"_cs, "algo"_cs},
     [](const ExternMethodImpls::ExternInfo &externInfo) {
         auto &state = externInfo.state;

         // We do not calculate the checksum
         // for now and instead use a dummy.
         const auto *cond = externInfo.externArgs->at(0)->expression;

         const auto &checksumVar =
             externInfo.externArgs->at(2)->expression->checkedTo<IR::InOutReference>()->ref;
         auto verifyChecksumLabel = externInfo.externObjectRef.path->toString() + "_" +
                                    externInfo.methodName + "_" +
                                    std::to_string(externInfo.originalCall.clone_id);
         const auto *hashCalc =
             ToolsVariables::getSymbolicVariable(checksumVar.type, verifyChecksumLabel);
         const auto *checksumCurrentVal = state.get(checksumVar);
         state.set(checksumVar, new IR::Mux(checksumVar.type, cond, hashCalc, checksumCurrentVal));
         return nullptr;
     }},
});
}  // namespace Bmv2V1modelExterns

const IR::Expression *V1ModelExpressionResolver::processExtern(
    const ExternMethodImpls::ExternInfo &externInfo) {
    auto method = Bmv2V1modelExterns::EXTERN_METHOD_IMPLS.find(
        externInfo.externObjectRef, externInfo.methodName, externInfo.externArgs);
    if (method.has_value()) {
        return method.value()(externInfo);
    }
    return ExpressionResolver::processExtern(externInfo);
}

}  // namespace P4::P4Tools::Flay::V1Model
