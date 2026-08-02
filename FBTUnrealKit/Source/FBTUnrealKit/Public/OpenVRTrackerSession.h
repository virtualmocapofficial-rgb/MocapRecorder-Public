#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

namespace vr
{
class IVRSystem;
}

class FOpenVRTrackerSession
{
public:
    static FOpenVRTrackerSession& Get();

    void Acquire();
    void Release();
    void ForceShutdown();

    bool IsInitialized() const;
    vr::IVRSystem* GetVRSystem() const;
    FString GetInitStatus() const;

    bool GetStringTrackedDeviceProperty(int32 DeviceIndex, int32 Property, FString& OutValue) const;

private:
    FOpenVRTrackerSession();
    ~FOpenVRTrackerSession();

    bool TryInitialize();
    bool FinalizeShutdown();
    FString GetOpenVRDllPath() const;

private:
    vr::IVRSystem* VRSystem = nullptr;
    void* OpenVRDllHandle = nullptr;
    int32 RefCount = 0;
    bool bInitialized = false;
    FString InitStatus;
};
