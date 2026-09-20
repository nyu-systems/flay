# Flay
Flay is a control-plane dead code elimination tool for P4 programs. Flay takes a P4 program and control-plane configuration and removes all the code that can not be executed with this particular configuration. Flay also removes code that is dead across programmable blocks. For example, code that is only executed if a header is valid, but that header is never set valid in the parser block.

## Building Flay
Flay is a  [P4Tools](https://github.com/p4lang/p4c/tree/main/backends/p4tools) module, which itself is part of [P4C](https://github.com/p4lang/p4c). To be able to use Flay, you first need to build P4C. Instructions can be found [here](https://github.com/p4lang/p4c#installing-p4c-from-source).

After building P4C, you need link Flay as a P4Tools module. The [CI scripts](https://github.com/fruffy/flay/blob/master/.github/workflows/ci-build.yml) describe a similar workflow.
You will need to symlink Flay into the modules directory, rerun CMake in your P4C build directory, then rebuild.
```
ln -sf flay p4c/backends/p4tools/modules/
cd p4c/build
cmake ..
make
```

## Parser analysis

Flay computes data-plane expressions using data-flow analysis with state merging.
These expressions describe the values and reachability conditions at program
points of interest. Data-plane variables remain symbolic, while control-plane
variables serve as placeholders for configuration assignments. Each annotation
captures the paths reaching its program point, allowing specialization queries
to be evaluated independently at that point.

For parsers, Flay first unrolls loops during compilation and then processes the
reachable states in topological order. Before evaluating a state, it combines the
symbolic states from all incoming transitions. Conditional expressions preserve
path-dependent values, and the disjunction of incoming path conditions determines
reachability. This allows a shared destination to be evaluated once without
enumerating every path through the parser. Select transitions preserve first-match
priority, with unmatched inputs taking the default transition or rejecting the
packet. Accept and reject outcomes are combined under the parser's entry
condition. Cycles remaining after unrolling are unsupported.

The resulting expressions are computed once and reused across control-plane
updates. Flay maps each control-plane symbol to the program points it can affect.
An update identifies these points, substitutes the current control-plane
assignments into their expressions, and checks whether specialization must change.
Symbols are matched by structural identity, independently of their IR allocation,
so an update can refer to a symbol already present in the analyzed program.

Flay precomputes Z3 representations to reduce the work required for subsequent
updates. During translation, a traversal-local cache reuses translations of shared
IR nodes. Its pointer keys avoid duplicate translation work; they do not determine
how control-plane symbols match during update processing.

State merging avoids repeated execution of shared parser states, but the resulting
expressions can still be large and costly to simplify. Initial analysis cost must
therefore be measured separately from update-processing cost. The `--skip-parsers`
option omits parser analysis; the paper's initial-analysis measurements in Table 2
use this option.
