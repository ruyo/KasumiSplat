#include "KasumiSplatSpzParser.h"
#include "KasumiSplatImportFactory.h"
#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "load-spz.h"
#include "coordinate-system-adobe.h"
THIRD_PARTY_INCLUDES_END

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKasumiSplatSpzV4Test,
    "KasumiSplat.Import.SPZV4",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatSpzV4Test::RunTest(const FString& Parameters)
{
    spz::GaussianCloud Source;
    Source.numPoints = 1;
    Source.shDegree = 1;
    Source.antialiased = true;
    Source.positions = {1.0f, 2.0f, 3.0f};
    Source.scales = {0.0f, FMath::Loge(2.0f), FMath::Loge(3.0f)};
    Source.rotations = {0.0f, 0.0f, 0.0f, 1.0f};
    Source.alphas = {0.0f};
    Source.colors = {0.0f, 0.25f, -0.25f};
    Source.sh = {
        0.125f, 0.25f, 0.375f,
        -0.125f, -0.25f, -0.375f,
        0.5f, 0.625f, 0.75f};
    const std::shared_ptr<spz::SpzExtensionCoordinateSystemAdobe> CoordinateExtension =
        std::make_shared<spz::SpzExtensionCoordinateSystemAdobe>();
    CoordinateExtension->coordinateSystem = spz::CoordinateSystem::RDF;
    Source.extensions.push_back(CoordinateExtension);

    spz::PackOptions PackOptions;
    PackOptions.from = spz::CoordinateSystem::RDF;
    PackOptions.version = 4;
    std::vector<uint8_t> Encoded;
    TestTrue(TEXT("Reference library creates SPZ v4 data"), spz::saveSpz(Source, PackOptions, &Encoded));
    if (Encoded.empty())
    {
        return false;
    }

    FKasumiSplatPlyImportOptions Options;
    Options.UnitsToCentimeters = 2.0;
    FKasumiSplatPlyImportResult Result;
    FString Error;
    TestTrue(TEXT("SPZ v4 parses"), FKasumiSplatSpzParser::Parse(
        Encoded.data(), int64(Encoded.size()), Options, Result, Error));
    TestEqual(TEXT("Point count"), Result.Points.Num(), 1);
    TestEqual(TEXT("SPZ resolves to Standard 3DGS conventions"),
        Result.ResolvedProfile, EKasumiSplatImportProfile::Standard3DGS);
    TestEqual(TEXT("Degree-one SH value count"), Result.HigherOrderSHCoefficientsPerPoint, 9);
    TestTrue(TEXT("Encoding records v4"), Result.Encoding.Contains(TEXT("SPZ v4")));
    TestTrue(TEXT("Encoding records antialiasing metadata"), Result.Encoding.Contains(TEXT("antialiased")));
    if (Result.Points.Num() == 1)
    {
        const FKasumiSplatPoint& Point = Result.Points[0];
        TestTrue(TEXT("Position and units round trip"), Point.Position.Equals(FVector(2, 4, 6), 0.01));
        TestTrue(TEXT("Log scales decode within SPZ's 8-bit quantization error"),
            Point.Sigma.Equals(FVector(2, 4, 6), 0.2));
        TestTrue(TEXT("Quaternion round trips"), Point.Rotation.Equals(FQuat::Identity, 0.01));
        TestTrue(TEXT("Logit alpha decodes"), FMath::IsNearlyEqual(Point.Color.A, 0.5f, 0.01f));
    }
    if (Result.HigherOrderSH.Num() == 9)
    {
        // The parser converts coefficient-major RGB into channel-major f_rest order.
        TestTrue(TEXT("SH red channel is contiguous"),
            Result.HigherOrderSH[0] > 0.0f &&
            Result.HigherOrderSH[1] < 0.0f &&
            Result.HigherOrderSH[2] > 0.0f);
        TestTrue(TEXT("SH green channel follows red"), Result.HigherOrderSH[3] > 0.0f);
    }
    if (!Error.IsEmpty()) AddError(Error);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKasumiSplatSpzLegacyTest,
    "KasumiSplat.Import.SPZLegacy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatSpzLegacyTest::RunTest(const FString& Parameters)
{
    spz::GaussianCloud Source;
    Source.numPoints = 1;
    Source.shDegree = 0;
    Source.positions = {-1.0f, 0.5f, 2.0f};
    Source.scales = {0.0f, 0.0f, 0.0f};
    Source.rotations = {0.0f, 0.0f, 0.0f, 1.0f};
    Source.alphas = {0.0f};
    Source.colors = {0.0f, 0.0f, 0.0f};

    spz::PackOptions PackOptions;
    PackOptions.from = spz::CoordinateSystem::RDF;
    PackOptions.version = 3;
    std::vector<uint8_t> Encoded;
    TestTrue(TEXT("Reference library creates legacy gzip SPZ data"),
        spz::saveSpz(Source, PackOptions, &Encoded));

    FKasumiSplatPlyImportOptions Options;
    Options.UnitsToCentimeters = 1.0;
    FKasumiSplatPlyImportResult Result;
    FString Error;
    TestTrue(TEXT("Legacy SPZ parses"), !Encoded.empty() && FKasumiSplatSpzParser::Parse(
        Encoded.data(), int64(Encoded.size()), Options, Result, Error));
    TestEqual(TEXT("Legacy point count"), Result.Points.Num(), 1);
    TestTrue(TEXT("Encoding records the legacy container"), Result.Encoding.Contains(TEXT("legacy")));
    if (!Error.IsEmpty()) AddError(Error);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKasumiSplatSpzValidationTest,
    "KasumiSplat.Import.SPZValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatSpzValidationTest::RunTest(const FString& Parameters)
{
    UKasumiSplatImportFactory* Factory = NewObject<UKasumiSplatImportFactory>();
    TestTrue(TEXT("Factory advertises lowercase SPZ"), Factory->FactoryCanImport(TEXT("capture.spz")));
    TestTrue(TEXT("Factory advertises uppercase SPZ"), Factory->FactoryCanImport(TEXT("capture.SPZ")));

    const uint8 Invalid[] = {'N', 'O', 'P', 'E'};
    FKasumiSplatPlyImportOptions Options;
    FKasumiSplatPlyImportResult Result;
    FString Error;
    TestFalse(TEXT("Invalid SPZ signature is rejected"),
        FKasumiSplatSpzParser::Parse(Invalid, UE_ARRAY_COUNT(Invalid), Options, Result, Error));
    TestTrue(TEXT("Invalid signature reports a useful error"), Error.Contains(TEXT("signature")));
    return true;
}

#endif
