#include "GPUGaussianField.hpp"
#include <projects/extended_gaussian/renderer/resource/GaussianField.hpp>
#include <projects/extended_gaussian/renderer/subsystem/rendering_system/RenderUtils.hpp>

namespace sibr {
	GPUGaussianField::GPUGaussianField(const std::string& p_assetId, const GaussianField* p_origin)
	{
		asset_id = p_assetId;
		count = p_origin->count;
		sh_degree = p_origin->sh_degree;
		CUDA_SAFE_CALL_ALWAYS(cudaMalloc((void**)&pos_cuda, sizeof(Vector3f) * count));
		CUDA_SAFE_CALL_ALWAYS(cudaMemcpy(pos_cuda, p_origin->pos.data(), sizeof(Vector3f) * count, cudaMemcpyHostToDevice));
		CUDA_SAFE_CALL_ALWAYS(cudaMalloc((void**)&rot_cuda, sizeof(Vector4f) * count));
		CUDA_SAFE_CALL_ALWAYS(cudaMemcpy(rot_cuda, p_origin->rot.data(), sizeof(Vector4f) * count, cudaMemcpyHostToDevice));
		int sh_coeff_count = (p_origin->sh_degree + 1) * (p_origin->sh_degree + 1) * 3;
		CUDA_SAFE_CALL_ALWAYS(cudaMalloc((void**)&shs_cuda, sizeof(float) * sh_coeff_count * count));
		CUDA_SAFE_CALL_ALWAYS(cudaMemcpy(shs_cuda, p_origin->SHs.data(), sizeof(float) * sh_coeff_count * count, cudaMemcpyHostToDevice));
		CUDA_SAFE_CALL_ALWAYS(cudaMalloc((void**)&opacity_cuda, sizeof(float) * count));
		CUDA_SAFE_CALL_ALWAYS(cudaMemcpy(opacity_cuda, p_origin->opacities.data(), sizeof(float) * count, cudaMemcpyHostToDevice));
		CUDA_SAFE_CALL_ALWAYS(cudaMalloc((void**)&scale_cuda, sizeof(Vector3f) * count));
		CUDA_SAFE_CALL_ALWAYS(cudaMemcpy(scale_cuda, p_origin->scale.data(), sizeof(Vector3f) * count, cudaMemcpyHostToDevice));
		bytes = sizeof(Vector3f) * count
			+ sizeof(Vector4f) * count
			+ sizeof(float) * sh_coeff_count * count
			+ sizeof(float) * count
			+ sizeof(Vector3f) * count;
	}

	GPUGaussianField::~GPUGaussianField()
	{
		cudaFree(pos_cuda);
		cudaFree(rot_cuda);
		cudaFree(scale_cuda);
		cudaFree(opacity_cuda);
		cudaFree(shs_cuda);
	}
}
