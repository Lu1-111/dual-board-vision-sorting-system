#pragma once
#include "buffered_executor.hpp"

// Lightweight view into one network of the single shared Arm NN runtime.
class IndexedExecutor {
    BufferedNPUExecutorMultiModel& runtime;
    int index;
public:
    IndexedExecutor(BufferedNPUExecutorMultiModel& value, int model) : runtime(value), index(model) {}
    int GetInputTensorNum() { return runtime.GetInputTensorNum(index); }
    int GetOutputTensorNum() { return runtime.GetOutputTensorNum(index); }
    Size GetInputAspectRatio(int tensor) { return runtime.GetInputAspectRatio(index, tensor); }
    armnn::DataType GetInputDataType() const { return runtime.GetInputDataType(index); }
    float GetInputQuantizationScale(int tensor) { return runtime.GetInputQuantizationScale(index, tensor); }
    int GetInputQuantizationOffset(int tensor) { return runtime.GetInputQuantizationOffset(index, tensor); }
    CmaBuf& GetInputBuffer(int tensor, int buffer) { return runtime.GetInputBuffer(index, tensor, buffer); }
    CmaBuf& GetOutputBuffer(int tensor, int buffer) { return runtime.GetOutputBuffer(index, tensor, buffer); }
    armnn::ConstTensor& GetInputTensor(int tensor, int buffer) { return runtime.GetInputTensor(index, tensor, buffer); }
    const armnn::OutputTensors& GetOutputTensors(int buffer) { return runtime.GetOutputTensors(index, buffer); }
    bool Run(int buffer) { return runtime.Run(index, buffer); }
};
