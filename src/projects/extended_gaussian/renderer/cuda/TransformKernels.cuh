// TransformKernels.cuh
#pragma once
#include <cuda_runtime.h>

struct Matrix4x4
{
    float m[16];
};

void launchTransformKernel(
    int blocks, int threads, int count, int offset,
    const float3* l_pos, const float4* l_rot, const float3* l_scale,
    float3* w_pos, float4* w_rot, float3* w_scale,
    Matrix4x4 worldMat, float4 instRot, float instScale);