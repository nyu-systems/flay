#include "backends/p4tools/modules/flay/flay.h"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "backends/p4tools/common/compiler/compiler_target.h"
#include "backends/p4tools/common/lib/logging.h"
#include "backends/p4tools/modules/flay/core/interpreter/partial_evaluator.h"
#include "backends/p4tools/modules/flay/core/interpreter/target.h"
#include "backends/p4tools/modules/flay/core/lib/return_macros.h"
#include "backends/p4tools/modules/flay/core/specialization/service_wrapper_bfruntime.h"
#include "backends/p4tools/modules/flay/core/specialization/service_wrapper_p4runtime.h"
#include "backends/p4tools/modules/flay/register.h"
#include "backends/p4tools/modules/flay/toolname.h"
#include "control-plane/p4RuntimeSerializer.h"
#include "control-plane/p4RuntimeTypes.h"

#ifdef FLAY_WITH_GRPC
#include "backends/p4tools/modules/flay/service/flay_grpc_service.h"
#endif
#include "lib/error.h"
#include "lib/nullstream.h"

namespace P4::P4Tools::Flay {

void Flay::registerTarget() {
    // Register all available compiler targets.
    // These are discovered by CMAKE, which fills out the register.h.in file.
    registerFlayTargets();
}

#ifdef FLAY_WITH_GRPC
int runServer(const FlayOptions &flayOptions, const FlayCompilerResult &flayCompilerResult,
              const ExecutionState &executionState, const ControlPlaneConstraints &constraints) {
    FlayServiceOptions serviceOptions;

    // Initialize the flay service, which includes a dead code eliminator.
    FlayService service(serviceOptions, flayCompilerResult, executionState.nodeAnnotationMap(),
                        constraints);
    if (errorCount() > 0) {
        error("Encountered errors trying to starting the service.");
        return EXIT_FAILURE;
    }
    printInfo("Starting flay server...");
    service.startServer(flayOptions.getServerAddress());
    return errorCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
#endif

std::optional<FlayServiceStatisticsMap> runServiceWrapper(
    const FlayOptions &flayOptions, const FlayCompilerResult &compilerResult,
    IncrementalAnalysisMap incrementalAnalysisMap) {
    auto controlPlaneApi = flayOptions.controlPlaneApi();
    // TODO: Make this target-specific?
    FlayServiceWrapper *serviceWrapper = nullptr;
    if (controlPlaneApi == "P4RUNTIME") {
        serviceWrapper =
            new P4RuntimeFlayServiceWrapper(compilerResult, std::move(incrementalAnalysisMap));
    } else if (controlPlaneApi == "BFRUNTIME") {
        serviceWrapper =
            new BfRuntimeFlayServiceWrapper(compilerResult, std::move(incrementalAnalysisMap));
    } else {
        error("Unsupported control plane API %1%.", controlPlaneApi.data());
        return std::nullopt;
    }
    if (flayOptions.hasConfigurationUpdatePattern()) {
        RETURN_IF_FALSE(serviceWrapper->parseControlUpdatesFromPattern(
                            flayOptions.configurationUpdatePattern()) == EXIT_SUCCESS,
                        std::nullopt);
    }
    RETURN_IF_FALSE(serviceWrapper->run() == EXIT_SUCCESS, std::nullopt);
    if (FlayOptions::get().optimizedOutputDir() != std::nullopt) {
        serviceWrapper->outputOptimizedProgram("optimized.final.p4");
    }
    return serviceWrapper->computeFlayServiceStatistics();
}

int Flay::mainImpl(const CompilerResult &compilerResult) {
    // Register all available flay targets.
    // These are discovered by CMAKE, which fills out the register.h.in file.
    registerFlayTargets();

    // Make sure the input result corresponds to the result we expect.
    ASSIGN_OR_RETURN_WITH_MESSAGE(const auto &flayCompilerResult,
                                  compilerResult.to<FlayCompilerResult>(), EXIT_FAILURE,
                                  error("Expected a FlayCompilerResult."));

    const auto *programInfo = FlayTarget::produceProgramInfo(compilerResult);
    if (programInfo == nullptr) {
        error("Program not supported by target device and architecture.");
        return EXIT_FAILURE;
    }
    if (errorCount() > 0) {
        error("Flay: Encountered errors during preprocessing. Exiting");
        return EXIT_FAILURE;
    }
    const auto &flayOptions = FlayOptions::get();

    // If we write to the optimized program to file, also dump the program after the midend.
    auto optimizedOutputDir = flayOptions.optimizedOutputDir();
    if (optimizedOutputDir != std::nullopt) {
        if (!std::filesystem::exists(optimizedOutputDir.value())) {
            std::filesystem::create_directory(optimizedOutputDir.value());
        }
        auto midendOutputFile = optimizedOutputDir.value() / "midend.p4";
        std::ofstream output(midendOutputFile);
        if (!output.is_open()) {
            error("Could not open file %1% for writing.", midendOutputFile.c_str());
            return EXIT_FAILURE;
        }
        P4::ToP4 toP4(&output, false);
        flayCompilerResult.getOriginalProgram().apply(toP4);
        output.close();
        printInfo("Wrote midend program to %1%", midendOutputFile);
    }

    if (flayOptions.p4InfoFilePath().has_value()) {
        auto outputFile = openFile(flayOptions.p4InfoFilePath().value(), true);
        if (outputFile == nullptr) {
            return EXIT_FAILURE;
        }
        flayCompilerResult.getP4RuntimeApi().serializeP4InfoTo(outputFile.get(),
                                                               P4::P4RuntimeFormat::TEXT_PROTOBUF);
    }

    // printInfo("Running analysis...");
    // SymbolicExecutor symbolicExecutor;
    // ASSIGN_OR_RETURN(const auto &analysisResult,
    //                  symbolicExecutor.run(flayOptions, flayCompilerResult, *programInfo),
    //                  EXIT_FAILURE);

#ifdef FLAY_WITH_GRPC
    printInfo("Starting the service...");
    // If server mode is active, start the server and exit once it has finished.
    if (flayOptions.serverModeActive()) {
        if (flayOptions.controlPlaneApi() != "P4RUNTIME") {
            error("Server mode requires P4RUNTIME as --control-plane option.");
        }
        return runServer(flayOptions, flayCompilerResult, symbolicExecutor.getExecutionState(),
                         constraints);
    }
#endif
    PartialEvaluationOptions partialEvaluationOptions;
    IncrementalAnalysisMap incrementalAnalysisMap;
    auto [result, inserted] = incrementalAnalysisMap.emplace(
        "partialEvaluation",
        std::make_unique<PartialEvaluation>(flayOptions, flayCompilerResult, *programInfo,
                                            partialEvaluationOptions));
    if (result->second->initialize() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    RETURN_IF_FALSE(
        runServiceWrapper(flayOptions, flayCompilerResult, std::move(incrementalAnalysisMap)),
        EXIT_FAILURE);
    return errorCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

std::optional<FlayServiceStatisticsMap> optimizeProgramImpl(
    std::optional<std::reference_wrapper<const std::string>> program,
    const FlayOptions &flayOptions) {
    // Register supported Flay targets.
    registerFlayTargets();

    P4Tools::Target::init(flayOptions.target.c_str(), flayOptions.arch.c_str());

    CompilerResultOrError compilerResult;
    if (program.has_value()) {
        // Run the compiler to get an IR and invoke the tool.
        ASSIGN_OR_RETURN(
            compilerResult,
            P4Tools::CompilerTarget::runCompiler(flayOptions, TOOL_NAME, program.value().get()),
            std::nullopt);
    } else {
        RETURN_IF_FALSE_WITH_MESSAGE(!flayOptions.file.empty(), std::nullopt,
                                     error("Expected a file input."));
        // Run the compiler to get an IR and invoke the tool.
        ASSIGN_OR_RETURN(compilerResult,
                         P4Tools::CompilerTarget::runCompiler(flayOptions, TOOL_NAME),
                         std::nullopt);
    }

    ASSIGN_OR_RETURN_WITH_MESSAGE(const auto &flayCompilerResult,
                                  compilerResult.value().get().to<FlayCompilerResult>(),
                                  std::nullopt, error("Expected a FlayCompilerResult."));

    const auto *programInfo = FlayTarget::produceProgramInfo(flayCompilerResult);
    if (programInfo == nullptr || errorCount() > 0) {
        error("P4Flay encountered errors during preprocessing.");
        return std::nullopt;
    }

    // If we write to the optimized program to file, also dump the program after the midend.
    auto optimizedOutputDir = flayOptions.optimizedOutputDir();
    if (optimizedOutputDir != std::nullopt) {
        if (!std::filesystem::exists(optimizedOutputDir.value())) {
            std::filesystem::create_directory(optimizedOutputDir.value());
        }
    }

    PartialEvaluationOptions partialEvaluationOptions;
    IncrementalAnalysisMap incrementalAnalysisMap;
    auto [result, inserted] = incrementalAnalysisMap.emplace(
        "partialEvaluation",
        std::make_unique<PartialEvaluation>(flayOptions, flayCompilerResult, *programInfo,
                                            partialEvaluationOptions));
    if (result->second->initialize() != EXIT_SUCCESS) {
        return std::nullopt;
    }
    return runServiceWrapper(flayOptions, flayCompilerResult, std::move(incrementalAnalysisMap));
}

std::optional<FlayServiceStatisticsMap> Flay::optimizeProgram(const std::string &program,
                                                              const FlayOptions &flayOptions) {
    try {
        return optimizeProgramImpl(program, flayOptions);
    } catch (const std::exception &e) {
        std::cerr << "Internal error: " << e.what() << "\n";
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<FlayServiceStatisticsMap> Flay::optimizeProgram(const FlayOptions &flayOptions) {
    try {
        return optimizeProgramImpl(std::nullopt, flayOptions);
    } catch (const std::exception &e) {
        std::cerr << "Internal error: " << e.what() << "\n";
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace P4::P4Tools::Flay
