/****************************************************************
 * Copyright (c) 2023 Anlogic, Inc.  All rights reserved.
 * SPDX-License-Identifier: MIT
 ****************************************************************/

#include "armnnUtils/DataLayoutIndexed.hpp"
#include "armnnALUtils/cma_mem_init.hpp"
#include "buffered_executor.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include "armnn/IProfiler.hpp"

namespace {
struct CubeNetworkProfileState {
    armnn::NetworkId network;
    std::string path;
    unsigned int max_runs;
    unsigned int runs = 0;
};
struct CubeProfileState {
    void (*destroy)(armnn::IRuntime*);
    std::vector<CubeNetworkProfileState> networks;
};
std::mutex cube_profile_mutex;
std::map<armnn::IRuntime*, CubeProfileState> cube_profiles;

armnn::IRuntimePtr createRuntime() {
    // The feature graph is CPU-only and the head owns its Alnpu buffers. Only
    // their external tensors need CMA (allocated below by CmaBuf). Do not
    // register CmaAllocator for CpuAcc: this SDK's AclAllocatorDecorator wraps
    // custom allocations in non-owning MemoryRegion objects. Switching graphs
    // releases/reacquires the CPU pools, losing those CMA allocations each time.
    // With no custom allocator, NeonBackend selects the owning ACL Allocator.
    std::cerr << "V28_RUNTIME_CREATE CPU workspace=default-heap IO=CMA NPU=CMA\n";
    armnn::IRuntime::CreationOptions options;
    return armnn::IRuntime::Create(options);
}

void traceCmaUsage(armnn::NetworkId network) {
    const char* setting = std::getenv("CUBE_CMA_TRACE");
    if (setting && std::string(setting) == "1") {
        // EnqueueWorkload has completed here; no additional inference is
        // launched by this diagnostic and its own memory use is bounded.
        std::cerr << "V28_CMA network=" << network
                  << " managed_bytes=" << cma_mem_count() << '\n';
    }
}

armnn::ModelOptions cpuModelOptions() {
    armnn::ModelOptions options;
    const char* setting = std::getenv("CUBE_CPU_THREADS");
    const std::string requested = setting && *setting ? setting : "2";
    if (requested != "1" && requested != "2" && requested != "4")
        throw std::runtime_error("CUBE_CPU_THREADS must be 1, 2 or 4");
    const unsigned int count = static_cast<unsigned int>(std::stoul(requested));
    options.push_back(armnn::BackendOptions{"CpuAcc", {{"NumberOfThreads", count}}});
    std::cerr << "CPU_ASSIST NumberOfThreads=" << count << '\n';
    return options;
}

// The SDK executor has an implicit destructor. Its runtime smart-pointer deleter
// provides a shutdown hook without changing the SDK class layout or ABI.
void destroyProfiledRuntime(armnn::IRuntime* runtime) noexcept {
    CubeProfileState state{};
    {
        std::lock_guard<std::mutex> lock(cube_profile_mutex);
        const auto found = cube_profiles.find(runtime);
        if (found == cube_profiles.end()) return; // Only registered runtimes use this deleter.
        state = std::move(found->second);
        cube_profiles.erase(found);
    }
    for (const auto& network : state.networks) {
        try {
            const auto profiler = runtime->GetProfiler(network.network);
            if (profiler) {
                profiler->EnableProfiling(false);
                std::ofstream output(network.path, std::ios::out | std::ios::trunc);
                if (!output) throw std::runtime_error("cannot open profiling output");
                profiler->AnalyzeEventsAndWriteResults(output);
                output.flush();
                if (!output) throw std::runtime_error("cannot write profiling output");
                std::cerr << "CPU_NPU_PROFILE network=" << network.network
                          << " runs=" << network.runs << " saved to " << network.path << '\n';
            }
        } catch (const std::exception& error) {
            std::cerr << "CPU_NPU_PROFILE " << network.path << ": " << error.what() << '\n';
        } catch (...) {
            std::cerr << "CPU_NPU_PROFILE failed to write " << network.path << '\n';
        }
    }
    state.destroy(runtime);
}

void enableNetworkProfile(armnn::IRuntimePtr& runtime, armnn::NetworkId network,
                          const std::string& modelPath) {
    const char* prefix = std::getenv("CUBE_PROFILE_PATH");
    if (!prefix || !*prefix) return;
    unsigned int max_runs = 10;
    const char* requested = std::getenv("CUBE_PROFILE_MAX_RUNS");
    if (requested && *requested) {
        const std::string value(requested);
        if (value.find_first_not_of("0123456789") != std::string::npos || value.size() > 3)
            throw std::runtime_error("CUBE_PROFILE_MAX_RUNS must be between 1 and 100");
        max_runs = static_cast<unsigned int>(std::stoul(value));
        if (max_runs < 1 || max_runs > 100)
            throw std::runtime_error("CUBE_PROFILE_MAX_RUNS must be between 1 and 100");
    }
    const auto profiler = runtime->GetProfiler(network);
    if (!profiler) throw std::runtime_error("ArmNN profiler is unavailable");
    const auto separator = modelPath.find_last_of("/\\");
    std::string tag = modelPath.substr(separator == std::string::npos ? 0 : separator + 1);
    for (char& c : tag)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) c = '_';
    const std::string path = std::string(prefix) + "." + tag + ".network" +
                             std::to_string(network) + ".json";
    {
        std::lock_guard<std::mutex> lock(cube_profile_mutex);
        auto found = cube_profiles.find(runtime.get());
        if (found == cube_profiles.end()) {
            found = cube_profiles.emplace(runtime.get(), CubeProfileState{
                runtime.get_deleter(), {}}).first;
            runtime.get_deleter() = destroyProfiledRuntime;
        }
        found->second.networks.push_back(CubeNetworkProfileState{network, path, max_runs});
    }
    profiler->EnableProfiling(true);
    std::cerr << "CPU_NPU_PROFILE network=" << network << " model=" << modelPath
              << " max_runs=" << max_runs << " path=" << path << '\n';
}

