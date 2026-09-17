#include "KasumiSplatImportFactory.h"
#include "KasumiSplatNaming.h"

#include "EditorFramework/AssetImportData.h"
#include "KasumiSplatAsset.h"
#include "KasumiSplatPlyParser.h"
#include "Async/MappedFileHandle.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Crc.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogKasumiSplatImport, Log, All);

#define LOCTEXT_NAMESPACE "KasumiSplatImportFactory"

namespace
{
    FKasumiSplatImportSettingsSnapshot CaptureImportSettings(const UKasumiSplatImportFactory& Factory)
    {
        FKasumiSplatImportSettingsSnapshot Result;
        Result.Profile = Factory.ImportProfile;
        Result.UnitsToCentimeters = Factory.UnitsToCentimeters;
        Result.DefaultSigmaCentimeters = Factory.DefaultSigmaCentimeters;
        Result.MaxPointCount = Factory.MaxPointCount;
        Result.bSwapYZ = Factory.bSwapYZ;
        Result.bFlipX = Factory.bFlipX;
        Result.bFlipY = Factory.bFlipY;
        Result.bFlipZ = Factory.bFlipZ;
        Result.bScaleValuesAreLogarithmic = Factory.bScaleValuesAreLogarithmic;
        Result.bOpacityValuesAreLogits = Factory.bOpacityValuesAreLogits;
        Result.bRgbValuesAreSRGB = Factory.bRgbValuesAreSRGB;
        Result.bQuaternionWFirst = Factory.bQuaternionWFirst;
        Result.bSkipInvalidPoints = Factory.bSkipInvalidPoints;
        Result.bSpatiallySortForStreaming = Factory.bSpatiallySortForStreaming;
        return Result;
    }

    void RestoreImportSettings(
        UKasumiSplatImportFactory& Factory,
        const FKasumiSplatImportSettingsSnapshot& Settings)
    {
        Factory.ImportProfile = Settings.Profile;
        Factory.UnitsToCentimeters = Settings.UnitsToCentimeters;
        Factory.DefaultSigmaCentimeters = Settings.DefaultSigmaCentimeters;
        Factory.MaxPointCount = Settings.MaxPointCount;
        Factory.bSwapYZ = Settings.bSwapYZ;
        Factory.bFlipX = Settings.bFlipX;
        Factory.bFlipY = Settings.bFlipY;
        Factory.bFlipZ = Settings.bFlipZ;
        Factory.bScaleValuesAreLogarithmic = Settings.bScaleValuesAreLogarithmic;
        Factory.bOpacityValuesAreLogits = Settings.bOpacityValuesAreLogits;
        Factory.bRgbValuesAreSRGB = Settings.bRgbValuesAreSRGB;
        Factory.bQuaternionWFirst = Settings.bQuaternionWFirst;
        Factory.bSkipInvalidPoints = Settings.bSkipInvalidPoints;
        Factory.bSpatiallySortForStreaming = Settings.bSpatiallySortForStreaming;
    }

    FString BuildImportFingerprint(
        IPlatformFile& PlatformFile,
        const FString& Filename,
        const UKasumiSplatImportFactory& Factory)
    {
        const FString FingerprintInput = FString::Printf(
            TEXT("KasumiSplat-v%d|%lld|%lld|%.17g|%.17g|%lld|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d"),
            UKasumiSplatAsset::CurrentDataVersion,
            PlatformFile.FileSize(*Filename),
            PlatformFile.GetTimeStamp(*Filename).GetTicks(),
            Factory.UnitsToCentimeters,
            Factory.DefaultSigmaCentimeters,
            Factory.MaxPointCount,
            int32(Factory.ImportProfile),
            Factory.bSwapYZ,
            Factory.bFlipX,
            Factory.bFlipY,
            Factory.bFlipZ,
            Factory.bScaleValuesAreLogarithmic,
            Factory.bOpacityValuesAreLogits,
            Factory.bRgbValuesAreSRGB,
            Factory.bQuaternionWFirst,
            Factory.bSkipInvalidPoints,
            Factory.bSpatiallySortForStreaming);
        return FString::Printf(TEXT("%08x"), FCrc::StrCrc32(*FingerprintInput));
    }
}

