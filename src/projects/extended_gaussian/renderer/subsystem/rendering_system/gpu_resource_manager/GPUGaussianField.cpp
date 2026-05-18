#include "GPUGaussianField.hpp"
#include <projects/extended_gaussian/renderer/resource/GaussianField.hpp>
#include <projects/extended_gaussian/renderer/subsystem/rendering_system/RenderUtils.hpp>

#include <algorithm>
#include <vector>

namespace {
	int clampShDegree(int degree)
	{
		return std::max(0, std::min(3, degree));
	}

	int shCoefficientCount(int degree)
	{
		const int clampedDegree = clampShDegree(degree);
		return (clampedDegree + 1) * (clampedDegree + 1);
	}
}

namespace sibr {
	GPUGaussianField::GPUGaussianField(const std::string& p_assetId, const GaussianField* p_origin)
		: GPUGaussianField(p_assetId, p_origin, p_origin ? p_origin->sh_degree : 3)
	{
	}

	GPUGaussianField::GPUGaussianField(const std::string& p_assetId, const GaussianField* p_origin, int effective_sh_degree)
	{
		asset_id = p_assetId;
		count = p_origin->count;
		sh_degree = clampShDegree(effective_sh_degree);
		CUDA_SAFE_CALL_ALWAYS(cudaMalloc((void**)&pos_cuda, sizeof(Vector3f) * count));
		CUDA_SAFE_CALL_ALWAYS(cudaMemcpy(pos_cuda, p_origin->pos.data(), sizeof(Vector3f) * count, cudaMemcpyHostToDevice));
		CUDA_SAFE_CALL_ALWAYS(cudaMalloc((void**)&rot_cuda, sizeof(Vector4f) * count));
		CUDA_SAFE_CALL_ALWAYS(cudaMemcpy(rot_cuda, p_origin->rot.data(), sizeof(Vector4f) * count, cudaMemcpyHostToDevice));

		const int sourceShCoefficients = shCoefficientCount(p_origin->sh_degree);
		const int targetShCoefficients = shCoefficientCount(sh_degree);
		const int sourceFloatsPerGaussian = sourceShCoefficients * 3;
		const int targetFloatsPerGaussian = targetShCoefficients * 3;
		CUDA_SAFE_CALL_ALWAYS(cudaMalloc((void**)&shs_cuda, sizeof(float) * targetFloatsPerGaussian * count));
		if (p_origin->sh_degree == sh_degree) {
			CUDA_SAFE_CALL_ALWAYS(cudaMemcpy(shs_cuda, p_origin->SHs.data(), sizeof(float) * targetFloatsPerGaussian * count, cudaMemcpyHostToDevice));
		}
		else {
			std::vector<float> packedShs(static_cast<size_t>(count) * targetFloatsPerGaussian, 0.0f);
			const int copiedFloatsPerGaussian = std::min(sourceFloatsPerGaussian, targetFloatsPerGaussian);
			for (int i = 0; i < count; ++i) {
				const float* src = p_origin->SHs.data() + static_cast<size_t>(i) * sourceFloatsPerGaussian;
				float* dst = packedShs.data() + static_cast<size_t>(i) * targetFloatsPerGaussian;
				std::copy(src, src + copiedFloatsPerGaussian, dst);
			}
			CUDA_SAFE_CALL_ALWAYS(cudaMemcpy(shs_cuda, packedShs.data(), sizeof(float) * packedShs.size(), cudaMemcpyHostToDevice));
		}
		CUDA_SAFE_CALL_ALWAYS(cudaMalloc((void**)&opacity_cuda, sizeof(float) * count));
		CUDA_SAFE_CALL_ALWAYS(cudaMemcpy(opacity_cuda, p_origin->opacities.data(), sizeof(float) * count, cudaMemcpyHostToDevice));
		CUDA_SAFE_CALL_ALWAYS(cudaMalloc((void**)&scale_cuda, sizeof(Vector3f) * count));
		CUDA_SAFE_CALL_ALWAYS(cudaMemcpy(scale_cuda, p_origin->scale.data(), sizeof(Vector3f) * count, cudaMemcpyHostToDevice));
		bytes = estimateBytes(p_origin, sh_degree);
		SIBR_LOG << "Uploaded GPU GaussianField '" << asset_id
			<< "' (source SH Degree: " << p_origin->sh_degree
			<< ", GPU SH Degree: " << sh_degree
			<< ", bytes: " << bytes << ")." << std::endl;
	}

	GPUGaussianField::~GPUGaussianField()
	{
		cudaFree(pos_cuda);
		cudaFree(rot_cuda);
		cudaFree(scale_cuda);
		cudaFree(opacity_cuda);
		cudaFree(shs_cuda);
	}

	size_t GPUGaussianField::estimateBytes(const GaussianField* p_origin, int effective_sh_degree)
	{
		if (!p_origin) {
			return 0;
		}

		const size_t gaussianCount = p_origin->count;
		const size_t shFloatCount = static_cast<size_t>(shCoefficientCount(effective_sh_degree)) * 3;
		return sizeof(Vector3f) * gaussianCount
			+ sizeof(Vector4f) * gaussianCount
			+ sizeof(float) * shFloatCount * gaussianCount
			+ sizeof(float) * gaussianCount
			+ sizeof(Vector3f) * gaussianCount;
	}
}
