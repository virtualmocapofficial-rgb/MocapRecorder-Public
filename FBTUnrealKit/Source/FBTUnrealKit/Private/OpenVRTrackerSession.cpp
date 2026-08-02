#include "OpenVRTrackerSession.h"

#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include <openvr.h>

FOpenVRTrackerSession& FOpenVRTrackerSession::Get()
{
    static FOpenVRTrackerSession Instance;
    return Instance;
}

FOpenVRTrackerSession::FOpenVRTrackerSession()
{
}

FOpenVRTrackerSession::~FOpenVRTrackerSession()
{
    ForceShutdown();
}

void FOpenVRTrackerSession::Acquire()
{
    ++RefCount;
    TryInitialize();
}

void FOpenVRTrackerSession::Release()
{
    RefCount = FMath::Max(0, RefCount - 1);
}

void FOpenVRTrackerSession::ForceShutdown()
{
    RefCount = 0;
    FinalizeShutdown();
}

bool FOpenVRTrackerSession::IsInitialized() const
{
    return bInitialized && VRSystem != nullptr;
}

vr::IVRSystem* FOpenVRTrackerSession::GetVRSystem() const
{
    return IsInitialized() ? VRSystem : nullptr;
}

FString FOpenVRTrackerSession::GetInitStatus() const
{
    return InitStatus;
}

bool FOpenVRTrackerSession::GetStringTrackedDeviceProperty(int32 DeviceIndex, int32 Property, FString& OutValue) const
{
    vr::IVRSystem* System = GetVRSystem();
    if (!System || DeviceIndex < 0 || DeviceIndex >= static_cast<int32>(vr::k_unMaxTrackedDeviceCount))
    {
        return false;
    }

    char Buffer[128] = {};
    vr::ETrackedPropertyError PropertyError = vr::TrackedProp_Success;
    const uint32 Count = System->GetStringTrackedDeviceProperty(
        static_cast<vr::TrackedDeviceIndex_t>(DeviceIndex),
        static_cast<vr::ETrackedDeviceProperty>(Property),
        Buffer,
        sizeof(Buffer),
        &PropertyError);

    if (PropertyError != vr::TrackedProp_Success || Count <= 1)
    {
        return false;
    }

    OutValue = FString(UTF8_TO_TCHAR(Buffer));
    return true;
}

bool FOpenVRTrackerSession::TryInitialize()
{
    if (IsInitialized())
    {
        return true;
    }

    if (!OpenVRDllHandle)
    {
        OpenVRDllHandle = FPlatformProcess::GetDllHandle(TEXT("openvr_api.dll"));
        if (!OpenVRDllHandle)
        {
            const FString OpenVRDllPath = GetOpenVRDllPath();
            OpenVRDllHandle = FPlatformProcess::GetDllHandle(*OpenVRDllPath);
        }
    }

    if (!OpenVRDllHandle)
    {
        InitStatus = TEXT("OpenVR DLL not found");
        return false;
    }

    vr::EVRInitError InitError = vr::VRInitError_None;
    VRSystem = vr::VR_Init(&InitError, vr::VRApplication_Background);
    bInitialized = (InitError == vr::VRInitError_None && VRSystem != nullptr);
    InitStatus = bInitialized
        ? TEXT("OpenVR initialized")
        : FString::Printf(TEXT("OpenVR init failed: %d"), static_cast<int32>(InitError));

    return bInitialized;
}

bool FOpenVRTrackerSession::FinalizeShutdown()
{
    if (bInitialized)
    {
        vr::VR_Shutdown();
    }

    VRSystem = nullptr;
    bInitialized = false;

    if (OpenVRDllHandle)
    {
        FPlatformProcess::FreeDllHandle(OpenVRDllHandle);
        OpenVRDllHandle = nullptr;
    }

    return true;
}

FString FOpenVRTrackerSession::GetOpenVRDllPath() const
{
    return FPaths::Combine(
        FPaths::EnginePluginsDir(),
        TEXT("Experimental"),
        TEXT("LiveLinkOpenVR"),
        TEXT("Source"),
        TEXT("ThirdParty"),
        TEXT("OpenVR"),
        TEXT("OpenVRv1_5_17"),
        TEXT("bin"),
        TEXT("win64"),
        TEXT("openvr_api.dll"));
}