void recordProfileRun(armnn::IRuntime* runtime, armnn::NetworkId network) {
    std::lock_guard<std::mutex> lock(cube_profile_mutex);
    const auto found = cube_profiles.find(runtime);
    if (found == cube_profiles.end()) return;
    for (auto& state : found->second.networks) {
        if (state.network == network && state.runs < state.max_runs) {
            if (++state.runs == state.max_runs)
                runtime->GetProfiler(network)->EnableProfiling(false);
            break;
        }
    }
}
} // namespace

// Board-specific assistance for operators unavailable in this PL/runtime revision.
// V28 depthwise operations are not implemented by the installed hard NPU.
class CubeCpuAssist final : public armnn::IStrategy {
    std::set<std::string> selected;
public:
    CubeCpuAssist() {
        const char* setting = std::getenv("CUBE_CPU_OPS");
        std::stringstream input(setting ? setting : "Splitter,Addition,ElementwiseBinary,Multiplication,Concat,Resize,Pooling2d,DepthwiseConvolution2d");
        const std::set<std::string> allowed{
            "Splitter", "Addition", "ElementwiseBinary", "Multiplication", "Concat", "Resize", "Pooling2d", "DepthwiseConvolution2d"};
        std::string token;
        while (std::getline(input, token, ',')) {
            if (!allowed.count(token)) throw std::runtime_error("Invalid CUBE_CPU_OPS entry: " + token);
            selected.insert(token);
        }
    }
    void ExecuteStrategy(const armnn::IConnectableLayer* layer, const armnn::BaseDescriptor&,
                         const std::vector<armnn::ConstTensor>&, const char* name,
                         armnn::LayerBindingId = 0) override {
        const std::string kind = armnn::GetLayerTypeAsCString(layer->GetType());
        // Selected release keeps feature extraction in FP32; only the UINT8 head uses Alnpu.
        const bool float_feature = name && std::string(name).find("/features.") != std::string::npos;
        if (selected.count(kind) || float_feature) {
            const_cast<armnn::IConnectableLayer*>(layer)->BackendSelectionHint(
                armnn::Optional<armnn::BackendId>(armnn::BackendId("CpuAcc")));
            std::cerr << "CPU_ASSIST " << kind << " " << (name ? name : "") << '\n';
        }
    }
};