UKasumiSplatImportFactory::UKasumiSplatImportFactory()
{
    bCreateNew = false;
    bEditorImport = true;
    SupportedClass = UKasumiSplatAsset::StaticClass();
    Formats.Add(TEXT("ply;3D Gaussian Splat PLY"));
}

bool UKasumiSplatImportFactory::ConfigureProperties()
{
    if (IsAutomatedImport() || FApp::IsUnattended() || IsRunningCommandlet() || !GEditor)
    {
        return true;
    }

    FPropertyEditorModule& PropertyEditorModule =
        FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

    FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    DetailsViewArgs.bAllowSearch = false;
    DetailsViewArgs.bHideSelectionTip = true;
    DetailsViewArgs.bShowOptions = false;

    TSharedRef<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
    DetailsView->SetObject(this);

    TSharedRef<bool> bAccepted = MakeShared<bool>(false);
    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(LOCTEXT("WindowTitle", "KasumiSplat PLY Import Options"))
        .SizingRule(ESizingRule::UserSized)
        .ClientSize(FVector2D(560.0f, 620.0f))
        .SupportsMinimize(false)
        .SupportsMaximize(false);
    TWeakPtr<SWindow> WeakWindow = Window;

    Window->SetContent(
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("Menu.Background"))
        .Padding(8.0f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot()
                [
                    DetailsView
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .HAlign(HAlign_Right)
            .Padding(0.0f, 8.0f, 0.0f, 0.0f)
            [
                SNew(SUniformGridPanel)
                .SlotPadding(FAppStyle::GetMargin("StandardDialog.SlotPadding"))
                .MinDesiredSlotWidth(FAppStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
                .MinDesiredSlotHeight(FAppStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
                + SUniformGridPanel::Slot(0, 0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("Import", "Import"))
                    .HAlign(HAlign_Center)
                    .ContentPadding(FAppStyle::GetMargin("StandardDialog.ContentPadding"))
                    .OnClicked_Lambda([bAccepted, WeakWindow]()
                    {
                        *bAccepted = true;
                        if (const TSharedPtr<SWindow> PinnedWindow = WeakWindow.Pin())
                        {
                            PinnedWindow->RequestDestroyWindow();
                        }
                        return FReply::Handled();
                    })
                ]
                + SUniformGridPanel::Slot(1, 0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("Cancel", "Cancel"))
                    .HAlign(HAlign_Center)
                    .ContentPadding(FAppStyle::GetMargin("StandardDialog.ContentPadding"))
                    .OnClicked_Lambda([WeakWindow]()
                    {
                        if (const TSharedPtr<SWindow> PinnedWindow = WeakWindow.Pin())
                        {
                            PinnedWindow->RequestDestroyWindow();
                        }
                        return FReply::Handled();
                    })
                ]
            ]
        ]);

    GEditor->EditorAddModalWindow(Window);
    return *bAccepted;
}

bool UKasumiSplatImportFactory::FactoryCanImport(const FString& Filename)
{
    return FPaths::GetExtension(Filename).Equals(TEXT("ply"), ESearchCase::IgnoreCase);
}

