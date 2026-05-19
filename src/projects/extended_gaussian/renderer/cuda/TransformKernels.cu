#include "TransformKernels.cuh"
#include <device_launch_parameters.h>

__device__ float4 normalizeQuat(float4 q) {
    float invLen = rsqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return make_float4(q.x * invLen, q.y * invLen, q.z * invLen, q.w * invLen);
}

__device__ float4 quatMultiply(float4 q1, float4 q2) {
    return make_float4(
        q1.x * q2.x - q1.y * q2.y - q1.z * q2.z - q1.w * q2.w, // W
        q1.x * q2.y + q1.y * q2.x + q1.z * q2.w - q1.w * q2.z, // X
        q1.x * q2.z - q1.y * q2.w + q1.z * q2.x + q1.w * q2.y, // Y
        q1.x * q2.w + q1.y * q2.z - q1.z * q2.y + q1.w * q2.x  // Z
    );
}

__global__ void transformGaussiansKernel(
    int count, int offset,
    const float3* l_pos, const float4* l_rot, const float3* l_scale,
    float3* w_pos, float4* w_rot, float3* w_scale,
    Matrix4x4 worldMat, float4 rotQuat, float instScale
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    int targetIdx = idx + offset;

    // Transform Position
    float3 p = l_pos[idx];
    w_pos[targetIdx].x = worldMat.m[0] * p.x + worldMat.m[1] * p.y + worldMat.m[2] * p.z + worldMat.m[3];
    w_pos[targetIdx].y = worldMat.m[4] * p.x + worldMat.m[5] * p.y + worldMat.m[6] * p.z + worldMat.m[7];
    w_pos[targetIdx].z = worldMat.m[8] * p.x + worldMat.m[9] * p.y + worldMat.m[10] * p.z + worldMat.m[11];

    // Transform Rotation
    w_rot[targetIdx] = normalizeQuat(quatMultiply(rotQuat, l_rot[idx]));

    // Transform scale
    w_scale[targetIdx] = make_float3(l_scale[idx].x * instScale, l_scale[idx].y * instScale, l_scale[idx].z * instScale);

}

__global__ void copySHsKernel(
    int count, int offset,
    const float* src_shs, int src_coeffs,
    float* dst_shs, int dst_coeffs
) {
    const int dst_floats_per_gaussian = dst_coeffs * 3;
    const int src_floats_per_gaussian = src_coeffs * 3;
    const int total = count * dst_floats_per_gaussian;

    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total) return;

    const int gaussian_idx = idx / dst_floats_per_gaussian;
    const int local_float_idx = idx - gaussian_idx * dst_floats_per_gaussian;

    float value = 0.0f;
    if (local_float_idx < src_floats_per_gaussian) {
        value = src_shs[gaussian_idx * src_floats_per_gaussian + local_float_idx];
    }

    dst_shs[(gaussian_idx + offset) * dst_floats_per_gaussian + local_float_idx] = value;
}

void launchTransformKernel(
    int blocks, int threads, int count, int offset,
    const float3* l_pos, const float4* l_rot, const float3* l_scale,
    float3* w_pos, float4* w_rot, float3* w_scale,
    Matrix4x4 worldMat, float4 instRot, float instScale)
{
    transformGaussiansKernel << <blocks, threads >> > (
        count, offset, l_pos, l_rot, l_scale, w_pos, w_rot, w_scale, worldMat, instRot, instScale);
}

void launchCopySHsKernel(
    int blocks, int threads, int count, int offset,
    const float* src_shs, int src_coeffs,
    float* dst_shs, int dst_coeffs)
{
    if (count <= 0 || src_coeffs <= 0 || dst_coeffs <= 0) {
        return;
    }

    copySHsKernel << <blocks, threads >> > (
        count, offset, src_shs, src_coeffs, dst_shs, dst_coeffs);
}
