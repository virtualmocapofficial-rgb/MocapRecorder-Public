#include "FBTUnrealKit.h"

#include "FBTMotionControllerProvider.h"
#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"
#include "OpenVRTrackerSession.h"

void FFBTUnrealKitModule::StartupModule()
{
	MotionControllerProvider = MakeUnique<FFBTMotionControllerProvider>();
	FOpenVRTrackerSession::Get().Acquire();
	IModularFeatures::Get().RegisterModularFeature(IMotionController::GetModularFeatureName(), MotionControllerProvider.Get());
}

void FFBTUnrealKitModule::ShutdownModule()
{
	if (MotionControllerProvider)
	{
		IModularFeatures::Get().UnregisterModularFeature(IMotionController::GetModularFeatureName(), MotionControllerProvider.Get());
		MotionControllerProvider.Reset();
	}

	FOpenVRTrackerSession::Get().ForceShutdown();
}

IMPLEMENT_MODULE(FFBTUnrealKitModule, FBTUnrealKit)
