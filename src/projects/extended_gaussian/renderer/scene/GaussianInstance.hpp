#pragma once

# include <core/system/Config.hpp>
#include "Config.hpp"
#include "core/system/Matrix.hpp"
#include "core/system/Quaternion.hpp"

#include <string>

namespace sibr {
	class SIBR_EXTENDED_GAUSSIAN_EXPORT GaussianInstance {
	public:
		SIBR_CLASS_PTR(GaussianInstance);

		GaussianInstance(const std::string& p_name);
		GaussianInstance(const std::string& p_name, const std::string& p_assetId, Vector3f p_position = Vector3f(), Vector3f p_euler_angle = Vector3f(), float p_scale = 1.f);

		Matrix4f getTranslationMatrix() const;
		Matrix4f getRotationMatrix() const;
		Quaternionf getRotationQuaternion() const;
		Matrix4f getScaleMatrix() const;

		const std::string& getName() const;
		void setName(const std::string& p_name);

		const std::string& getAssetId() const;
		void setAssetId(const std::string& p_assetId);
		bool hasAsset() const;

		const Vector3f& getPosition() const;
		void setPosition(const Vector3f& p_position);

		const Vector3f& getEuler() const;
		void setEuler(const Vector3f& p_euler);

		float getScale() const;
		void setScale(float p_scale);

		// GUI Helpers
		std::string& getNameRef();
		Vector3f& getPositionRef();
		Vector3f& getEulerRef();
		float& getScaleRef();

	private:
		std::string name;
		std::string assetId;
		Vector3f position;
		Vector3f euler_angle;
		float scale = 1.f;
	};
}
