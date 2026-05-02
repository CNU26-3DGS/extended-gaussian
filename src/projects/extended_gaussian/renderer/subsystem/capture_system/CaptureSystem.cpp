#include "CaptureSystem.hpp"

namespace sibr {
	void CaptureSystem::addCapture(
		const std::string& name,
		GaussianInstance* instance,
		const GaussianField* field,
		const Vector3f& pos,
		const Vector3f& euler,
		float scale,
		const Matrix4f& view) {
		captures.insert({name, std::make_unique<Capture>(name, instance, field, pos, euler, scale, view)});
	}

	Capture* CaptureSystem::getCapture(const std::string& name) {
		auto it = captures.find(name);
		if (it == captures.end()) {
			return nullptr;
		}
		return it->second.get();
	}
	const Capture* CaptureSystem::getCapture(const std::string& name) const {
		auto it = captures.find(name);
		if (it == captures.end()) {
			return nullptr;
		}
		return it->second.get();
	}

	bool CaptureSystem::removeCapture(const std::string& name) {
		return captures.erase(name);	
	}

	const std::unordered_map<std::string, Capture::UPtr>& CaptureSystem::getCaptures() const {
		return captures;
	}
}