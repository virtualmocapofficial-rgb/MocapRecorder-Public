#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MocapRecorderTypes.h"
#include "MocapFoleyAudioRecorderComponent.generated.h"

class UAudioComponent;

UCLASS(ClassGroup = (Mocap), meta = (BlueprintSpawnableComponent))
class MOCAPRECORDER_API UMocapFoleyAudioRecorderComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UMocapFoleyAudioRecorderComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Foley")
    float SampleRate = 60.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Foley")
    float MaxEmitterDistance = 5000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Foley")
    float BassDistanceFalloff = 1800.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Foley")
    int32 PlayerIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Foley")
    bool bAutoDiscoverActiveAudioComponents = true;

    UFUNCTION(BlueprintCallable, Category = "Mocap|Foley")
    void StartFoleyRecording();

    UFUNCTION(BlueprintCallable, Category = "Mocap|Foley")
    void StopFoleyRecording();

    UFUNCTION(BlueprintCallable, Category = "Mocap|Foley")
    void RegisterSoundEmitter(UAudioComponent* AudioComponent, FName TrackName = NAME_None);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Foley")
    void SampleFoleyFrame();

    UFUNCTION(BlueprintCallable, Category = "Mocap|Foley")
    FString BuildFoleyManifestJson() const;

    UFUNCTION(BlueprintCallable, Category = "Mocap|Foley")
    bool SaveFoleyManifestJson(const FString& AbsoluteFilePath) const;

    UFUNCTION(BlueprintPure, Category = "Mocap|Foley")
    const TArray<FMocapFoleyAudioTrack>& GetRecordedFoleyTracks() const { return Tracks; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    int32 FindOrAddTrack(UAudioComponent* AudioComponent, FName TrackName);
    void AutoDiscoverEmitters();

    UPROPERTY(Transient)
    TArray<TObjectPtr<UAudioComponent>> RegisteredEmitters;

    UPROPERTY(Transient)
    TArray<FMocapFoleyAudioTrack> Tracks;

    bool bIsRecording = false;
    float TimeAccumulator = 0.f;
    float TimeFromStart = 0.f;
};