bool UKasumiSplatImportFactory::ImportFile(
    UKasumiSplatAsset& Asset,
    const FString& Filename,
    FFeedbackContext* Warn,
    bool* bOutOperationCanceled) const
{
    if (bOutOperationCanceled) *bOutOperationCanceled = false;
    FScopedSlowTask SlowTask(100.0f, LOCTEXT("ImportProgress", "Importing Gaussian splats..."));
    if (!FApp::IsUnattended() && !IsRunningCommandlet()) SlowTask.MakeDialog(true);
    double LastProgress = 0.0;

    FKasumiSplatPlyImportOptions Options;
    Options.UnitsToCentimeters = UnitsToCentimeters;
    Options.DefaultSigmaCentimeters = DefaultSigmaCentimeters;
    Options.MaxPointCount = MaxPointCount;
    Options.Profile = ImportProfile;
    Options.bSwapYZ = bSwapYZ;
    Options.bFlipX = bFlipX;
    Options.bFlipY = bFlipY;
    Options.bFlipZ = bFlipZ;
    Options.bScaleValuesAreLogarithmic = bScaleValuesAreLogarithmic;
    Options.bOpacityValuesAreLogits = bOpacityValuesAreLogits;
    Options.bRgbValuesAreSRGB = bRgbValuesAreSRGB;
    Options.bQuaternionWFirst = bQuaternionWFirst;
    Options.bSkipInvalidPoints = bSkipInvalidPoints;
    Options.ProgressCallback = [&SlowTask, &LastProgress, bOutOperationCanceled](int64 Processed, int64 Total)
    {
        const double Progress = Total > 0 ? double(Processed) / double(Total) : 1.0;
        const double Delta = FMath::Max(Progress - LastProgress, 0.0);
        if (Delta > 0.0)
        {
            SlowTask.EnterProgressFrame(float(Delta * 90.0), FText::Format(
                LOCTEXT("DecodeProgress", "Decoding splat {0} of {1}"),
                FText::AsNumber(Processed),
                FText::AsNumber(Total)));
            LastProgress = Progress;
        }
        const bool bContinue = !SlowTask.ShouldCancel();
        if (!bContinue && bOutOperationCanceled) *bOutOperationCanceled = true;
        return bContinue;
    };

    FKasumiSplatPlyImportResult Result;
    FString Error;
    bool bParsed = false;
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    const FString ImportFingerprint = BuildImportFingerprint(PlatformFile, Filename, *this);
    if (Asset.GetPointCount() > 0 && Asset.ImportFingerprint == ImportFingerprint)
    {
        UE_LOG(LogKasumiSplatImport, Display, TEXT("Skipped unchanged KasumiSplat source '%s'."), *Filename);
        return true;
    }
    FOpenMappedResult MappedResult = PlatformFile.OpenMappedEx2(*Filename);
    TUniquePtr<IMappedFileHandle> MappedHandle = MappedResult.HasError() ? nullptr : MappedResult.StealValue();
    TUniquePtr<IMappedFileRegion> MappedRegion;
    if (MappedHandle)
    {
        MappedRegion.Reset(MappedHandle->MapRegion());
        if (MappedRegion)
        {
            bParsed = FKasumiSplatPlyParser::Parse(
                static_cast<const uint8*>(MappedRegion->GetMappedPtr()),
                int64(MappedRegion->GetMappedSize()),
                Options,
                Result,
                Error);
        }
    }
    else
    {
        TArray<uint8> Bytes;
        if (FFileHelper::LoadFileToArray(Bytes, *Filename))
        {
            bParsed = FKasumiSplatPlyParser::Parse(Bytes, Options, Result, Error);
        }
        else
        {
            Error = TEXT("The file could not be read.");
        }
    }
    if (!bParsed)
    {
        if (bOutOperationCanceled && *bOutOperationCanceled)
        {
            UE_LOG(LogKasumiSplatImport, Display, TEXT("KasumiSplat import cancelled for '%s'."), *Filename);
            return false;
        }
        Warn->Logf(ELogVerbosity::Error, TEXT("KasumiSplat failed to import '%s': %s"), *Filename, *Error);
        return false;
    }

    SlowTask.EnterProgressFrame(10.0f, LOCTEXT("BuildAssetProgress", "Building streaming chunks..."));
    if (SlowTask.ShouldCancel())
    {
        if (bOutOperationCanceled) *bOutOperationCanceled = true;
        return false;
    }
    Asset.Modify();
    const int32 ImportedPointCount = Result.Points.Num();
    Asset.SetImportedPoints(
        MoveTemp(Result.Points),
        Result.Encoding,
        UnitsToCentimeters,
        MoveTemp(Result.HigherOrderSH),
        Result.HigherOrderSHCoefficientsPerPoint,
        bSpatiallySortForStreaming,
        Result.LocalToSHDirection,
        Result.ResolvedProfile);
    Asset.ImportFingerprint = ImportFingerprint;
    Asset.ImportSettings = CaptureImportSettings(*this);
#if WITH_EDITORONLY_DATA
    if (Asset.AssetImportData) Asset.AssetImportData->Update(Filename);
#endif
    Asset.MarkPackageDirty();
    UE_LOG(
        LogKasumiSplatImport,
        Display,
        TEXT("Imported %d splats from '%s' with profile %d into %d streaming chunks (%lld skipped). Bounds: %s. Higher SH coefficients: %d per point."),
        ImportedPointCount,
        *Filename,
        int32(Result.ResolvedProfile),
        Asset.Chunks.Num(),
        Result.SkippedPointCount,
        *Asset.LocalBounds.ToString(),
        Result.HigherOrderSHCoefficientsPerPoint);
    return true;
}