BufferedNPUExecutor::BufferedNPUExecutor(std::string& modelPath, std::vector<armnn::BackendId>& backends): m_runtime(createRuntime()) {
    armnn::ConfigureLogging(true, false, armnn::LogSeverity::Info);
    std::cerr << "V28_MODEL_PARSE " << modelPath << '\n';

    armnnOnnxParser::IOnnxParserPtr parser = armnnOnnxParser::IOnnxParser::Create();

    // Construct ArmNN network
    armnn::INetworkPtr network = parser->CreateNetworkFromBinaryFile(modelPath.c_str());
    if (!network) throw std::runtime_error("Model parsing failed: " + modelPath);
    CubeCpuAssist cpu_assist;
    network->ExecuteStrategy(cpu_assist);

    // Optimise ArmNN network
    const armnn::ModelOptions cpu_options = cpuModelOptions();
    armnn::OptimizerOptionsOpaque op_options(false, false, false, true, cpu_options, true);
    op_options.SetImportEnabled(true);
    op_options.SetExportEnabled(true);

    std::cerr << "V28_MODEL_OPTIMIZE " << modelPath << '\n';
    armnn::IOptimizedNetworkPtr optNet = armnn::Optimize(*network, backends, m_runtime->GetDeviceSpec(), op_options);
    if (!optNet) throw std::runtime_error("NPU graph optimization failed");

    // Load graph into runtime
    armnn::INetworkProperties networkProperties(
        false,
        armnn::MemorySource::Malloc, armnn::MemorySource::Malloc,
        false,
        armnn::ProfilingDetailsMethod::DetailsWithEvents, false, 2);
    std::string errorMsg;
    std::cerr << "V28_MODEL_LOAD " << modelPath << '\n';
    if (m_runtime->LoadNetwork(m_netId, std::move(optNet), errorMsg, networkProperties) != armnn::Status::Success)
        throw std::runtime_error("NPU graph loading failed: " + errorMsg);

    // Each graph has its own bounded profile, even when sharing a runtime.
    enableNetworkProfile(m_runtime, m_netId, modelPath);

    armnn::ConfigureLogging(true, false, armnn::LogSeverity::Error);

    for (const std::string &name : parser->GetInputTensorNames()) {
        m_inputBindingInfo.push_back(parser->GetNetworkInputBindingInfo(name));
        m_inputLayerNamesList.push_back(name);
    }
    for (const std::string &name : parser->GetOutputTensorNames()) {
        m_outputBindingInfo.push_back(parser->GetNetworkOutputBindingInfo(name));
        m_outputLayerNamesList.push_back(name);
    }

    
    for (auto &info : m_inputBindingInfo) {
        info.second.SetConstant(true);
        for (int idx = 0; idx < MAX_BUF_NUM; idx++) {
            m_inputBuffers.at(idx).emplace_back(CmaBuf(info.second.GetNumBytes()));
            m_inputTensors.at(idx).push_back({info.first, armnn::ConstTensor(info.second, m_inputBuffers.at(idx).back()->virt_addr)});
        }
	}
    
	for (const auto &info : m_outputBindingInfo) {
        for (int idx = 0; idx < MAX_BUF_NUM; idx++) {
            m_outputBuffers.at(idx).emplace_back(CmaBuf(info.second.GetNumBytes()));
            m_outputTensors.at(idx).push_back({info.first, armnn::Tensor(info.second, m_outputBuffers.at(idx).back()->virt_addr)});
        }
	}
}

Size BufferedNPUExecutor::GetInputAspectRatio(int tensorIndex) {
	const auto shape = m_inputBindingInfo.at(tensorIndex).second.GetShape();
    assert(shape.GetNumDimensions() == 4);
    armnnUtils::DataLayoutIndexed nchw(armnn::DataLayout::NCHW);
    return Size(shape[nchw.GetWidthIndex()],
                shape[nchw.GetHeightIndex()]);
}

armnn::DataType BufferedNPUExecutor::GetInputDataType() const {
	assert(m_inputLayerNamesList.size() > 0);
	return m_inputBindingInfo.at(0).second.GetDataType();
}

float BufferedNPUExecutor::GetInputQuantizationScale(int tensorIndex) {
	return m_inputBindingInfo.at(tensorIndex).second.GetQuantizationScale();
}

int BufferedNPUExecutor::GetInputQuantizationOffset(int tensorIndex) {
	return m_inputBindingInfo.at(tensorIndex).second.GetQuantizationOffset();
}

float BufferedNPUExecutor::GetOutputQuantizationScale(int tensorIndex) {
	return m_outputBindingInfo.at(tensorIndex).second.GetQuantizationScale();
}

int BufferedNPUExecutor::GetOutputQuantizationOffset(int tensorIndex) {
	return m_outputBindingInfo.at(tensorIndex).second.GetQuantizationOffset();
}

std::string BufferedNPUExecutor::GetInputTensorName(int tensorIndex) {
	return m_inputLayerNamesList.at(tensorIndex);
}

std::string BufferedNPUExecutor::GetOutputTensorName(int tensorIndex) {
	return m_outputLayerNamesList.at(tensorIndex);
}

CmaBuf& BufferedNPUExecutor::GetInputBuffer(int tensorIndex, int bufferIndex) {
	return m_inputBuffers.at(bufferIndex).at(tensorIndex);
}

