# XFAILS: tests that currently fail. Most of these are temporary.
# ================================================

# Wildcard failing tests for now. TODO:  Classify.
p4tools_add_xfail_reason(
  "flay-bmv2-v1model"
  "Compiler Bug|Unimplemented compiler support"
  issue1304.p4 # Cast failed: Pipeline<my_packet, my_metadata> with type Type_Specialized is not a
               # Type_Declaration.
  header-stack-ops-bmv2.p4 # Unknown method member expression: hdr_0.h2; of type header h2_t
  issue4739.p4 # type ForStatement not implemented in the core stepper
  loop-3-clause-tricky2.p4 # type ForStatement not implemented in the core stepper
)

p4tools_add_xfail_reason(
  "flay-bmv2-v1model"
  "Unsupported assignment"
)

p4tools_add_xfail_reason(
  "flay-bmv2-v1model"
  "Unknown or unimplemented extern method: .*"
)

# These are custom externs we do not implement.
p4tools_add_xfail_reason(
  "flay-bmv2-v1model"
  "Unknown or unimplemented extern method: .*"
  issue1882-bmv2.p4 # Unknown or unimplemented extern method: extr.increment
  issue1882-1-bmv2.p4 # Unknown or unimplemented extern method: extr.increment
  issue2664-bmv2.p4 # Unknown or unimplemented extern method: ipv4_checksum.update
  issue1193-bmv2.p4 # Unknown or unimplemented extern method: c.count
)

p4tools_add_xfail_reason(
  "flay-bmv2-v1model"
  "Unable to find var .* in the symbolic environment"
  union-valid-bmv2.p4 # Unable to find var h.u.*valid; in the symbolic environment.
  issue3091.p4 # WONTFIX Unable to find var ternary; in the symbolic environment.
)

p4tools_add_xfail_reason(
  "flay-bmv2-v1model"
  "expected a header or header union stack"
  issue4057.p4
)

p4tools_add_xfail_reason(
  "flay-bmv2-v1model" "Only constants are supported"
  parser-unroll-test10.p4 # Value meta.hs_next_index; is not a constant. Only constants are
                          # supported as part of a state variable.
                          # hdr.hs[meta.hs_next_index].setValid();
)

p4tools_add_xfail_reason(
  "flay-bmv2-v1model"
  "Unsupported type argument for Value Set"
  pvs-nested-struct.p4
)

p4tools_add_xfail_reason(
  "flay-bmv2-v1model"
  "Parser state .* was already visited. We currently do not support parser loops."
  issue2314.p4
  invalid-hdr-warnings1.p4
  parser-unroll-test9.p4
  parser-unroll-issue3537.p4
  parser-unroll-issue3537-1.p4
  issue281.p4
  fabric.p4
)

# When trying to remove dead code we can not find a particular node in the reachability map.
# Often this happens because the compiler optimizes the expression away and Flay never sees it.
# TODO: For now, we use a warning, but maybe this should be an error?
p4tools_add_xfail_reason(
  "flay-bmv2-v1model"
  "error: Unable to find node .* in the reachability map"
  # issue2345-multiple_dependencies.p4
  # issue2345-with_nested_if.p4
  # issue420.p4
  # predication_issue_3.p4
  # dash-pipeline-v1model-bmv2.p4
)

# We are trying to map a duplicate condition to the reachability map.
# This can happen when the source information is ambiguous.
# Unclear how to resolve this issue as it emerges from unclean compiler passes.
p4tools_add_xfail_reason(
  "flay-bmv2-v1model"
  "Reachability mapping for node .* already exists"
)

p4tools_add_xfail_reason(
  "flay-bmv2-v1model"
  "Failed BMv2 compilation."
  # This test uses a feature currently unsupported in the BMv2 back-end.
  issue907-bmv2.p4
  # These tests use a table key with type 'error'
  issue1062-bmv2.p4
  # This reads stack.next
  issue692-bmv2.p4
  # These test use computations in the verify/update checksum controls - unsupported
  issue1765-1-bmv2.p4
  # This test tries to hash (via the hash extern) a value that isn't a tuple - unsupported
  hashing-non-tuple-bmv2.p4
  # Similarly, this test hashes on a bit input.
  issue584-1-bmv2.p4
)
