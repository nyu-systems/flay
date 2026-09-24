#include "backends/p4tools/modules/flay/targets/tofino/target.h"

#include <cstddef>
#include <cstdlib>
#include <map>
#include <optional>
#include <vector>

#include "backends/p4tools/common/lib/util.h"
#include "backends/p4tools/modules/flay/core/control_plane/protobuf_utils.h"
#include "backends/p4tools/modules/flay/targets/tofino/control_plane_architecture.h"
#include "backends/p4tools/modules/flay/targets/tofino/symbolic_state.h"
#include "backends/p4tools/modules/flay/targets/tofino/tofino1/program_info.h"
#include "backends/p4tools/modules/flay/targets/tofino/tofino1/stepper.h"
#include "ir/ir.h"
#include "lib/cstring.h"
#include "lib/exceptions.h"
#include "lib/ordered_map.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wpedantic"
#include "p4/config/v1/p4info.pb.h"
#pragma GCC diagnostic pop

namespace P4::P4Tools::Flay::Tofino {

using namespace P4::literals;

/* =============================================================================================
 *  TofinoBaseFlayTarget implementation
 * ============================================================================================= */

TofinoBaseFlayTarget::TofinoBaseFlayTarget(const std::string &deviceName,
                                           const std::string &archName)
    : FlayTarget(deviceName, archName) {};

CompilerResultOrError TofinoBaseFlayTarget::runCompilerImpl(const CompilerOptions &options,
                                                            const IR::P4Program *program) const {
    program = runFrontend(options, program);
    if (program == nullptr) {
        return std::nullopt;
    }

    /// After the front end, get the P4Runtime API for the tna architecture.
    /// TODO: We need to implement the P4Runtime handler for Tofino.
    auto *serializer = P4::P4RuntimeSerializer::get();
    serializer->registerArch(cstring("tofino"),
                             new P4::ControlPlaneAPI::Standard::TofinoArchHandlerBuilder());

    std::optional<P4::P4RuntimeAPI> p4runtimeApi;
    auto p4UserInfo = FlayOptions::get().userP4Info();
    if (p4UserInfo.has_value()) {
        ASSIGN_OR_RETURN(
            auto p4Info,
            Protobuf::deserializeObjectFromFile<p4::config::v1::P4Info>(p4UserInfo.value()),
            std::nullopt);
        p4runtimeApi = P4::P4RuntimeAPI(new p4::config::v1::P4Info(p4Info), nullptr);
    } else {
        /// After the front end, get the P4Runtime API for the V1model architecture.
        p4runtimeApi =
            P4::P4RuntimeSerializer::get()->generateP4Runtime(program, cstring("tofino"));
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

    ASSIGN_OR_RETURN(
        auto initialControlPlaneState,
        TofinoControlPlaneInitializer().generateInitialControlPlaneConstraints(program),
        std::nullopt);

    return {*new FlayCompilerResult{CompilerResult(*program), *originalProgram,
                                    p4runtimeApi.value(), initialControlPlaneState}};
}

/* =============================================================================================
 *  Tofino1FlayTarget implementation
 * ============================================================================================= */

Tofino1FlayTarget::Tofino1FlayTarget() : TofinoBaseFlayTarget("tofino1", "tna") {}

void Tofino1FlayTarget::make() {
    static Tofino1FlayTarget *INSTANCE = nullptr;
    if (INSTANCE == nullptr) {
        INSTANCE = new Tofino1FlayTarget();
    }
}

const ProgramInfo *Tofino1FlayTarget::produceProgramInfoImpl(
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
              "%1%: The Tofno1 architecture requires 6 pipes. Received %2%.", mainDecl,
              flattenedBlocks.size());

    ordered_map<cstring, const IR::Type_Declaration *> programmableBlocks;
    std::map<int, int> declIdToGress;

    // Add to parserDeclIdToGress, mauDeclIdToGress, and deparserDeclIdToGress.
    for (size_t idx = 0; idx < flattenedBlocks.size(); ++idx) {
        const auto *declType = flattenedBlocks.at(idx);

        auto canonicalName = ARCH_SPEC.getArchMember(idx)->blockName;
        programmableBlocks.emplace(canonicalName, declType);
    }

    return new Tofino1ProgramInfo(*compilerResult.checkedTo<FlayCompilerResult>(),
                                  programmableBlocks);
}

const ArchSpec Tofino1FlayTarget::ARCH_SPEC = ArchSpec(
    "Pipeline"_cs,
    {
        // parser IngressParserT<H, M>(
        //     packet_in pkt,
        //     out H hdr,
        //     out M ig_md,
        //     @optional out ingress_intrinsic_metadata_t ig_intr_md,
        //     @optional out ingress_intrinsic_metadata_for_tm_t ig_intr_md_for_tm,
        //     @optional out ingress_intrinsic_metadata_from_parser_t ig_intr_md_from_prsr);
        {"IngressParserT"_cs,
         {
             nullptr,
             "*hdr"_cs,
             "*ig_md"_cs,
             "*ig_intr_md"_cs,
             "*ig_intr_md_for_tm"_cs,
             "*ig_intr_md_from_prsr"_cs,
         }},
        // control IngressT<H, M>(
        //     inout H hdr,
        //     inout M ig_md,
        //     @optional in ingress_intrinsic_metadata_t ig_intr_md,
        //     @optional in ingress_intrinsic_metadata_from_parser_t ig_intr_md_from_prsr,
        //     @optional inout ingress_intrinsic_metadata_for_deparser_t ig_intr_md_for_dprsr,
        //     @optional inout ingress_intrinsic_metadata_for_tm_t ig_intr_md_for_tm);
        {"IngressT"_cs,
         {
             "*hdr"_cs,
             "*ig_md"_cs,
             "*ig_intr_md"_cs,
             "*ig_intr_md_from_prsr"_cs,
             "*ig_intr_md_for_dprsr"_cs,
             "*ig_intr_md_for_tm"_cs,
         }},
        // control IngressDeparserT<H, M>(
        //     packet_out pkt,
        //     inout H hdr,
        //     in M metadata,
        //     @optional in ingress_intrinsic_metadata_for_deparser_t ig_intr_md_for_dprsr,
        //     @optional in ingress_intrinsic_metadata_t ig_intr_md);
        {"IngressDeparserT"_cs,
         {
             nullptr,
             "*hdr"_cs,
             "*ig_md"_cs,
             "*ig_intr_md_for_dprsr"_cs,
             "*ig_intr_md"_cs,
         }},
        // parser EgressParserT<H, M>(
        //     packet_in pkt,
        //     out H hdr,
        //     out M eg_md,
        //     @optional out egress_intrinsic_metadata_t eg_intr_md,
        //     @optional out egress_intrinsic_metadata_from_parser_t eg_intr_md_from_prsr,
        //     @optional out egress_intrinsic_metadata_for_deparser_t eg_intr_md_for_dprsr);
        {"EgressParserT"_cs,
         {
             nullptr,
             "*hdr"_cs,
             "*eg_md"_cs,
             "*eg_intr_md"_cs,
             "*eg_intr_md_from_prsr"_cs,
             "*eg_intr_md_for_dprsr"_cs,
         }},
        // control EgressT<H, M>(
        //     inout H hdr,
        //     inout M eg_md,
        //     @optional in egress_intrinsic_metadata_t eg_intr_md,
        //     @optional in egress_intrinsic_metadata_from_parser_t eg_intr_md_from_prsr,
        //     @optional inout egress_intrinsic_metadata_for_deparser_t eg_intr_md_for_dprsr,
        //     @optional inout egress_intrinsic_metadata_for_output_port_t eg_intr_md_for_oport);
        {"EgressT"_cs,
         {
             "*hdr"_cs,
             "*eg_md"_cs,
             "*eg_intr_md"_cs,
             "*eg_intr_md_from_prsr"_cs,
             "*eg_intr_md_for_dprsr"_cs,
             "*eg_intr_md_for_oport"_cs,
         }},
        // control EgressDeparserT<H, M>(
        //     packet_out pkt,
        //     inout H hdr,
        //     in M metadata,
        //     @optional in egress_intrinsic_metadata_for_deparser_t eg_intr_md_for_dprsr,
        //     @optional in egress_intrinsic_metadata_t eg_intr_md,
        //     @optional in egress_intrinsic_metadata_from_parser_t eg_intr_md_from_prsr);
        {"EgressDeparserT"_cs,
         {
             nullptr,
             "*hdr"_cs,
             "*eg_md"_cs,
             "*eg_intr_md_for_dprsr"_cs,
             "*eg_intr_md"_cs,
             "*eg_intr_md_from_prsr"_cs,
         }},
    });

const ArchSpec *Tofino1FlayTarget::getArchSpecImpl() const { return &ARCH_SPEC; }

FlayStepper &Tofino1FlayTarget::getStepperImpl(const ProgramInfo &programInfo,
                                               ControlPlaneConstraints &constraints,
                                               ExecutionState &executionState) const {
    return *new Tofino1FlayStepper(*programInfo.checkedTo<Tofino1ProgramInfo>(), constraints,
                                   executionState);
}

}  // namespace P4::P4Tools::Flay::Tofino
