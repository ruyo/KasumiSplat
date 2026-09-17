#include "KasumiSplatPlyParser.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKasumiSplatAsciiPlyTest,
    "KasumiSplat.Import.PLYAscii",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatAsciiPlyTest::RunTest(const FString& Parameters)
{
    const FString Text =
        TEXT("ply\n")
        TEXT("format ascii 1.0\n")
        TEXT("element vertex 2\n")
        TEXT("property float x\n")
        TEXT("property float y\n")
        TEXT("property float z\n")
        TEXT("property float f_dc_0\n")
        TEXT("property float f_dc_1\n")
        TEXT("property float f_dc_2\n")
        TEXT("property float opacity\n")
        TEXT("property float scale_0\n")
        TEXT("property float scale_1\n")
        TEXT("property float scale_2\n")
        TEXT("property float rot_0\n")
        TEXT("property float rot_1\n")
        TEXT("property float rot_2\n")
        TEXT("property float rot_3\n")
        TEXT("end_header\n")
        TEXT("1 2 3 4 0 0 0 0 0 0 1 0 0 0\n")
        TEXT("-1 -2 -3 0 0 0 0 0 0 0 1 0 0 0\n");

    FTCHARToUTF8 Utf8(*Text);
    TArray<uint8> Bytes;
    Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());

    FKasumiSplatPlyImportOptions Options;
    Options.UnitsToCentimeters = 1.0;
    Options.Profile = EKasumiSplatImportProfile::AutoDetect;
    FKasumiSplatPlyImportResult Result;
    FString Error;
    TestTrue(TEXT("Pointer and 64-bit size PLY path parses"), FKasumiSplatPlyParser::Parse(Bytes.GetData(), int64(Bytes.Num()), Options, Result, Error));
    TestEqual(TEXT("Point count"), Result.Points.Num(), 2);
    TestEqual(TEXT("Gaussian properties select the standard profile"),
        Result.ResolvedProfile, EKasumiSplatImportProfile::Standard3DGS);
    if (Result.Points.Num() == 2)
    {
        TestTrue(TEXT("Position is preserved"), Result.Points[0].Position.Equals(FVector(1, 2, 3), 1.e-6));
        TestTrue(TEXT("Log scale is exponentiated"), Result.Points[0].Sigma.Equals(FVector(1), 1.e-6));
        TestTrue(TEXT("Quaternion order is w,x,y,z"), Result.Points[0].Rotation.Equals(FQuat::Identity, 1.e-6));
        TestTrue(TEXT("Opacity is sigmoid decoded"), FMath::IsNearlyEqual(Result.Points[0].Color.A, 0.5f));
        TestTrue(TEXT("SH DC preserves values above display white"), Result.Points[0].Color.R > 1.0f);
        TestEqual(TEXT("Stable id"), Result.Points[1].StableId, 1);
    }
    if (!Error.IsEmpty()) AddError(Error);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKasumiSplatBinaryPlyTest,
    "KasumiSplat.Import.PLYBinaryLittleEndian",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatBinaryPlyTest::RunTest(const FString& Parameters)
{
    const FString Header =
        TEXT("ply\n")
        TEXT("format binary_little_endian 1.0\n")
        TEXT("element vertex 1\n")
        TEXT("property float x\n")
        TEXT("property float y\n")
        TEXT("property float z\n")
        TEXT("end_header\n");
    FTCHARToUTF8 Utf8(*Header);
    TArray<uint8> Bytes;
    Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
    for (const float Value : { 4.0f, 5.0f, 6.0f })
    {
        Bytes.Append(reinterpret_cast<const uint8*>(&Value), sizeof(Value));
    }

    FKasumiSplatPlyImportOptions Options;
    Options.UnitsToCentimeters = 2.0;
    FKasumiSplatPlyImportResult Result;
    FString Error;
    TestTrue(TEXT("Binary PLY parses"), FKasumiSplatPlyParser::Parse(Bytes, Options, Result, Error));
    TestEqual(TEXT("Point count"), Result.Points.Num(), 1);
    if (Result.Points.Num() == 1)
    {
        TestTrue(TEXT("Unit conversion is applied"), Result.Points[0].Position.Equals(FVector(8, 10, 12), 1.e-6));
        TestTrue(TEXT("Missing scale uses fallback"), Result.Points[0].Sigma.Equals(FVector(1), 1.e-6));
    }
    if (!Error.IsEmpty()) AddError(Error);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKasumiSplatCompatibilityPlyTest,
    "KasumiSplat.Import.PLYCoordinatesAndHigherSH",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatCompatibilityPlyTest::RunTest(const FString& Parameters)
{
    const FString Text =
        TEXT("ply\nformat ascii 1.0\nelement vertex 1\n")
        TEXT("property float x\nproperty float y\nproperty float z\n")
        TEXT("property float f_rest_0\nproperty float f_rest_1\nend_header\n")
        TEXT("1 2 3 0.25 -0.5\n");
    FTCHARToUTF8 Utf8(*Text);
    TArray<uint8> Bytes;
    Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());

    FKasumiSplatPlyImportOptions Options;
    Options.UnitsToCentimeters = 10.0;
    Options.bSwapYZ = true;
    Options.bFlipX = true;
    FKasumiSplatPlyImportResult Result;
    FString Error;
    TestTrue(TEXT("Compatibility PLY parses"), FKasumiSplatPlyParser::Parse(Bytes, Options, Result, Error));
    TestTrue(TEXT("Axis conversion is applied"), Result.Points[0].Position.Equals(FVector(-10, 30, 20), 1.e-6));
    const FVector4 CustomSHDirection = Result.LocalToSHDirection.TransformVector(FVector(-1, 3, 2));
    TestTrue(TEXT("Custom SH direction transform reverses axis conversion"),
        FVector(CustomSHDirection.X, CustomSHDirection.Y, CustomSHDirection.Z).Equals(FVector(1, 2, 3), 1.e-6));
    TestEqual(TEXT("SH coefficient count"), Result.HigherOrderSHCoefficientsPerPoint, 2);
    TestEqual(TEXT("SH payload count"), Result.HigherOrderSH.Num(), 2);
    TestTrue(TEXT("SH values are retained"), FMath::IsNearlyEqual(Result.HigherOrderSH[1], -0.5f));

    Options = FKasumiSplatPlyImportOptions();
    Options.UnitsToCentimeters = 10.0;
    Options.bUseLumaAICoordinates = true;
    TestTrue(TEXT("Luma AI coordinate PLY parses"), FKasumiSplatPlyParser::Parse(Bytes, Options, Result, Error));
    TestTrue(TEXT("Luma AI PLY RDF basis converts to Unreal"), Result.Points[0].Position.Equals(FVector(30, 10, -20), 1.e-6));
    const FVector4 LumaSHDirection = Result.LocalToSHDirection.TransformVector(FVector(3, 1, -2));
    TestTrue(TEXT("Luma AI SH direction transform returns source direction"),
        FVector(LumaSHDirection.X, LumaSHDirection.Y, LumaSHDirection.Z).Equals(FVector(1, 2, 3), 1.e-6));
    if (!Error.IsEmpty()) AddError(Error);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKasumiSplatBigEndianAndBasisTest,
    "KasumiSplat.Import.PLYBigEndianAndQuaternionBasis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatBigEndianAndBasisTest::RunTest(const FString& Parameters)
{
    const FString Header =
        TEXT("ply\nformat binary_big_endian 1.0\nelement vertex 1\n")
        TEXT("property float x\nproperty float y\nproperty float z\n")
        TEXT("property float scale_x\nproperty float scale_y\nproperty float scale_z\n")
        TEXT("property float qw\nproperty float qx\nproperty float qy\nproperty float qz\n")
        TEXT("property uchar red\nproperty uchar green\nproperty uchar blue\nproperty uchar alpha\n")
        TEXT("end_header\n");
    FTCHARToUTF8 Utf8(*Header);
    TArray<uint8> Bytes;
    Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
    for (const float Value : {1.0f, 2.0f, 3.0f, 0.0f, FMath::Loge(2.0f), FMath::Loge(3.0f), 1.0f, 0.0f, 0.0f, 0.0f})
    {
        uint8 Native[sizeof(float)];
        FMemory::Memcpy(Native, &Value, sizeof(float));
        for (int32 ByteIndex = sizeof(float) - 1; ByteIndex >= 0; --ByteIndex) Bytes.Add(Native[ByteIndex]);
    }
    Bytes.Append({255, 128, 0, 64});

    FKasumiSplatPlyImportOptions Options;
    Options.UnitsToCentimeters = 1.0;
    Options.bSwapYZ = true;
    Options.bFlipX = true;
    FKasumiSplatPlyImportResult Result;
    FString Error;
    TestTrue(TEXT("Big-endian PLY parses"), FKasumiSplatPlyParser::Parse(Bytes, Options, Result, Error));
    TestEqual(TEXT("Encoding is retained"), Result.Encoding, FString(TEXT("PLY binary_big_endian 1.0")));
    if (Result.Points.Num() == 1)
    {
        const FKasumiSplatPoint& Point = Result.Points[0];
        TestTrue(TEXT("Position basis transforms"), Point.Position.Equals(FVector(-1, 3, 2), 1.e-5));
        TestTrue(TEXT("Local sigma order is retained"), Point.Sigma.Equals(FVector(1, 2, 3), 1.e-5));
        TestTrue(TEXT("Rotation X axis transforms"), Point.Rotation.GetAxisX().Equals(FVector(-1, 0, 0), 1.e-5));
        TestTrue(TEXT("Rotation Y axis transforms"), Point.Rotation.GetAxisY().Equals(FVector(0, 0, 1), 1.e-5));
        TestTrue(TEXT("Linear alpha is normalized"), FMath::IsNearlyEqual(Point.Color.A, 64.0f / 255.0f, 1.e-5f));
    }
    if (!Error.IsEmpty()) AddError(Error);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKasumiSplatSkipInvalidPlyTest,
    "KasumiSplat.Import.PLYSkipInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatSkipInvalidPlyTest::RunTest(const FString& Parameters)
{
    const FString Text =
        TEXT("ply\nformat ascii 1.0\nelement vertex 2\n")
        TEXT("property float x\nproperty float y\nproperty float z\nend_header\n")
        TEXT("1 2 3\nnan 5 6\n");
    FTCHARToUTF8 Utf8(*Text);
    TArray<uint8> Bytes;
    Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
    FKasumiSplatPlyImportOptions Options;
    Options.bSkipInvalidPoints = true;
    FKasumiSplatPlyImportResult Result;
    FString Error;
    TestTrue(TEXT("Invalid vertex can be skipped"), FKasumiSplatPlyParser::Parse(Bytes, Options, Result, Error));
    TestEqual(TEXT("One valid point remains"), Result.Points.Num(), 1);
    TestEqual(TEXT("Skipped count is reported"), Result.SkippedPointCount, int64(1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKasumiSplatCancelPlyTest,
    "KasumiSplat.Import.PLYCancel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatCancelPlyTest::RunTest(const FString& Parameters)
{
    const FString Text =
        TEXT("ply\nformat ascii 1.0\nelement vertex 1\n")
        TEXT("property float x\nproperty float y\nproperty float z\nend_header\n")
        TEXT("1 2 3\n");
    FTCHARToUTF8 Utf8(*Text);
    TArray<uint8> Bytes;
    Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());

    FKasumiSplatPlyImportOptions Options;
    Options.ProgressCallback = [](int64, int64) { return false; };
    FKasumiSplatPlyImportResult Result;
    FString Error;
    TestFalse(TEXT("Parser stops when progress callback cancels"),
        FKasumiSplatPlyParser::Parse(Bytes, Options, Result, Error));
    TestEqual(TEXT("Cancelled parse retains no points"), Result.Points.Num(), 0);
    TestEqual(TEXT("Cancellation has a clear error"), Error, FString(TEXT("Import cancelled.")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKasumiSplatImportProfileTest,
    "KasumiSplat.Import.Profiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatImportProfileTest::RunTest(const FString& Parameters)
{
    const FString Text =
        TEXT("ply\nformat ascii 1.0\nelement vertex 1\n")
        TEXT("property float x\nproperty float y\nproperty float z\n")
        TEXT("property float scale_0\nproperty float scale_1\nproperty float scale_2\n")
        TEXT("property float rot_0\nproperty float rot_1\nproperty float rot_2\nproperty float rot_3\n")
        TEXT("property uchar red\nproperty uchar green\nproperty uchar blue\nproperty float opacity\n")
        TEXT("end_header\n1 2 3 2 3 4 1 0 0 0 128 64 255 0.25\n");
    FTCHARToUTF8 Utf8(*Text);
    TArray<uint8> Bytes;
    Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());

    FKasumiSplatPlyImportOptions Options;
    Options.UnitsToCentimeters = 1.0;
    Options.Profile = EKasumiSplatImportProfile::RGBPointCloud;
    FKasumiSplatPlyImportResult Result;
    FString Error;
    TestTrue(TEXT("RGB point-cloud profile parses"),
        FKasumiSplatPlyParser::Parse(Bytes, Options, Result, Error));
    TestEqual(TEXT("Resolved profile is recorded"),
        Result.ResolvedProfile, EKasumiSplatImportProfile::RGBPointCloud);
    if (Result.Points.Num() == 1)
    {
        const FKasumiSplatPoint& Point = Result.Points[0];
        TestTrue(TEXT("Linear scale is not exponentiated"), Point.Sigma.Equals(FVector(2, 3, 4), 1.e-6));
        TestTrue(TEXT("Linear opacity is not passed through a sigmoid"),
            FMath::IsNearlyEqual(Point.Color.A, 0.25f, 1.e-6f));
        TestTrue(TEXT("Point-cloud RGB is converted from sRGB to linear"),
            FMath::IsNearlyEqual(Point.Color.R, 0.21586f, 1.e-4f));
    }

    const FString LumaText =
        TEXT("ply\nformat ascii 1.0\ncomment exported by Luma AI\nelement vertex 1\n")
        TEXT("property float x\nproperty float y\nproperty float z\nend_header\n1 2 3\n");
    FTCHARToUTF8 LumaUtf8(*LumaText);
    Bytes.Reset();
    Bytes.Append(reinterpret_cast<const uint8*>(LumaUtf8.Get()), LumaUtf8.Length());
    Options = FKasumiSplatPlyImportOptions();
    Options.UnitsToCentimeters = 1.0;
    Options.Profile = EKasumiSplatImportProfile::AutoDetect;
    TestTrue(TEXT("Auto-detected Luma profile parses"),
        FKasumiSplatPlyParser::Parse(Bytes, Options, Result, Error));
    TestEqual(TEXT("Luma comment selects the Luma profile"),
        Result.ResolvedProfile, EKasumiSplatImportProfile::LumaAI);
    TestTrue(TEXT("Auto-detected Luma coordinates are converted"),
        Result.Points.Num() == 1 && Result.Points[0].Position.Equals(FVector(3, 1, -2), 1.e-6));
    return true;
}

#endif
