#ifndef BACKENDS_P4TOOLS_MODULES_FLAY_TARGETS_BMV2_TARGET_H_
#define BACKENDS_P4TOOLS_MODULES_FLAY_TARGETS_BMV2_TARGET_H_

#include "backends/p4tools/common/lib/arch_spec.h"
#include "backends/p4tools/modules/flay/core/execution_state.h"
#include "backends/p4tools/modules/flay/core/program_info.h"
#include "backends/p4tools/modules/flay/core/stepper.h"
#include "backends/p4tools/modules/flay/core/target.h"
#include "ir/ir.h"

namespace P4Tools::Flay::V1Model {

class V1ModelFlayTarget : public FlayTarget {
 private:
    V1ModelFlayTarget();

    static const ArchSpec ARCH_SPEC;

 public:
    /// Registers this target.
    static void make();

 protected:
    const ProgramInfo *produceProgramInfoImpl(const CompilerResult &compilerResult,
                                              const IR::Declaration_Instance *mainDecl) const final;

    [[nodiscard]] std::optional<ControlPlaneConstraints> computeControlPlaneConstraintsImpl(
        const FlayCompilerResult &compilerResult, const FlayOptions &options) const override;

    [[nodiscard]] const ArchSpec *getArchSpecImpl() const override;

    [[nodiscard]] FlayStepper &getStepperImpl(const ProgramInfo &programInfo,
                                              ExecutionState &executionState) const override;

 private:
    CompilerResultOrError runCompilerImpl(const IR::P4Program *program) const final;
};

}  // namespace P4Tools::Flay::V1Model

#endif /* BACKENDS_P4TOOLS_MODULES_FLAY_TARGETS_BMV2_TARGET_H_ */
