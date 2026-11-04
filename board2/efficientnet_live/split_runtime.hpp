#pragma once
// Two independently parsed networks. Quantization boundary is explicit in the application.
#include <armnn/ArmNN.hpp>
#include <armnn/IProfiler.hpp>
#include <armnn/Logging.hpp>
#include <armnnOnnxParser/IOnnxParser.hpp>
#include <armnnALUtils/cma_mem_init.hpp>
#include <algorithm>
#include <cfenv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using Clock=std::chrono::steady_clock;
static double ms(Clock::time_point a,Clock::time_point b) { return std::chrono::duration<double,std::milli>(b-a).count(); }
static std::string json(const std::string& s) {
    std::ostringstream o;o << '"';
    for(unsigned char c:s) { if(c=='"'||c=='\\') o << '\\' << c;else if(c<32) o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;else o << c; }
    o << '"';return o.str();
}
struct CmaZone {
    CmaZone() { if(cma_mem_init()!=0) throw std::runtime_error("CMA initialization failed"); }
    ~CmaZone() { cma_mem_deinit(); }
};
struct Buffer {
    cma_buffer_t data;
    explicit Buffer(size_t n):data(static_cast<uint32_t>(n)) {
        if(!n||n>UINT32_MAX) throw std::runtime_error("Invalid buffer size");
        cma_mem_alloc(&data);if(!data.phys_addr||!data.virt_addr) throw std::runtime_error("CMA allocation failed");
    }
    ~Buffer() { if(data.phys_addr)cma_mem_free(&data); }
    Buffer(const Buffer&)=delete;Buffer& operator=(const Buffer&)=delete;
};
static void writeRaw(const std::string& path,const void* ptr,size_t n) {
    std::ofstream f(path,std::ios::binary);f.write(static_cast<const char*>(ptr),n);if(!f)throw std::runtime_error("Cannot write "+path);
}
static void shape(const armnn::TensorInfo& t,const std::vector<unsigned>& dims,const std::string& label) {
    if(t.GetNumDimensions()!=dims.size())throw std::runtime_error(label+" rank mismatch");
    for(unsigned i=0;i<dims.size();++i)if(t.GetShape()[i]!=dims[i])throw std::runtime_error(label+" shape mismatch");
}
static void describe(const std::string& label,const armnn::TensorInfo& t) {
    std::cout << label << " dtype=" << armnn::GetDataTypeName(t.GetDataType()) << " shape=[";
    for(unsigned i=0;i<t.GetNumDimensions();++i)std::cout << (i?",":"") << t.GetShape()[i];
    std::cout << "] bytes=" << t.GetNumBytes() << " scale=" << t.GetQuantizationScale() << " zp=" << t.GetQuantizationOffset() << std::endl;
}
static armnn::IRuntimePtr makeRuntime() {
    armnn::IRuntime::CreationOptions o;
    // Leave CpuAcc absent: use its standard allocator for the large FP32 front.
    // CMA is needed only by Alnpu and its small CpuRef fallback in the tail.
    o.m_CustomAllocatorMap["CpuRef"]=std::shared_ptr<armnn::ICustomAllocator>(new armnn::CmaAllocator);
    return armnn::IRuntime::Create(o);
}
class Model {
    // One runtime owns the custom allocator registration for both networks.
    // The caller keeps it alive until both Models have unloaded their networks.
    armnn::IRuntime* runtime;
    armnn::NetworkId networkId;
    bool loaded=false;
    std::string prefix;
    armnn::InputTensors inputTensors;
    armnn::OutputTensors outputTensors;
public:
    armnnOnnxParser::BindingPointInfo in,out;
    std::unique_ptr<Buffer> input,output;
    Model(armnn::IRuntime& sharedRuntime,const std::string& path,const std::vector<armnn::BackendId>& backends,const std::string& artifacts,bool tail)
        :runtime(&sharedRuntime),prefix(artifacts) {
        std::cout << "PARSE_BEGIN " << path << std::endl;
        auto parser=armnnOnnxParser::IOnnxParser::Create();auto net=parser->CreateNetworkFromBinaryFile(path.c_str());
        if(!net)throw std::runtime_error("Null parsed network");
        if(parser->GetInputTensorNames().size()!=1||parser->GetOutputTensorNames().size()!=1)throw std::runtime_error("Requires one input and one output per model");
        in=parser->GetNetworkInputBindingInfo(parser->GetInputTensorNames()[0]);
        out=parser->GetNetworkOutputBindingInfo(parser->GetOutputTensorNames()[0]);
        describe(prefix+" INPUT",in.second);describe(prefix+" OUTPUT",out.second);
        if(tail) {
            shape(in.second,{1,320,7,7},"tail input");shape(out.second,{1,3},"tail output");
            for(const auto* t:{&in.second,&out.second}) {
                if(t->GetDataType()!=armnn::DataType::QAsymmU8)throw std::runtime_error("Tail must have actual QAsymmU8 runtime I/O");
                if(!(t->GetQuantizationScale()>0)||!std::isfinite(t->GetQuantizationScale())||t->GetQuantizationOffset()<0||t->GetQuantizationOffset()>255)
                    throw std::runtime_error("Invalid tail quantization parameters");
            }
            if(in.second.GetNumBytes()!=320*7*7||out.second.GetNumBytes()!=3)throw std::runtime_error("Tail byte count mismatch");
        } else {
            shape(in.second,{1,3,224,224},"CPU input");shape(out.second,{1,320,7,7},"CPU output");
            if(in.second.GetDataType()!=armnn::DataType::Float32||out.second.GetDataType()!=armnn::DataType::Float32)
                throw std::runtime_error("CPU subnetwork must have Float32 I/O");
            if(in.second.GetNumBytes()!=3*224*224*4||out.second.GetNumBytes()!=320*7*7*4)throw std::runtime_error("CPU byte count mismatch");
        }
        std::cout << "PARSE_OK " << prefix << std::endl;
        armnn::OptimizerOptionsOpaque options;options.SetImportEnabled(true);options.SetExportEnabled(true);
        std::cout << "OPTIMIZE_BEGIN " << prefix << std::endl;
        auto optimized=armnn::Optimize(*net,backends,runtime->GetDeviceSpec(),options);
        if(!optimized)throw std::runtime_error("Optimize returned null: "+prefix);
        std::ofstream dot(prefix+"_graph.dot");if(!dot)throw std::runtime_error("Cannot create graph artifact");
        optimized->SerializeToDot(dot);dot.close();
        std::cout << "OPTIMIZE_OK " << prefix << std::endl;
        armnn::INetworkProperties properties(false,armnn::MemorySource::Malloc,armnn::MemorySource::Malloc,false,armnn::ProfilingDetailsMethod::DetailsWithEvents,false,2);
        std::string error;auto status=runtime->LoadNetwork(networkId,std::move(optimized),error,properties);
        std::cout << "LOAD_STATUS " << prefix << " status=" << static_cast<int>(status) << " message=" << error << std::endl;
        if(status!=armnn::Status::Success)throw std::runtime_error("LoadNetwork failed: "+prefix+" "+error);
        loaded=true;
        input.reset(new Buffer(in.second.GetNumBytes()));output.reset(new Buffer(out.second.GetNumBytes()));
        in.second.SetConstant(true);
        inputTensors.emplace_back(in.first,armnn::ConstTensor(in.second,input->data.virt_addr));
        outputTensors.emplace_back(out.first,armnn::Tensor(out.second,output->data.virt_addr));
        auto profiler=runtime->GetProfiler(networkId);if(!profiler)throw std::runtime_error("Missing profiler: "+prefix);
        profiler->EnableProfiling(false);
    }
    ~Model() { if(loaded) runtime->UnloadNetwork(networkId); }
    void enableProfiling(bool enabled) { auto p=runtime->GetProfiler(networkId); if(p)p->EnableProfiling(enabled); }
    double run() {
        if(cma_mem_sync_for_device(input->data.phys_addr,input->data.size)!=0)throw std::runtime_error("Input cache sync failed");
        auto a=Clock::now();auto status=runtime->EnqueueWorkload(networkId,inputTensors,outputTensors);auto b=Clock::now();
        if(status!=armnn::Status::Success)throw std::runtime_error("EnqueueWorkload failed: "+prefix);
        if(cma_mem_sync_for_cpu(output->data.phys_addr,output->data.size)!=0)throw std::runtime_error("Output cache sync failed");
        return ms(a,b);
    }
    void saveProfile() {
        auto p=runtime->GetProfiler(networkId);if(!p)throw std::runtime_error("No profiler");
        p->EnableProfiling(false);std::ofstream f(prefix+"_profile.txt");if(!f)throw std::runtime_error("Cannot write profile");
        p->AnalyzeEventsAndWriteResults(f);f.flush();if(!f)throw std::runtime_error("Incomplete profile write");
    }
};

