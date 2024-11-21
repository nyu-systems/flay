#include "backends/p4tools/modules/flay/targets/bmv2/target.h"

#include <cstddef>
#include <cstdlib>
#include <map>
#include <optional>
#include <vector>

#include "backends/p4tools/common/lib/util.h"
#include "backends/p4tools/modules/flay/core/control_plane/protobuf_utils.h"
#include "backends/p4tools/modules/flay/targets/bmv2/program_info.h"
#include "backends/p4tools/modules/flay/targets/bmv2/stepper.h"
#include "control-plane/p4RuntimeSerializer.h"
#include "frontends/common/options.h"
#include "frontends/common/resolveReferences/referenceMap.h"
#include "ir/ir.h"
#include "lib/cstring.h"
#include "lib/exceptions.h"
#include "lib/ordered_map.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wpedantic"
#include "p4/config/v1/p4info.pb.h"
#pragma GCC diagnostic pop

namespace P4::P4Tools::Flay::V1Model {

using namespace ::P4::literals;

/* =============================================================================================
 *  V1ModelFlayTarget implementation
 * ============================================================================================= */

V1ModelFlayTarget::V1ModelFlayTarget() : FlayTarget("bmv2", "v1model") {}

void V1ModelFlayTarget::make() {
    static V1ModelFlayTarget *INSTANCE = nullptr;
    if (INSTANCE == nullptr) {
        INSTANCE = new V1ModelFlayTarget();
    }
}

const ProgramInfo *V1ModelFlayTarget::produceProgramInfoImpl(
    const CompilerResult &compilerResult, const IR::Declaration_Instance *mainDecl) const {
    // The blocks in the main declaration are just the arguments in the constructor call.
    // Convert mainDecl->arguments into a vector of blocks, represented as constructor-call
    // expressions.
    const auto blocks =
        argumentsToTypeDeclarations(&compilerResult.getProgram(), mainDecl->arguments);

    // We should have six arguments.
    BUG_CHECK(blocks.size() == 6, "%1%: The BMV2 architecture requires 6 pipes. Received %2%.",
              mainDecl, blocks.size());

    ordered_map<cstring, const IR::Type_Declaration *> programmableBlocks;
    std::map<int, int> declIdToGress;

    // Add to parserDeclIdToGress, mauDeclIdToGress, and deparserDeclIdToGress.
    for (size_t idx = 0; idx < blocks.size(); ++idx) {
        const auto *declType = blocks.at(idx);

        auto canonicalName = ARCH_SPEC.getArchMember(idx)->blockName;
        programmableBlocks.emplace(canonicalName, declType);
    }

    return new Bmv2V1ModelProgramInfo(*compilerResult.checkedTo<FlayCompilerResult>(),
                                      programmableBlocks);
}

const ArchSpec V1ModelFlayTarget::ARCH_SPEC = ArchSpec(
    "V1Switch"_cs, {
                       // parser Parser<H, M>(packet_in b,
                       //                     out H parsedHdr,
                       //                     inout M meta,
                       //                     inout standard_metadata_t standard_metadata);
                       {"Parser"_cs,
                        {
                            nullptr,
                            "*hdr"_cs,
                            "*meta"_cs,
                            "*standard_metadata"_cs,
                        }},
                       // control VerifyChecksum<H, M>(inout H hdr,
                       //                              inout M meta);
                       {"VerifyChecksum"_cs,
                        {
                            "*hdr"_cs,
                            "*meta"_cs,
                        }},
                       // control Ingress<H, M>(inout H hdr,
                       //                       inout M meta,
                       //                       inout standard_metadata_t standard_metadata);
                       {"Ingress"_cs,
                        {
                            "*hdr"_cs,
                            "*meta"_cs,
                            "*standard_metadata"_cs,
                        }},
                       // control Egress<H, M>(inout H hdr,
                       //            inout M meta,
                       //            inout standard_metadata_t standard_metadata);
                       {"Egress"_cs,
                        {
                            "*hdr"_cs,
                            "*meta"_cs,
                            "*standard_metadata"_cs,
                        }},
                       // control ComputeChecksum<H, M>(inout H hdr,
                       //                       inout M meta);
                       {"ComputeChecksum"_cs,
                        {
                            "*hdr"_cs,
                            "*meta"_cs,
                        }},
                       // control Deparser<H>(packet_out b, in H hdr);
                       {"Deparser"_cs,
                        {
                            nullptr,
                            "*hdr"_cs,
                        }},
                   });

const ArchSpec *V1ModelFlayTarget::getArchSpecImpl() const { return &ARCH_SPEC; }

FlayStepper &V1ModelFlayTarget::getStepperImpl(const ProgramInfo &programInfo,
                                               ControlPlaneConstraints &constraints,
                                               ExecutionState &executionState) const {
    return *new V1ModelFlayStepper(*programInfo.checkedTo<Bmv2V1ModelProgramInfo>(), constraints,
                                   executionState);
}

CompilerResultOrError V1ModelFlayTarget::runCompilerImpl(const CompilerOptions &options,
                                                         const IR::P4Program *program) const {
    program = runFrontend(options, program);
    if (program == nullptr) {
        return std::nullopt;
    }

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
        p4runtimeApi = P4::P4RuntimeSerializer::get()->generateP4Runtime(program, "v1model"_cs);
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
                     Bmv2ControlPlaneInitializer().generateInitialControlPlaneConstraints(program),
                     std::nullopt);

    return {*new FlayCompilerResult{CompilerResult(*program), *originalProgram,
                                    p4runtimeApi.value(), initialControlPlaneState}};
}

}  // namespace P4::P4Tools::Flay::V1Model