UObject* UKasumiSplatImportFactory::FactoryCreateFile(
    UClass* InClass,
    UObject* InParent,
    FName InName,
    EObjectFlags Flags,
    const FString& Filename,
    const TCHAR* Parms,
    FFeedbackContext* Warn,
    bool& bOutOperationCanceled)
{
    bOutOperationCanceled = false;
    const FName AssetName = KasumiSplatNaming::MakeAssetName(InName);
    UPackage* SourcePackage = InParent ? InParent->GetOutermost() : nullptr;
    if (!SourcePackage)
    {
        UE_LOG(LogKasumiSplatImport, Error, TEXT("Cannot import '%s' without a destination package."), *Filename);
        return nullptr;
    }

    const FString AssetPackageName = KasumiSplatNaming::MakeAssetPackageName(SourcePackage->GetName(), AssetName);
    UPackage* AssetPackage = SourcePackage;
    if (SourcePackage->GetName() != AssetPackageName)
    {
        AssetPackage = FindPackage(nullptr, *AssetPackageName);
        if (!AssetPackage && FPackageName::DoesPackageExist(AssetPackageName))
        {
            AssetPackage = LoadPackage(nullptr, *AssetPackageName, LOAD_None);
        }
        if (!AssetPackage)
        {
            AssetPackage = CreatePackage(*AssetPackageName);
            AssetPackage->SetAssetAccessSpecifier(SourcePackage->GetAssetAccessSpecifier());
        }
    }

    AssetPackage->FullyLoad();
    UObject* ExistingObject = StaticFindObjectFast(UObject::StaticClass(), AssetPackage, AssetName);
    UKasumiSplatAsset* Asset = Cast<UKasumiSplatAsset>(ExistingObject);
    if (ExistingObject && !Asset)
    {
        UE_LOG(
            LogKasumiSplatImport,
            Error,
            TEXT("Cannot import '%s': '%s.%s' is already used by %s."),
            *Filename,
            *AssetPackageName,
            *AssetName.ToString(),
            *ExistingObject->GetClass()->GetName());
        return nullptr;
    }
    if (!Asset)
    {
        Asset = NewObject<UKasumiSplatAsset>(AssetPackage, InClass, AssetName, Flags);
    }
    return Asset && ImportFile(*Asset, Filename, Warn, &bOutOperationCanceled) ? Asset : nullptr;
}

bool UKasumiSplatImportFactory::CanReimport(UObject* Object, TArray<FString>& OutFilenames)
{
    const UKasumiSplatAsset* Asset = Cast<UKasumiSplatAsset>(Object);
#if WITH_EDITORONLY_DATA
    if (Asset && Asset->AssetImportData)
    {
        Asset->AssetImportData->ExtractFilenames(OutFilenames);
        return !OutFilenames.IsEmpty();
    }
#endif
    return false;
}

void UKasumiSplatImportFactory::SetReimportPaths(UObject* Object, const TArray<FString>& NewReimportPaths)
{
    UKasumiSplatAsset* Asset = Cast<UKasumiSplatAsset>(Object);
#if WITH_EDITORONLY_DATA
    if (Asset && Asset->AssetImportData && NewReimportPaths.Num() == 1)
    {
        Asset->AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
    }
#endif
}

EReimportResult::Type UKasumiSplatImportFactory::Reimport(UObject* Object)
{
    UKasumiSplatAsset* Asset = Cast<UKasumiSplatAsset>(Object);
    if (!Asset) return EReimportResult::Failed;

    if (Asset->DataVersion >= 6)
    {
        RestoreImportSettings(*this, Asset->ImportSettings);
    }

    if (!ConfigureProperties()) return EReimportResult::Cancelled;

    TArray<FString> Filenames;
    if (!CanReimport(Asset, Filenames) || Filenames.Num() != 1) return EReimportResult::Failed;
    bool bCanceled = false;
    if (ImportFile(*Asset, Filenames[0], GWarn, &bCanceled)) return EReimportResult::Succeeded;
    return bCanceled ? EReimportResult::Cancelled : EReimportResult::Failed;
}

#undef LOCTEXT_NAMESPACE
