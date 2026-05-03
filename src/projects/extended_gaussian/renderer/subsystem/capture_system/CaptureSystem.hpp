#pragma once

#include <projects/extended_gaussian/renderer/subsystem/Subsystem.hpp>
#include <core/system/Config.hpp>

#include "Config.hpp"
#include "capture.hpp"

#include <unordered_map>

namespace sibr
{

	class SIBR_EXTENDED_GAUSSIAN_EXPORT CaptureSystem : public Subsystem
	{
	public:
		SIBR_CLASS_PTR(CaptureSystem);

		CaptureSystem() = default;
		~CaptureSystem() = default;

		CaptureSystem(const CaptureSystem&) = delete;
		CaptureSystem& operator=(const CaptureSystem&) = delete;

		void onSystemAdded(ExtendedGaussianViewer& owner) override {}

		void onSystemRemoved(ExtendedGaussianViewer& owner) override {}

		void addCapture(
			const std::string& name,
			GaussianInstance* instance,
			const GaussianField* field,
			const Vector3f& pos,
			const Vector3f& euler,
			float scale,
			const Matrix4f& view);

		Capture* getCapture(const std::string& name);
		const Capture* getCapture(const std::string& name) const;

		bool removeCapture(const std::string& name);

		const std::unordered_map<std::string, Capture::UPtr>& getCaptures() const;

	private:
		std::unordered_map<std::string, Capture::UPtr> captures;
	};
}