CmaBuf& BufferedNPUExecutor::GetOutputBuffer(int tensorIndex, int bufferIndex) {
	return m_outputBuffers.at(bufferIndex).at(tensorIndex);
}

bool BufferedNPUExecutor::Run(int bufferIndex) {

    armnn::Status ret = m_runtime->EnqueueWorkload(m_netId, m_inputTensors.at(bufferIndex), m_outputTensors.at(bufferIndex));

    recordProfileRun(m_runtime.get(), m_netId);
    traceCmaUsage(m_netId);

    std::stringstream inferenceFinished;
    inferenceFinished << "Inference finished with code {" << log_as_int(ret) << "}\n";

    ARMNN_LOG(trace) << inferenceFinished.str();

    if (ret == armnn::Status::Failure)
    {
        ARMNN_LOG(error) << "Failed to perform inference.";
    }

    return (armnn::Status::Success == ret);
}


BufferedNPUExecutorMultiModel::BufferedNPUExecutorMultiModel(std::vector<std::string>& modelPaths, std::vector<armnn::BackendId>& backends): m_runtime(createRuntime()) {
    armnn::ConfigureLogging(true, false, armnn::LogSeverity::Info);

    armnnOnnxParser::IOnnxParserPtr parser = armnnOnnxParser::IOnnxParser::Create();

    // Optimise ArmNN network
    const armnn::ModelOptions cpu_options = cpuModelOptions();
    armnn::OptimizerOptionsOpaque op_options(false, false, false, true, cpu_options, true);
    op_options.SetImportEnabled(true);
    op_options.SetExportEnabled(true);

    armnn::INetworkProperties networkProperties(
        false,
        armnn::MemorySource::Malloc, armnn::MemorySource::Malloc,
        false,
        armnn::ProfilingDetailsMethod::DetailsWithEvents, false, 2);

    armnn::ConfigureLogging(true, false, armnn::LogSeverity::Error);


    m_netIds.resize(modelPaths.size());
    m_inputBindingInfos.resize(modelPaths.size());
    m_inputLayerNamesLists.resize(modelPaths.size());
    m_inputBuffers.resize(modelPaths.size());
    m_inputTensors.resize(modelPaths.size());
    m_outputBindingInfos.resize(modelPaths.size());
    m_outputLayerNamesLists.resize(modelPaths.size());
    m_outputBuffers.resize(modelPaths.size());
    m_outputTensors.resize(modelPaths.size());

    // Construct ArmNN network with a fresh parser per graph.
    for(int modelIndex = 0; modelIndex < modelPaths.size(); modelIndex++) {
        std::cerr << "V28_MODEL_PARSE index=" << modelIndex << " " << modelPaths[modelIndex] << '\n';
        parser = armnnOnnxParser::IOnnxParser::Create();
        armnn::INetworkPtr network = parser->CreateNetworkFromBinaryFile(modelPaths[modelIndex].c_str());
        if (!network) throw std::runtime_error("multi-model parse failed");
        CubeCpuAssist cpu_assist;
        network->ExecuteStrategy(cpu_assist);
        // V28 graph 0 is the FP32 feature extractor; graph 1 is the UINT8 NPU head.
        const std::vector<armnn::BackendId> selected_backends = modelIndex == 0
            ? std::vector<armnn::BackendId>{"CpuAcc"} : backends;
        std::cerr << "V28_MODEL_OPTIMIZE index=" << modelIndex
                  << (modelIndex == 0 ? " CpuAcc features" : " requested NPU head backends") << '\n';
        armnn::IOptimizedNetworkPtr optNet = armnn::Optimize(*network, selected_backends, m_runtime->GetDeviceSpec(), op_options);
        if (!optNet) throw std::runtime_error("multi-model optimization failed");

        // Load graph into runtime
        std::string errorMsg;
        std::cerr << "V28_MODEL_LOAD index=" << modelIndex << '\n';
        if (m_runtime->LoadNetwork(m_netIds[modelIndex], std::move(optNet), errorMsg, networkProperties) != armnn::Status::Success)
            throw std::runtime_error("multi-model load failed: " + errorMsg);
        std::cerr << "V28_MODEL_READY index=" << modelIndex << " network=" << m_netIds[modelIndex] << '\n';
        enableNetworkProfile(m_runtime, m_netIds[modelIndex], modelPaths[modelIndex]);

        for (const std::string &name : parser->GetInputTensorNames()) {
            m_inputBindingInfos[modelIndex].push_back(parser->GetNetworkInputBindingInfo(name));
            m_inputLayerNamesLists[modelIndex].push_back(name);
        }
        for (const std::string &name : parser->GetOutputTensorNames()) {
            m_outputBindingInfos[modelIndex].push_back(parser->GetNetworkOutputBindingInfo(name));
            m_outputLayerNamesLists[modelIndex].push_back(name);
        }

        for (auto &info : m_inputBindingInfos[modelIndex]) {
            info.second.SetConstant(true);
            for (int idx = 0; idx < MAX_BUF_NUM; idx++) {
                m_inputBuffers[modelIndex].at(idx).emplace_back(CmaBuf(info.second.GetNumBytes()));
                m_inputTensors[modelIndex].at(idx).push_back({info.first, armnn::ConstTensor(info.second, m_inputBuffers[modelIndex].at(idx).back()->virt_addr)});
            }
        }
        
        for (const auto &info : m_outputBindingInfos[modelIndex]) {
            for (int idx = 0; idx < MAX_BUF_NUM; idx++) {
                m_outputBuffers[modelIndex].at(idx).emplace_back(CmaBuf(info.second.GetNumBytes()));
                m_outputTensors[modelIndex].at(idx).push_back({info.first, armnn::Tensor(info.second, m_outputBuffers[modelIndex].at(idx).back()->virt_addr)});
            }
        }
    }
           
    

}

