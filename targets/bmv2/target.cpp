#include "backends/p4tools/modules/flay/targets/bmv2/target.h"

#include <cstddef>
#include <map>
#include <vector>

#include "backends/p4tools/modules/flay/control_plane/id_to_ir_map.h"
#include "backends/p4tools/modules/flay/control_plane/protobuf/protobuf.h"
#include "backends/p4tools/modules/flay/lib/logging.h"
#include "backends/p4tools/modules/flay/targets/bmv2/program_info.h"
#include "backends/p4tools/modules/flay/targets/bmv2/stepper.h"
#include "ir/ir.h"
#include "lib/cstring.h"
#include "lib/exceptions.h"
#include "lib/ordered_map.h"

namespace P4Tools::Flay::V1Model {

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

const ProgramInfo *V1ModelFlayTarget::initProgramImpl(
    const IR::P4Program *program, const IR::Declaration_Instance *mainDecl) const {
    // The blocks in the main declaration are just the arguments in the constructor call.
    // Convert mainDecl->arguments into a vector of blocks, represented as constructor-call
    // expressions.
    std::vector<const IR::Type_Declaration *> blocks;
    argumentsToTypeDeclarations(program, mainDecl->arguments, blocks);

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

    return new V1ModelProgramInfo(program, programmableBlocks);
}

const ArchSpec V1ModelFlayTarget::ARCH_SPEC =
    ArchSpec("V1Switch", {// parser Parser<H, M>(packet_in b,
                          //                     out H parsedHdr,
                          //                     inout M meta,
                          //                     inout standard_metadata_t standard_metadata);
                          {"Parser", {nullptr, "*hdr", "*meta", "*standard_metadata"}},
                          // control VerifyChecksum<H, M>(inout H hdr,
                          //                              inout M meta);
                          {"VerifyChecksum", {"*hdr", "*meta"}},
                          // control Ingress<H, M>(inout H hdr,
                          //                       inout M meta,
                          //                       inout standard_metadata_t standard_metadata);
                          {"Ingress", {"*hdr", "*meta", "*standard_metadata"}},
                          // control Egress<H, M>(inout H hdr,
                          //            inout M meta,
                          //            inout standard_metadata_t standard_metadata);
                          {"Egress", {"*hdr", "*meta", "*standard_metadata"}},
                          // control ComputeChecksum<H, M>(inout H hdr,
                          //                       inout M meta);
                          {"ComputeChecksum", {"*hdr", "*meta"}},
                          // control Deparser<H>(packet_out b, in H hdr);
                          {"Deparser", {nullptr, "*hdr"}}});

const ArchSpec *V1ModelFlayTarget::getArchSpecImpl() const { return &ARCH_SPEC; }

FlayStepper &V1ModelFlayTarget::getStepperImpl(const ProgramInfo &programInfo,
                                               ExecutionState &executionState,
                                               ControlPlaneState &controlPlaneState) const {
    auto *bmv2ControlPlaneState = controlPlaneState.to<Bmv2ControlPlaneState>();
    CHECK_NULL(bmv2ControlPlaneState);
    return *new V1ModelFlayStepper(*programInfo.checkedTo<V1ModelProgramInfo>(), executionState,
                                   *bmv2ControlPlaneState);
}

Bmv2ControlPlaneState &V1ModelFlayTarget::initializeControlPlaneStateImpl() const {
    return *new Bmv2ControlPlaneState();
}

std::optional<ControlPlaneConstraints> V1ModelFlayTarget::computeControlPlaneConstraintsImpl(
    const IR::P4Program &program, const FlayOptions &options,
    const ControlPlaneState &controlPlaneState) const {
    // Initialize some constraints that are active regardless of the control-plane configuration.
    // These constraints can be overridden by the respective control-plane configuration.
    auto constraints = controlPlaneState.getDefaultConstraints();
    if (!options.hasControlPlaneConfig()) {
        return constraints;
    }
    auto confPath = options.getControlPlaneConfig();
    printInfo("Parsing initial control plane configuration...\n");
    if (confPath.extension() == ".txtpb") {
        MapP4RuntimeIdtoIr idMapper;
        program.apply(idMapper);
        if (::errorCount() > 0) {
            return std::nullopt;
        }
        auto idToIrMap = idMapper.getP4RuntimeIdtoIrNodeMap();
        auto deserializedConfig = ProtobufDeserializer::deserializeProtobufConfig(confPath);
        auto protoConstraints =
            ProtobufDeserializer::convertToControlPlaneConstraints(deserializedConfig, idToIrMap);
        constraints.insert(protoConstraints.begin(), protoConstraints.end());
        return constraints;
    }

    ::error("Control plane file format %1% not supported for this target.",
            confPath.extension().c_str());
    return std::nullopt;
}

}  // namespace P4Tools::Flay::V1Model