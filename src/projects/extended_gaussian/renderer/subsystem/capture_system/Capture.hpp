#pragma once

#include <string>
#include <core/system/Config.hpp>
#include <core/system/Matrix.hpp>

#include "Config.hpp"

namespace sibr {
	class GaussianInstance;
	class GaussianField;

	class SIBR_EXTENDED_GAUSSIAN_EXPORT Capture {
	public:
		SIBR_CLASS_PTR(Capture);

		Capture(
			const std::string& p_name,
			GaussianInstance* instance,
			const GaussianField* field,
			const Vector3f& pos,
			const Vector3f& euler,
			float scale,
			const Matrix4f& view) 
			:name(p_name),
			captured_instance(instance),
			captured_field(field),
			capture_position(pos),
			captured_euler_angle(euler),
			captured_scale(scale),
			captured_view(view)
		{}
		~Capture() = default;

		Capture(const Capture&) = delete;
		Capture& operator=(const Capture&) = delete;


		std::string name;
		GaussianInstance* captured_instance;
		const GaussianField* captured_field;
		Vector3f capture_position;
		Vector3f captured_euler_angle;
		float captured_scale = 1.f;
		Matrix4f captured_view;
	};
}