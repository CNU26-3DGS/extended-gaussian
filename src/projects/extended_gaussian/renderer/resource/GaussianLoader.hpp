#pragma once

# include <core/system/Config.hpp>
#include "Config.hpp"
#include "GaussianField.hpp"

#include <boost/filesystem.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <fstream>

namespace sibr {
	class SIBR_EXTENDED_GAUSSIAN_EXPORT GaussianLoader {
	public:
		SIBR_CLASS_PTR(GaussianLoader);

		static GaussianField::UPtr load(const std::string& modelPath);
		static GaussianField::Ptr loadShared(const std::string& modelPath);
		static GaussianField::Ptr loadFromModelDir(const boost::filesystem::path& modelPath);

	private:
		template<int D>
		static SIBR_OPT_INLINE bool loadPly(const char* filename, GaussianField& output);
	};

    inline float sigmoid(float x) {
        return 1.0f / (1.0f + std::exp(-x));
    }

    template <int D>
    SIBR_OPT_INLINE bool GaussianLoader::loadPly(const char* filename, GaussianField& out_field)
    {
        struct Gaussian {
            Vector3f pos;
            float n[3];
            float shs[(D + 1) * (D + 1) * 3];
            float opacity;
            float scale[3];
            float rot[4];
        };

        std::ifstream infile(filename, std::ios_base::binary);
        if (!infile.good()) {
            SIBR_ERR << "Unable to find model's PLY file: " << filename << std::endl;
            return false;
        }

        std::string buff;
        std::getline(infile, buff); // ply
        std::getline(infile, buff); // format

        std::getline(infile, buff);
        if (buff.find("comment") != std::string::npos) {
            std::getline(infile, buff);
        }

        uint32_t count = 0;
        if (buff.find("element vertex") != std::string::npos) {
            std::stringstream ss(buff);
            std::string dummy;
            ss >> dummy >> dummy >> count;
        }

        while (std::getline(infile, buff)) {
            if (buff == "end_header") break;
        }

        SIBR_LOG << "Loading " << count << " Gaussians (SH Degree: " << D << ")" << std::endl;

        std::vector<Gaussian> points(count);
        infile.read(reinterpret_cast<char*>(points.data()), count * sizeof(Gaussian));

        out_field.count = count;
        out_field.pos.resize(count);
        out_field.scale.resize(count);
        out_field.rot.resize(count);
        out_field.opacities.resize(count);

        const int SH_N = (D + 1) * (D + 1);
        out_field.SHs.resize(count * SH_N * 3);

        Vector3f minn(FLT_MAX, FLT_MAX, FLT_MAX);
        Vector3f maxx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        Vector3d positionSum = Vector3d::Zero();
        for (uint32_t i = 0; i < count; i++) {
            maxx = maxx.cwiseMax(points[i].pos);
            minn = minn.cwiseMin(points[i].pos);
            positionSum += points[i].pos.cast<double>();
        }
        if (count > 0) {
            out_field.min_edges = minn;
            out_field.max_edges = maxx;
            out_field.has_centroid = true;
            out_field.centroid = (positionSum / static_cast<double>(count)).cast<float>();

            constexpr double kFocusKeepRatio = 0.90;
            auto selectMedian = [](std::vector<float>& values) {
                const size_t middle = values.size() / 2;
                std::nth_element(values.begin(), values.begin() + middle, values.end());
                return values[middle];
            };

            std::vector<float> axisValues(count);
            for (uint32_t i = 0; i < count; ++i) {
                axisValues[i] = points[i].pos.x();
            }
            const float medianX = selectMedian(axisValues);
            for (uint32_t i = 0; i < count; ++i) {
                axisValues[i] = points[i].pos.y();
            }
            const float medianY = selectMedian(axisValues);
            for (uint32_t i = 0; i < count; ++i) {
                axisValues[i] = points[i].pos.z();
            }
            const float medianZ = selectMedian(axisValues);

            const Vector3f medianCenter(medianX, medianY, medianZ);
            const size_t focusCount = std::min<size_t>(
                count,
                std::max<size_t>(
                    1,
                    static_cast<size_t>(std::ceil(static_cast<double>(count) * kFocusKeepRatio))));
            std::vector<float> distances(count);
            for (uint32_t i = 0; i < count; ++i) {
                distances[i] = (points[i].pos - medianCenter).squaredNorm();
            }
            std::nth_element(distances.begin(), distances.begin() + focusCount - 1, distances.end());
            const float distanceThreshold = distances[focusCount - 1];

            Vector3f focusMin(FLT_MAX, FLT_MAX, FLT_MAX);
            Vector3f focusMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
            Vector3d focusSum = Vector3d::Zero();
            size_t keptCount = 0;
            for (uint32_t i = 0; i < count; ++i) {
                if ((points[i].pos - medianCenter).squaredNorm() > distanceThreshold) {
                    continue;
                }
                focusMin = focusMin.cwiseMin(points[i].pos);
                focusMax = focusMax.cwiseMax(points[i].pos);
                focusSum += points[i].pos.cast<double>();
                ++keptCount;
            }

            if (keptCount > 0) {
                out_field.has_focus_bounds = true;
                out_field.focus_center = (focusSum / static_cast<double>(keptCount)).cast<float>();
                out_field.focus_bounds_min = focusMin;
                out_field.focus_bounds_max = focusMax;
            }
        }
        else {
            out_field.min_edges = Vector3f::Zero();
            out_field.max_edges = Vector3f::Zero();
            out_field.has_centroid = false;
            out_field.centroid = Vector3f::Zero();
            out_field.has_focus_bounds = false;
            out_field.focus_center = Vector3f::Zero();
            out_field.focus_bounds_min = Vector3f::Zero();
            out_field.focus_bounds_max = Vector3f::Zero();
        }

        std::vector<std::pair<uint64_t, int>> mapp(count);
        const Vector3f size = maxx - minn;
        const Vector3f safeSize(
            std::abs(size.x()) > FLT_EPSILON ? size.x() : 1.0f,
            std::abs(size.y()) > FLT_EPSILON ? size.y() : 1.0f,
            std::abs(size.z()) > FLT_EPSILON ? size.z() : 1.0f);
        for (uint32_t i = 0; i < count; i++) {
            Vector3f rel = (points[i].pos - minn).array() / safeSize.array();
            Vector3f scaled = ((float((1 << 21) - 1)) * rel);
            Vector3i xyz = scaled.cast<int>();

            uint64_t code = 0;
            for (int j = 0; j < 21; j++) {
                code |= ((uint64_t(xyz.x() & (1 << j))) << (3 * j + 0));
                code |= ((uint64_t(xyz.y() & (1 << j))) << (3 * j + 1));
                code |= ((uint64_t(xyz.z() & (1 << j))) << (3 * j + 2));
            }
            mapp[i] = { code, i };
        }

        std::sort(mapp.begin(), mapp.end());

        for (uint32_t k = 0; k < count; k++) {
            const int i = mapp[k].second;

            out_field.pos[k] = points[i].pos;

            float len = std::sqrt(points[i].rot[0] * points[i].rot[0] + points[i].rot[1] * points[i].rot[1] +
                points[i].rot[2] * points[i].rot[2] + points[i].rot[3] * points[i].rot[3]);
            out_field.rot[k] = Vector4f(points[i].rot[0], points[i].rot[1], points[i].rot[2], points[i].rot[3]) / (len > 1e-5f ? len : 1.0f);

            out_field.scale[k] = Vector3f(std::exp(points[i].scale[0]), std::exp(points[i].scale[1]), std::exp(points[i].scale[2]));

            out_field.opacities[k] = sigmoid(points[i].opacity);

            int baseIdx = k * SH_N * 3;
            out_field.SHs[baseIdx + 0] = points[i].shs[0];
            out_field.SHs[baseIdx + 1] = points[i].shs[1];
            out_field.SHs[baseIdx + 2] = points[i].shs[2];

            for (int j = 1; j < SH_N; j++) {
                out_field.SHs[baseIdx + j * 3 + 0] = points[i].shs[(j - 1) + 3];
                out_field.SHs[baseIdx + j * 3 + 1] = points[i].shs[(j - 1) + SH_N + 2];
                out_field.SHs[baseIdx + j * 3 + 2] = points[i].shs[(j - 1) + 2 * SH_N + 1];
            }
        }

        return true;
    }

	inline GaussianField::Ptr GaussianLoader::loadShared(const std::string& modelPath)
	{
		auto field = load(modelPath);
		if (!field) {
			return nullptr;
		}
		return GaussianField::Ptr(field.release());
	}

	inline GaussianField::Ptr GaussianLoader::loadFromModelDir(const boost::filesystem::path& modelPath)
	{
		return loadShared(modelPath.string());
	}
}
