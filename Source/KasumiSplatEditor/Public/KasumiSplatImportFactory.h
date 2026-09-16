#pragma once

#include "CoreMinimal.h"
#include "EditorReimportHandler.h"
#include "Factories/Factory.h"
#include "KasumiSplatTypes.h"
#include "KasumiSplatImportFactory.generated.h"

UCLASS()
class UKasumiSplatImportFactory : public UFactory, public FReimportHandler
{
    GENERATED_BODY()

public:
    UKasumiSplatImportFactory();

    UPROPERTY(EditAnywhere, Category="Gaussian Splat Import", meta=(ClampMin="0.000001"))
    double UnitsToCentimeters = 100.0;

    UPROPERTY(EditAnywhere, Category="Gaussian Splat Import", meta=(ClampMin="0.001", Units="cm"))
    double DefaultSigmaCentimeters = 1.0;

    UPROPERTY(EditAnywhere, Category="Gaussian Splat Import", meta=(ClampMin="1"))
    int64 MaxPointCount = 20000000;

    UPROPERTY(EditAnywhere, Category="Gaussian Splat Import", meta=(DisplayPriority="0"))
    EKasumiSplatImportProfile ImportProfile = EKasumiSplatImportProfile::AutoDetect;

    UPROPERTY(EditAnywhere, Category="Gaussian Splat Import|Custom", meta=(EditCondition="ImportProfile == EKasumiSplatImportProfile::Custom"))
    bool bSwapYZ = false;

    UPROPERTY(EditAnywhere, Category="Gaussian Splat Import|Custom", meta=(EditCondition="ImportProfile == EKasumiSplatImportProfile::Custom"))
    bool bFlipX = false;

    UPROPERTY(EditAnywhere, Category="Gaussian Splat Import|Custom", meta=(EditCondition="ImportProfile == EKasumiSplatImportProfile::Custom"))
    bool bFlipY = false;

    UPROPERTY(EditAnywhere, Category="Gaussian Splat Import|Custom", meta=(EditCondition="ImportProfile == EKasumiSplatImportProfile::Custom"))
    bool bFlipZ = false;

    UPROPERTY(EditAnywhere, Category="Gaussian Splat Import|Custom", meta=(EditCondition="ImportProfile == EKasumiSplatImportProfile::Custom"))
    bool bScaleValuesAreLogarithmic = true;

    UPROPERTY(EditAnywhere, Category="Gaussian Splat Import|Custom", meta=(EditCondition="ImportProfile == EKasumiSplatImportProfile::Custom"))
    bool bOpacityValuesAreLogits = true;

    UPROPERTY(EditAnywhere, Category="Gaussian Splat Import|Custom", meta=(EditCondition="ImportProfile == EKasumiSplatImportProfile::Custom"))
    bool bRgbValuesAreSRGB = false;

    UPROPERTY(EditAnywhere, Category="Gaussian Splat Import|Custom", meta=(EditCondition="ImportProfile == EKasumiSplatImportProfile::Custom"))
    bool bQuaternionWFirst = true;

    /** Continue importing when an individual vertex has invalid position data. */
    UPROPERTY(EditAnywhere, Category="Gaussian Splat Import|Compatibility")
    bool bSkipInvalidPoints = false;

    /** Reorder points into spatial Morton order so nearby chunks contain nearby splats. */
    UPROPERTY(EditAnywhere, Category="Gaussian Splat Import|Streaming")
    bool bSpatiallySortForStreaming = true;

    virtual bool ConfigureProperties() override;
    virtual bool FactoryCanImport(const FString& Filename) override;
    virtual UObject* FactoryCreateFile(
        UClass* InClass,
        UObject* InParent,
        FName InName,
        EObjectFlags Flags,
        const FString& Filename,
        const TCHAR* Parms,
        FFeedbackContext* Warn,
        bool& bOutOperationCanceled) override;

    virtual bool CanReimport(UObject* Object, TArray<FString>& OutFilenames) override;
    virtual void SetReimportPaths(UObject* Object, const TArray<FString>& NewReimportPaths) override;
    virtual EReimportResult::Type Reimport(UObject* Object) override;

private:
    bool ImportFile(
        class UKasumiSplatAsset& Asset,
        const FString& Filename,
        FFeedbackContext* Warn,
        bool* bOutOperationCanceled = nullptr) const;
};