Size BufferedNPUExecutorMultiModel::GetInputAspectRatio(int modelIndex, int tensorIndex) {
	const auto shape = m_inputBindingInfos[modelIndex].at(tensorIndex).second.GetShape();
    assert(shape.GetNumDimensions() == 4);
    armnnUtils::DataLayoutIndexed nchw(armnn::DataLayout::NCHW);
    return Size(shape[nchw.GetWidthIndex()],
                shape[nchw.GetHeightIndex()]);
}

armnn::DataType BufferedNPUExecutorMultiModel::GetInputDataType(int modelIndex) const {
	assert(m_inputLayerNamesLists[modelIndex].size() > 0);
	return m_inputBindingInfos[modelIndex].at(0).second.GetDataType();
}

float BufferedNPUExecutorMultiModel::GetInputQuantizationScale(int modelIndex, int tensorIndex) {
	return m_inputBindingInfos[modelIndex].at(tensorIndex).second.GetQuantizationScale();
}

int BufferedNPUExecutorMultiModel::GetInputQuantizationOffset(int modelIndex, int tensorIndex) {
	return m_inputBindingInfos[modelIndex].at(tensorIndex).second.GetQuantizationOffset();
}

float BufferedNPUExecutorMultiModel::GetOutputQuantizationScale(int modelIndex, int tensorIndex) {
	return m_outputBindingInfos[modelIndex].at(tensorIndex).second.GetQuantizationScale();
}

int BufferedNPUExecutorMultiModel::GetOutputQuantizationOffset(int modelIndex, int tensorIndex) {
	return m_outputBindingInfos[modelIndex].at(tensorIndex).second.GetQuantizationOffset();
}

std::string BufferedNPUExecutorMultiModel::GetInputTensorName(int modelIndex, int tensorIndex) {
	return m_inputLayerNamesLists[modelIndex].at(tensorIndex);
}

std::string BufferedNPUExecutorMultiModel::GetOutputTensorName(int modelIndex, int tensorIndex) {
	return m_outputLayerNamesLists[modelIndex].at(tensorIndex);
}

CmaBuf& BufferedNPUExecutorMultiModel::GetInputBuffer(int modelIndex, int tensorIndex, int bufferIndex) {
	return m_inputBuffers[modelIndex].at(bufferIndex).at(tensorIndex);
}

CmaBuf& BufferedNPUExecutorMultiModel::GetOutputBuffer(int modelIndex, int tensorIndex, int bufferIndex) {
	return m_outputBuffers[modelIndex].at(bufferIndex).at(tensorIndex);
}

bool BufferedNPUExecutorMultiModel::Run(int modelIndex, int bufferIndex) {

    armnn::Status ret = m_runtime->EnqueueWorkload(m_netIds[modelIndex], m_inputTensors[modelIndex].at(bufferIndex), m_outputTensors[modelIndex].at(bufferIndex));
    recordProfileRun(m_runtime.get(), m_netIds[modelIndex]);
    traceCmaUsage(m_netIds[modelIndex]);

    std::stringstream inferenceFinished;
    inferenceFinished << "Inference finished with code {" << log_as_int(ret) << "}\n";

    ARMNN_LOG(trace) << inferenceFinished.str();

    if (ret == armnn::Status::Failure)
    {
        ARMNN_LOG(error) << "Failed to perform inference.";
    }

    return (armnn::Status::Success == ret);
}
