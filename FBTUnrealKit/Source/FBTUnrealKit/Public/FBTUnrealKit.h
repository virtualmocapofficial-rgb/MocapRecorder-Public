#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FBTUNREALKIT_API FFBTUnrealKitModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TUniquePtr<class FFBTMotionControllerProvider> MotionControllerProvider;
};
