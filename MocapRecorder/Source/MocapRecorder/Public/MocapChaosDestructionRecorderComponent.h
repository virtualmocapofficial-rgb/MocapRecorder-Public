#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MocapRecorderTypes.h"
#include "MocapChaosDestructionRecorderComponent.generated.h"

UCLASS(ClassGroup = (Mocap), meta = (BlueprintSpawnableComponent))
class MOCAPRECORDER_API UMocapChaosDestructionRecorderComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UMocapChaosDestructionRecorderComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Chaos")
    float SampleRate = 60.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Chaos")
    bool bEachGeometryCollectionExportsAsOwnModel = true;

    UFUNCTION(BlueprintCallable, Category = "Mocap|Chaos")
    void StartChaosRecording();

    UFUNCTION(BlueprintCallable, Category = "Mocap|Chaos")
    void StopChaosRecording();

    UFUNCTION(BlueprintCallable, Category = "Mocap|Chaos")
    void RegisterChaosCollection(UPrimitiveComponent* GeometryCollectionComponent, FName ExportModelName = NAME_None);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Chaos")
    void SubmitChaosPieceTransform(UPrimitiveComponent* GeometryCollectionComponent, int32 PieceIndex, FName PieceName, const FTransform& WorldTransform, bool bVisible = true);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Chaos")
    void SampleChaosFrame();

    UFUNCTION(BlueprintCallable, Category = "Mocap|Chaos")
    FString BuildChaosManifestJson() const;

    UFUNCTION(BlueprintCallable, Category = "Mocap|Chaos")
    bool SaveChaosManifestJson(const FString& AbsoluteFilePath) const;

    UFUNCTION(BlueprintPure, Category = "Mocap|Chaos")
    const TArray<FMocapChaosCollectionTake>& GetRecordedChaosCollections() const { return Collections; }

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    int32 FindOrAddCollection(UPrimitiveComponent* GeometryCollectionComponent, FName ExportModelName);
    int32 FindOrAddPiece(FMocapChaosCollectionTake& Collection, int32 PieceIndex, FName PieceName);

    UPROPERTY(Transient)
    TArray<TObjectPtr<UPrimitiveComponent>> RegisteredCollections;

    UPROPERTY(Transient)
    TArray<FMocapChaosCollectionTake> Collections;

    bool bIsRecording = false;
    float TimeAccumulator = 0.f;
    float TimeFromStart = 0.f;
};
