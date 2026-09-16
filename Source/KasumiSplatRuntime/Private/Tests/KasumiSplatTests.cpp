#include "Misc/AutomationTest.h"
#include "KasumiSplatRenderer.h"
#include "KasumiSplatConfiguration.h"
#include "KasumiSplatSortPolicy.h"
#include "KasumiSplatStreaming.h"
#include "KasumiSplatAsset.h"
#include "KasumiSplatComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    struct FKasumiAsyncChunkTestState
    {
        bool bComplete = false;
        bool bSuccess = false;
        int32 PointCount = 0;
        int32 FirstStableId = INDEX_NONE;
    };
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FWaitForKasumiAsyncChunk,
    TSharedPtr<FKasumiAsyncChunkTestState>, State,
    FAutomationTestBase*, Test);

bool FWaitForKasumiAsyncChunk::Update()
{
    if (!State->bComplete) return false;
    Test->TestTrue(TEXT("Async chunk load succeeds"), State->bSuccess);
    Test->TestEqual(TEXT("Async load returns one selected point"), State->PointCount, 1);
    Test->TestEqual(TEXT("Async load retains source identity"), State->FirstStableId, 65536);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKasumiSplatEffectTest, "KasumiSplat.Effects.DeterministicSourcePosition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatEffectTest::RunTest(const FString& Parameters)
{
    FKasumiSplatPoint Point;
    Point.StableId = 13;
    Point.Position = FVector(10, 20, 30);
    FKasumiSplatStyle Style;
    TestEqual(TEXT("Neutral progress preserves source"), KasumiSplat::EvaluatePosition(Point, Style), Point.Position);
    Style.Progress = 0.75f;
    const FVector Forward = KasumiSplat::EvaluatePosition(Point, Style);
    Style.Progress = 1.0f;
    KasumiSplat::EvaluatePosition(Point, Style);
    Style.Progress = 0.75f;
    TestEqual(TEXT("Scrubbing is deterministic"), KasumiSplat::EvaluatePosition(Point, Style), Forward);
    TestTrue(TEXT("Effect stays within declared displacement"), FVector::Dist(Forward, Point.Position) <= Style.Displacement + 0.001);
    Style.Progress = 0.0f;
    TestEqual(TEXT("Returning to zero restores source"), KasumiSplat::EvaluatePosition(Point, Style), Point.Position);
    TestEqual(TEXT("Source remains immutable"), Point.Position, FVector(10, 20, 30));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKasumiSplatPackedStorageTest, "KasumiSplat.Storage.PackedRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatPackedStorageTest::RunTest(const FString& Parameters)
{
    UKasumiSplatAsset* Asset = NewObject<UKasumiSplatAsset>();
    TArray<FKasumiSplatPoint> Source;
    FKasumiSplatPoint& Point = Source.AddDefaulted_GetRef();
    Point.StableId = 123456789;
    Point.Position = FVector(10, 20, 30);
    Point.Sigma = FVector(1, 2, 3);
    Point.Color = FLinearColor(0.1f, 0.2f, 0.3f, 0.4f);
    TArray<float> SourceSH = {0.25f, -0.5f, 0.75f};
    Asset->SetImportedPoints(MoveTemp(Source), TEXT("Test"), 1.0, MoveTemp(SourceSH), 3);

    TArray<FKasumiSplatPoint> Loaded;
    TestTrue(TEXT("Packed points load"), Asset->LoadPoints(Loaded));
    TestEqual(TEXT("Packed point count"), Loaded.Num(), 1);
    TestEqual(TEXT("Stable ID survives packing"), Loaded[0].StableId, 123456789);
    TestTrue(TEXT("Position survives packing"), Loaded[0].Position.Equals(FVector(10, 20, 30), 1.e-5));
    TestEqual(TEXT("One chunk is generated"), Asset->Chunks.Num(), 1);
    TArray<float> LoadedSH;
    TestTrue(TEXT("SH bulk data loads"), Asset->LoadHigherOrderSH(LoadedSH));
    TestEqual(TEXT("SH value count"), LoadedSH.Num(), 3);
    if (LoadedSH.Num() == 3) TestTrue(TEXT("SH value survives packing"), FMath::IsNearlyEqual(LoadedSH[1], -0.5f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKasumiSplatChunkRangeTest, "KasumiSplat.Storage.ChunkRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatChunkRangeTest::RunTest(const FString& Parameters)
{
    UKasumiSplatAsset* Asset = NewObject<UKasumiSplatAsset>();
    TArray<FKasumiSplatPoint> Source;
    Source.SetNum(65537);
    for (int32 Index = 0; Index < Source.Num(); ++Index)
    {
        Source[Index].StableId = Index;
        Source[Index].Position = FVector(Index, 0, 0);
    }
    Asset->SetImportedPoints(MoveTemp(Source), TEXT("ChunkTest"), 1.0);
    TestEqual(TEXT("Two chunks are generated"), Asset->Chunks.Num(), 2);

    TArray<FKasumiSplatPoint> Loaded;
    TestTrue(TEXT("Second chunk loads"), Asset->LoadPointChunks({1}, Loaded));
    TestEqual(TEXT("Only one point is resident"), Loaded.Num(), 1);
    if (Loaded.Num() == 1)
    {
        TestEqual(TEXT("Chunk source identity is retained"), Loaded[0].StableId, 65536);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKasumiSplatAsyncChunkTest, "KasumiSplat.Storage.AsyncChunk",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatAsyncChunkTest::RunTest(const FString& Parameters)
{
    UKasumiSplatAsset* Asset = NewObject<UKasumiSplatAsset>();
    TArray<FKasumiSplatPoint> Source;
    Source.SetNum(65537);
    for (int32 Index = 0; Index < Source.Num(); ++Index)
    {
        Source[Index].StableId = Index;
    }
    Asset->SetImportedPoints(MoveTemp(Source), TEXT("AsyncChunkTest"), 1.0);

    const TSharedPtr<FKasumiAsyncChunkTestState> State = MakeShared<FKasumiAsyncChunkTestState>();
    Asset->LoadPointChunksAsync({1},
        [State](bool bSuccess, TArray<FKasumiSplatPoint>&& Points, TArray<float>&&, int32)
    {
        State->bSuccess = bSuccess;
        State->PointCount = Points.Num();
        State->FirstStableId = Points.IsEmpty() ? INDEX_NONE : Points[0].StableId;
        State->bComplete = true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForKasumiAsyncChunk(State, this));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKasumiSplatSpatialChunkTest, "KasumiSplat.Storage.SpatialMortonChunks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatSpatialChunkTest::RunTest(const FString& Parameters)
{
    UKasumiSplatAsset* Asset = NewObject<UKasumiSplatAsset>();
    TArray<FKasumiSplatPoint> Source;
    Source.SetNum(131072);
    for (int32 Index = 0; Index < Source.Num(); ++Index)
    {
        Source[Index].StableId = Index;
        Source[Index].Position = FVector(Index % 2 == 0 ? -10000.0 : 10000.0, 0.0, 0.0);
    }
    Asset->SetImportedPoints(MoveTemp(Source), TEXT("SpatialChunkTest"), 1.0);
    TestTrue(TEXT("Asset records spatial sort"), Asset->bSpatiallySorted);
    TestEqual(TEXT("Two spatial chunks are generated"), Asset->Chunks.Num(), 2);
    if (Asset->Chunks.Num() == 2)
    {
        TestTrue(TEXT("First chunk is spatially compact"), Asset->Chunks[0].LocalBounds.GetSize().X < 100.0);
        TestTrue(TEXT("Second chunk is spatially compact"), Asset->Chunks[1].LocalBounds.GetSize().X < 100.0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKasumiSplatQualityPresetTest, "KasumiSplat.Settings.QualityPresets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatQualityPresetTest::RunTest(const FString& Parameters)
{
    UKasumiSplatComponent* Component = NewObject<UKasumiSplatComponent>();
    TestEqual(TEXT("Automatic GPU sorting is the default"), Component->SortMode, EKasumiSplatSortMode::Auto);
    TestEqual(TEXT("Default tile size"), Component->TileSizePixels, 32);
    TestEqual(TEXT("Default tile overlap budget"), Component->MaxTilesPerSplat, 64);
    TestEqual(TEXT("Default point budget matches High quality"), Component->MaxVisibleSplats, 750000);
    TestEqual(TEXT("Needle suppression defaults to a conservative limit"), Component->MaxAnisotropy, 32.0f);
    TestEqual(TEXT("Oversized source splats are limited by default"), Component->MaxSplatSigmaCentimeters, 100.0f);
    Component->ApplyQualityPreset(EKasumiSplatQuality::Low);
    TestEqual(TEXT("Low point budget"), Component->MaxVisibleSplats, 100000);
    Component->ApplyQualityPreset(EKasumiSplatQuality::Cinematic);
    TestEqual(TEXT("Cinematic point budget"), Component->MaxVisibleSplats, 2000000);
    TestTrue(TEXT("Cinematic preserves small splats"), Component->MinProjectedRadiusPixels == 0.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKasumiSplatAppearancePresetTest, "KasumiSplat.Settings.AppearancePresets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatAppearancePresetTest::RunTest(const FString& Parameters)
{
    UKasumiSplatComponent* Component = NewObject<UKasumiSplatComponent>();
    TestEqual(TEXT("Reference appearance is the default"), Component->AppearancePreset, EKasumiSplatAppearancePreset::Reference);
    TestEqual(TEXT("Reference exposure is neutral"), Component->Appearance.ExposureEV, 0.0f);
    TestEqual(TEXT("Reference saturation is neutral"), Component->Appearance.Saturation, 1.0f);
    TestEqual(TEXT("Reference contrast is neutral"), Component->Appearance.Contrast, 1.0f);
    TestEqual(TEXT("Reference SH strength is neutral"), Component->Appearance.SHStrength, 1.0f);
    TestEqual(TEXT("Reference opacity density is neutral"), Component->Appearance.OpacityDensity, 1.0f);

    Component->ApplyAppearancePreset(EKasumiSplatAppearancePreset::Vivid);
    TestEqual(TEXT("Vivid exposure"), Component->Appearance.ExposureEV, 0.25f);
    TestEqual(TEXT("Vivid saturation"), Component->Appearance.Saturation, 1.2f);
    TestEqual(TEXT("Vivid contrast"), Component->Appearance.Contrast, 1.08f);
    TestEqual(TEXT("Vivid SH strength"), Component->Appearance.SHStrength, 1.1f);
    TestEqual(TEXT("Vivid opacity density"), Component->Appearance.OpacityDensity, 1.2f);

    Component->ApplyAppearancePreset(EKasumiSplatAppearancePreset::Dense);
    TestEqual(TEXT("Dense opacity density"), Component->Appearance.OpacityDensity, 1.6f);

    Component->Appearance.ExposureEV = 0.75f;
    Component->ApplyAppearancePreset(EKasumiSplatAppearancePreset::Custom);
    TestEqual(TEXT("Custom preserves authored values"), Component->Appearance.ExposureEV, 0.75f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKasumiSplatSortPolicyTest, "KasumiSplat.Rendering.SortPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatSortPolicyTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Auto uses exact sorting at the threshold"),
        ResolveKasumiSplatRequestedSortMode(EKasumiSplatSortMode::Auto, KasumiAutoTiledThreshold),
        EKasumiSplatSortMode::GlobalRadix);
    TestEqual(TEXT("Auto uses tiled sorting above the threshold"),
        ResolveKasumiSplatRequestedSortMode(EKasumiSplatSortMode::Auto, KasumiAutoTiledThreshold + 1),
        EKasumiSplatSortMode::Tiled);
    TestEqual(TEXT("An explicit bucket request is preserved"),
        ResolveKasumiSplatRequestedSortMode(EKasumiSplatSortMode::Bucket, 1),
        EKasumiSplatSortMode::Bucket);
    TestEqual(TEXT("Auto keeps tiled mode and uses a same-frame overflow fallback"),
        ResolveKasumiSplatSortModeForView(EKasumiSplatSortMode::Auto, 1000000, 100, 32, 64, 64.0f, 256),
        EKasumiSplatSortMode::Tiled);
    TestEqual(TEXT("Explicit tiled mode keeps the dynamic overflow path"),
        ResolveKasumiSplatSortModeForView(EKasumiSplatSortMode::Tiled, 1000000, 100, 32, 64, 64.0f, 256),
        EKasumiSplatSortMode::Tiled);
    TestEqual(TEXT("Tiled mode remains available under safe limits"),
        ResolveKasumiSplatSortModeForView(EKasumiSplatSortMode::Tiled, 10000, 1000, 32, 64, 64.0f, 256),
        EKasumiSplatSortMode::Tiled);
    TestEqual(TEXT("Large footprints are handled by the GPU overflow fallback"),
        ResolveKasumiSplatSortModeForView(EKasumiSplatSortMode::Tiled, 10000, 1000, 32, 64, 1024.0f, 256),
        EKasumiSplatSortMode::Tiled);
    TestEqual(TEXT("An unsupported tile count falls back before allocation"),
        ResolveKasumiSplatSortModeForView(EKasumiSplatSortMode::Tiled, 10000, 70000, 32, 64, 64.0f, 256),
        EKasumiSplatSortMode::GlobalRadix);
    TestEqual(TEXT("A 64 pixel radius spans the conservative five by five tile footprint"),
        GetKasumiMaximumTileOverlap(32, 64.0f), 25u);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKasumiSplatTransitionSnapshotTest, "KasumiSplat.Streaming.TransitionSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatTransitionSnapshotTest::RunTest(const FString& Parameters)
{
    TArray<FKasumiSplatPoint> PreviousPoints;
    PreviousPoints.SetNum(2);
    PreviousPoints[0].StableId = 1;
    PreviousPoints[1].StableId = 2;
    TArray<FKasumiSplatPoint> NewPoints;
    NewPoints.SetNum(2);
    NewPoints[0].StableId = 2;
    NewPoints[1].StableId = 3;

    FKasumiSplatResidentSnapshot Previous;
    Previous.Points = MakeShared<const TArray<FKasumiSplatPoint>, ESPMode::ThreadSafe>(PreviousPoints);
    Previous.HigherOrderSH = MakeShared<const TArray<float>, ESPMode::ThreadSafe>(TArray<float>{10.0f, 20.0f});
    Previous.HigherOrderSHCoefficientsPerPoint = 1;
    FKasumiSplatResidentSnapshot NewTarget;
    NewTarget.Points = MakeShared<const TArray<FKasumiSplatPoint>, ESPMode::ThreadSafe>(NewPoints);
    NewTarget.HigherOrderSH = MakeShared<const TArray<float>, ESPMode::ThreadSafe>(TArray<float>{200.0f, 300.0f});
    NewTarget.HigherOrderSHCoefficientsPerPoint = 1;
    const FKasumiSplatResidentSnapshot Merged =
        BuildKasumiSplatTransitionSnapshot(Previous, NewTarget);

    TestTrue(TEXT("Merged points are available"), Merged.Points.IsValid());
    TestTrue(TEXT("Transition classes are available"), Merged.TransitionClasses.IsValid());
    if (!Merged.Points.IsValid() || !Merged.TransitionClasses.IsValid()) return false;
    TestEqual(TEXT("Common points are not duplicated"), Merged.Points->Num(), 3);
    TestEqual(TEXT("Common point uses the new target value"), (*Merged.Points)[0].StableId, 2);
    TestEqual(TEXT("Common point remains fully visible"), (*Merged.TransitionClasses)[0],
        uint8(EKasumiSplatTransitionClass::Stable));
    TestEqual(TEXT("New point fades in"), (*Merged.TransitionClasses)[1],
        uint8(EKasumiSplatTransitionClass::Entering));
    TestEqual(TEXT("Old point fades out"), (*Merged.TransitionClasses)[2],
        uint8(EKasumiSplatTransitionClass::Leaving));
    TestTrue(TEXT("SH remains aligned with the merged point order"), Merged.HigherOrderSH.IsValid());
    if (Merged.HigherOrderSH.IsValid())
    {
        TestEqual(TEXT("Common point uses target SH"), (*Merged.HigherOrderSH)[0], 200.0f);
        TestEqual(TEXT("Entering point uses target SH"), (*Merged.HigherOrderSH)[1], 300.0f);
        TestEqual(TEXT("Leaving point keeps previous SH"), (*Merged.HigherOrderSH)[2], 10.0f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKasumiSplatRenderConfigurationTest, "KasumiSplat.Rendering.Configuration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatRenderConfigurationTest::RunTest(const FString& Parameters)
{
    FKasumiSplatPacket Packet;
    Packet.EffectLayers.SetNum(6);
    Packet.TileSizePixels = 31;
    Packet.MaxTilesPerSplat = 0;
    Packet.MaxVisibleSplats = 0;
    Packet.MinProjectedRadiusPixels = -1.0f;
    Packet.MaxProjectedRadiusPixels = 0.0f;
    Packet.AntialiasingFilterVariance = -1.0f;
    Packet.Appearance.ExposureEV = 20.0f;
    Packet.Appearance.Saturation = -1.0f;
    Packet.Appearance.Contrast = 8.0f;
    Packet.Appearance.SHStrength = -1.0f;
    Packet.Appearance.OpacityDensity = 20.0f;
    Packet.ExternalMaskExtent = FVector(-10.0, 0.0, 20.0);
    Packet.ExternalMaskStrength = 2.0f;
    KasumiSplatConfig::SanitizeRenderPacket(Packet);

    TestEqual(TEXT("Effect layers respect the shader capacity"), Packet.EffectLayers.Num(), 4);
    TestEqual(TEXT("Tile size is aligned"), Packet.TileSizePixels, 32u);
    TestEqual(TEXT("At least one tile is emitted"), Packet.MaxTilesPerSplat, 1u);
    TestEqual(TEXT("At least one point is considered"), Packet.MaxVisibleSplats, 1u);
    TestEqual(TEXT("Negative projected radii are removed"), Packet.MinProjectedRadiusPixels, 0.0f);
    TestEqual(TEXT("Projected radius maximum remains usable"), Packet.MaxProjectedRadiusPixels, 1.0f);
    TestEqual(TEXT("Antialiasing variance is non-negative"), Packet.AntialiasingFilterVariance, 0.0f);
    TestEqual(TEXT("Appearance exposure is bounded"), Packet.Appearance.ExposureEV, 8.0f);
    TestEqual(TEXT("Appearance saturation is non-negative"), Packet.Appearance.Saturation, 0.0f);
    TestEqual(TEXT("Appearance contrast is bounded"), Packet.Appearance.Contrast, 4.0f);
    TestEqual(TEXT("Appearance SH strength is non-negative"), Packet.Appearance.SHStrength, 0.0f);
    TestEqual(TEXT("Appearance opacity density is bounded"), Packet.Appearance.OpacityDensity, 8.0f);
    TestEqual(TEXT("Mask strength is normalized"), Packet.ExternalMaskStrength, 1.0f);
    TestTrue(TEXT("Mask extents are positive"), Packet.ExternalMaskExtent.GetMin() >= 0.001);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKasumiSplatLODTest, "KasumiSplat.Streaming.DeterministicLOD",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatLODTest::RunTest(const FString& Parameters)
{
    TArray<FKasumiSplatPoint> Points;
    TArray<float> HigherOrderSH;
    for (int32 Index = 0; Index < 6; ++Index)
    {
        FKasumiSplatPoint& Point = Points.AddDefaulted_GetRef();
        Point.StableId = Index;
        Point.Position = FVector(Index * 2.0, 0.0, 0.0);
        Point.Sigma = FVector(1.0);
        Point.Color = FLinearColor(Index / 5.0f, 0.25f, 0.5f, 0.5f);
        HigherOrderSH.Add(float(Index + 10));
    }
    const TArray<FKasumiSplatPoint> OriginalPoints = Points;
    const TArray<float> OriginalSH = HigherOrderSH;
    ApplyKasumiSplatLOD(2, 1, Points, HigherOrderSH);

    TestEqual(TEXT("Stride two produces three merged Gaussians"), Points.Num(), 3);
    if (Points.Num() == 3)
    {
        TestTrue(TEXT("Merged mean preserves the source distribution"),
            Points[1].Position.Equals(FVector(5.0, 0.0, 0.0), 1.e-5));
        TestTrue(TEXT("Between-point variance expands the merged Gaussian"),
            FMath::IsNearlyEqual(Points[1].Sigma.X, FMath::Sqrt(2.0), 1.e-4));
        TestTrue(TEXT("Independent opacity is accumulated"),
            FMath::IsNearlyEqual(Points[1].Color.A, 0.75f, 1.e-5f));
    }
    TestTrue(TEXT("SH coefficients are opacity-weighted with the merge"),
        HigherOrderSH.Num() == 3 && FMath::IsNearlyEqual(HigherOrderSH[1], 12.5f, 1.e-5f));

    TArray<FKasumiSplatPoint> RepeatedPoints = OriginalPoints;
    TArray<float> RepeatedSH = OriginalSH;
    ApplyKasumiSplatLOD(2, 1, RepeatedPoints, RepeatedSH);
    TestEqual(TEXT("Merged Stable IDs are deterministic"), RepeatedPoints[1].StableId, Points[1].StableId);
    TestTrue(TEXT("Merged covariance is deterministic"), RepeatedPoints[1].Sigma.Equals(Points[1].Sigma, 1.e-6));

    TArray<FKasumiSplatPoint> OddPoints;
    OddPoints.SetNum(3);
    OddPoints[2].StableId = 42;
    TArray<float> NoSH;
    ApplyKasumiSplatLOD(2, 0, OddPoints, NoSH);
    TestEqual(TEXT("A final singleton keeps its source identity"), OddPoints[1].StableId, 42);
    TestEqual(TEXT("LOD does not increase inside the upper hysteresis band"),
        ResolveKasumiSplatLODStride(1050.0, 1000.0f, 4, 1), 1);
    TestEqual(TEXT("LOD increases after the upper hysteresis band"),
        ResolveKasumiSplatLODStride(1110.0, 1000.0f, 4, 1), 2);
    TestEqual(TEXT("LOD does not decrease inside the lower hysteresis band"),
        ResolveKasumiSplatLODStride(950.0, 1000.0f, 4, 2), 2);
    TestEqual(TEXT("LOD decreases after the lower hysteresis band"),
        ResolveKasumiSplatLODStride(890.0, 1000.0f, 4, 2), 1);
    return true;
}
#endif
