#include "backends/p4tools/modules/flay/targets/nikss/target.h"

#include <cstddef>
#include <cstdlib>
#include <optional>
#include <vector>

#include "backends/p4tools/common/lib/util.h"
#include "backends/p4tools/modules/flay/core/control_plane/protobuf_utils.h"
#include "backends/p4tools/modules/flay/targets/nikss/psa/program_info.h"
#include "backends/p4tools/modules/flay/targets/nikss/psa/stepper.h"
#include "backends/p4tools/modules/flay/targets/nikss/symbolic_state.h"
#include "ir/ir.h"
#include "lib/cstring.h"
#include "lib/exceptions.h"
#include "lib/ordered_map.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wpedantic"
#include "p4/config/v1/p4info.pb.h"
#pragma GCC diagnostic pop

namespace P4::P4Tools::Flay::Nikss {

using namespace ::P4::literals;

/* =============================================================================================
 *  NikssBaseFlayTarget implementation
 * ============================================================================================= */

NikssBaseFlayTarget::NikssBaseFlayTarget(const std::string &deviceName, const std::string &archName)
    : FlayTarget(deviceName, archName) {};

CompilerResultOrError NikssBaseFlayTarget::runCompilerImpl(const CompilerOptions &options,
                                                           const IR::P4Program *program) const {
    program = runFrontend(options, program);
    if (program == nullptr) {
        return std::nullopt;
    }
    /// After the front end, get the P4Runtime API for the pna architecture.
    /// TODO: We need to implement the P4Runtime handler for Nikss.
    std::optional<P4::P4RuntimeAPI> p4runtimeApi;
    auto p4UserInfo = FlayOptions::get().userP4Info();
    if (p4UserInfo.has_value()) {
        ASSIGN_OR_RETURN(
            auto p4Info,
            Protobuf::deserializeObjectFromFile<p4::config::v1::P4Info>(p4UserInfo.value()),
            std::nullopt);
        p4runtimeApi = P4::P4RuntimeAPI(p4Info.New(), nullptr);
    } else {
        /// After the front end, get the P4Runtime API for the V1model architecture.
        p4runtimeApi = P4::P4RuntimeSerializer::get()->generateP4Runtime(program, "pna"_cs);
        if (errorCount() > 0) {
            return std::nullopt;
        }
    }

    program = runMidEnd(options, program);
    if (program == nullptr) {
        return std::nullopt;
    }
    // Copy the program after the (safe) mid end.
    auto *originalProgram = program->clone();

    P4::ReferenceMap refMap;
    P4::TypeMap typeMap;
    program = program->apply(mkPrivateMidEnd(options, &refMap, &typeMap));
    ASSIGN_OR_RETURN(auto initialControlPlaneState,
                     NikssControlPlaneInitializer().generateInitialControlPlaneConstraints(program),
                     std::nullopt);

    return {*new FlayCompilerResult{CompilerResult(*program), *originalProgram,
                                    p4runtimeApi.value(), initialControlPlaneState}};
}

/* =============================================================================================
 *  PsaFlayTarget implementation
 * ============================================================================================= */

PsaFlayTarget::PsaFlayTarget() : NikssBaseFlayTarget("nikss", "psa") {}

void PsaFlayTarget::make() {
    static PsaFlayTarget *INSTANCE = nullptr;
    if (INSTANCE == nullptr) {
        INSTANCE = new PsaFlayTarget();
    }
}

const ProgramInfo *PsaFlayTarget::produceProgramInfoImpl(
    const CompilerResult &compilerResult, const IR::Declaration_Instance *mainDecl) const {
    // The blocks in the main declaration are just the arguments in the constructor call.
    // Convert mainDecl->arguments into a vector of blocks, represented as constructor-call
    // expressions.
    std::vector<const IR::Type_Declaration *> flattenedBlocks;

    for (const auto *arg : *mainDecl->arguments) {
        if (const auto *pathExpr = arg->expression->to<IR::PathExpression>()) {
            // Look up the path expression in the top-level namespace and expect to find a
            // declaration instance.
            const auto *declInstance = findProgramDecl(&compilerResult.getProgram(), pathExpr->path)
                                           ->checkedTo<IR::Declaration_Instance>();
            // Convert declInstance->arguments into a vector of blocks.
            auto blocks =
                argumentsToTypeDeclarations(&compilerResult.getProgram(), declInstance->arguments);
            flattenedBlocks.insert(flattenedBlocks.end(), blocks.begin(), blocks.end());
        }
    }

    // We should have six arguments.
    BUG_CHECK(flattenedBlocks.size() == 6,
              "%1%: The PSA architecture requires 6 pipes. Received %2%.", mainDecl,
              flattenedBlocks.size());

    ordered_map<cstring, const IR::Type_Declaration *> programmableBlocks;
    std::map<int, int> declIdToGress;

    // Add to parserDeclIdToGress, mauDeclIdToGress, and deparserDeclIdToGress.
    for (size_t idx = 0; idx < flattenedBlocks.size(); ++idx) {
        const auto *declType = flattenedBlocks.at(idx);

        auto canonicalName = ARCH_SPEC.getArchMember(idx)->blockName;
        programmableBlocks.emplace(canonicalName, declType);
    }
    return new PsaProgramInfo(*compilerResult.checkedTo<FlayCompilerResult>(), programmableBlocks);
}

const ArchSpec PsaFlayTarget::ARCH_SPEC =
    ArchSpec("Pipeline"_cs, {
                                // parser IngressParser<H, M, RESUBM, RECIRCM>(
                                //     packet_in buffer,
                                //     out H parsed_hdr,
                                //     inout M user_meta,
                                //     in psa_ingress_parser_input_metadata_t istd,
                                //     in RESUBM resubmit_meta,
                                //     in RECIRCM recirculate_meta);
                                {"IngressParser"_cs,
                                 {
                                     nullptr,
                                     "*hdr"_cs,
                                     "*meta"_cs,
                                     "*ingress_istd"_cs,
                                     "*resubmit_meta"_cs,
                                     "*recirculate_meta"_cs,
                                 }},
                                // control Ingress<H, M>(
                                //     inout H hdr, inout M user_meta,
                                //     in    psa_ingress_input_metadata_t  istd,
                                //     inout psa_ingress_output_metadata_t ostd);
                                {"Ingress"_cs,
                                 {
                                     "*hdr"_cs,
                                     "*meta"_cs,
                                     "*ingres_istd"_cs,
                                     "*ingres_ostd"_cs,
                                 }},
                                // control IngressDeparser<H, M, CI2EM, RESUBM, NM>(
                                //     packet_out buffer,
                                //     out CI2EM clone_i2e_meta,
                                //     out RESUBM resubmit_meta,
                                //     out NM normal_meta,
                                //     inout H hdr,
                                //     in M meta,
                                //     in psa_ingress_output_metadata_t istd);
                                {"IngressDeparser"_cs,
                                 {
                                     nullptr,
                                     "*clone_i2e_meta"_cs,
                                     "*resubmit_meta"_cs,
                                     "*normal_meta"_cs,
                                     "*hdr"_cs,
                                     "*meta"_cs,
                                     "*ingres_ostd"_cs,
                                 }},
                                // parser EgressParser<H, M, NM, CI2EM, CE2EM>(
                                //     packet_in buffer,
                                //     out H parsed_hdr,
                                //     inout M user_meta,
                                //     in psa_egress_parser_input_metadata_t istd,
                                //     in NM normal_meta,
                                //     in CI2EM clone_i2e_meta,
                                //     in CE2EM clone_e2e_meta);
                                {"EgressParser"_cs,
                                 {
                                     nullptr,
                                     "*hdr"_cs,
                                     "*meta"_cs,
                                     "*egress_pistd"_cs,
                                     "*normal_meta"_cs,
                                     "*clone_i2e_meta"_cs,
                                     "*clone_e2e_meta"_cs,
                                 }},
                                // control Egress<H, M>(
                                //     inout H hdr,
                                //     inout M user_meta,
                                //     in    psa_egress_input_metadata_t  istd,
                                //     inout psa_egress_output_metadata_t ostd);
                                {"Egress"_cs,
                                 {
                                     "*hdr"_cs,
                                     "*meta"_cs,
                                     "*egress_istd"_cs,
                                     "*egress_ostd"_cs,
                                 }},
                                // control EgressDeparser<H, M, CE2EM, RECIRCM>(
                                //     packet_out buffer,
                                //     out CE2EM clone_e2e_meta,
                                //     out RECIRCM recirculate_meta,
                                //     inout H hdr,
                                //     in M meta,
                                //     in psa_egress_output_metadata_t istd,
                                //     in psa_egress_deparser_input_metadata_t edstd);
                                {
                                    "EgressDeparser"_cs,
                                    {
                                        nullptr,
                                        "*clone_e2e_meta"_cs,
                                        "*recirculate_meta"_cs,
                                        "*hdr"_cs,
                                        "*meta"_cs,
                                        "*egress_ostd"_cs,
                                        "*egress_dstd"_cs,
                                    },
                                },
                            });

const ArchSpec *PsaFlayTarget::getArchSpecImpl() const { return &ARCH_SPEC; }

FlayStepper &PsaFlayTarget::getStepperImpl(const ProgramInfo &programInfo,
                                           ControlPlaneConstraints &constraints,
                                           ExecutionState &executionState) const {
    return *new PsaFlayStepper(*programInfo.checkedTo<PsaProgramInfo>(), constraints,
                               executionState);
}

}  // namespace P4::P4Tools::Flay::Nikss
