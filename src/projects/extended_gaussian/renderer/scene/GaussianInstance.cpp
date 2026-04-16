#include "GaussianInstance.hpp"
#include "core/system/Quaternion.hpp"

namespace sibr {
	GaussianInstance::GaussianInstance(const std::string& p_name) : name(p_name)
	{
	}

	GaussianInstance::GaussianInstance(const std::string& p_name, const std::string& p_assetId, Vector3f p_position, Vector3f p_euler_angle, float p_scale)
		:name(p_name),
		assetId(p_assetId),
		position(p_position),
		euler_angle(p_euler_angle),
		scale(p_scale)
	{
	}

	Matrix4f GaussianInstance::getTranslationMatrix() const {
		Matrix4f res = Matrix4f::Identity();
		res.block<3, 1>(0, 3) = position;
		return res;
	}

	Quaternionf GaussianInstance::getRotationQuaternion() const {
		const float degToRad = 3.1415926535f / 180.0f;

		Eigen::AngleAxisf rollAngle(euler_angle.x() * degToRad, Vector3f::UnitX());
		Eigen::AngleAxisf pitchAngle(euler_angle.y() * degToRad, Vector3f::UnitY());
		Eigen::AngleAxisf yawAngle(euler_angle.z() * degToRad, Vector3f::UnitZ());

		Quaternionf q = rollAngle * pitchAngle * yawAngle;

		return q.normalized();
	}

	Matrix4f GaussianInstance::getRotationMatrix() const {
		Matrix4f res = Matrix4f::Identity();
		res.block<3, 3>(0, 0) = getRotationQuaternion().toRotationMatrix();
		return res;
	}

	Matrix4f GaussianInstance::getScaleMatrix() const {
		Matrix4f res = Matrix4f::Identity();
		res(0, 0) = scale;
		res(1, 1) = scale;
		res(2, 2) = scale;
		return res;
	}

	const std::string& GaussianInstance::getName() const {
		return name;
	}

	void GaussianInstance::setName(const std::string& p_name) {
		name = p_name;
	}

	const std::string& GaussianInstance::getAssetId() const {
		return assetId;
	}

	void GaussianInstance::setAssetId(const std::string& p_assetId) {
		assetId = p_assetId;
	}

	bool GaussianInstance::hasAsset() const {
		return !assetId.empty();
	}

	const Vector3f& GaussianInstance::getPosition() const {
		return position;
	}

	void GaussianInstance::setPosition(const Vector3f& p_position) {
		position = p_position;
	}

	const Vector3f& GaussianInstance::getEuler() const {
		return euler_angle;
	}

	void GaussianInstance::setEuler(const Vector3f& p_euler) {
		euler_angle = p_euler;
	}

	float GaussianInstance::getScale() const {
		return scale;
	}

	void GaussianInstance::setScale(float p_scale) {
		scale = p_scale;
	}

	std::string& GaussianInstance::getNameRef() {
		return name;
	}
	Vector3f& GaussianInstance::getPositionRef() {
		return position;
	}
	Vector3f& GaussianInstance::getEulerRef() {
		return euler_angle;
	}
	float& GaussianInstance::getScaleRef() {
		return scale;
	}
}
