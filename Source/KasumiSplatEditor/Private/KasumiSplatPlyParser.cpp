#include "KasumiSplatPlyParser.h"

namespace
{
    enum class EScalarType : uint8
    {
        Int8,
        UInt8,
        Int16,
        UInt16,
        Int32,
        UInt32,
        Float32,
        Float64,
        Invalid
    };

    struct FPlyProperty
    {
        EScalarType Type = EScalarType::Invalid;
        FString Name;
    };

    EScalarType ParseType(const FString& Name)
    {
        if (Name == TEXT("char") || Name == TEXT("int8")) return EScalarType::Int8;
        if (Name == TEXT("uchar") || Name == TEXT("uint8")) return EScalarType::UInt8;
        if (Name == TEXT("short") || Name == TEXT("int16")) return EScalarType::Int16;
        if (Name == TEXT("ushort") || Name == TEXT("uint16")) return EScalarType::UInt16;
        if (Name == TEXT("int") || Name == TEXT("int32")) return EScalarType::Int32;
        if (Name == TEXT("uint") || Name == TEXT("uint32")) return EScalarType::UInt32;
        if (Name == TEXT("float") || Name == TEXT("float32")) return EScalarType::Float32;
        if (Name == TEXT("double") || Name == TEXT("float64")) return EScalarType::Float64;
        return EScalarType::Invalid;
    }

    int32 ScalarSize(EScalarType Type)
    {
        switch (Type)
        {
        case EScalarType::Int8:
        case EScalarType::UInt8: return 1;
        case EScalarType::Int16:
        case EScalarType::UInt16: return 2;
        case EScalarType::Int32:
        case EScalarType::UInt32:
        case EScalarType::Float32: return 4;
        case EScalarType::Float64: return 8;
        default: return 0;
        }
    }

    bool ReadHeaderLine(const uint8* Data, int64 Size, int64& Offset, FString& OutLine)
    {
        if (!Data || Offset >= Size) return false;
        const int64 Start = Offset;
        while (Offset < Size && Data[Offset] != '\n') ++Offset;
        int64 End = Offset;
        if (Offset < Size) ++Offset;
        if (End > Start && Data[End - 1] == '\r') --End;
        if (End - Start > MAX_int32) return false;
        FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Data + Start), int32(End - Start));
        OutLine = FString(Converted.Length(), Converted.Get());
        return true;
    }

    class FAsciiScanner
    {
    public:
        FAsciiScanner(const uint8* InCursor, const uint8* InEnd) : Cursor(InCursor), End(InEnd) {}

        bool Read(double& OutValue)
        {
            while (Cursor < End && FCharAnsi::IsWhitespace(ANSICHAR(*Cursor))) ++Cursor;
            if (Cursor >= End) return false;

            double Sign = 1.0;
            if (*Cursor == '-' || *Cursor == '+')
            {
                if (*Cursor++ == '-') Sign = -1.0;
            }

            if (Cursor < End && (*Cursor == 'n' || *Cursor == 'N' || *Cursor == 'i' || *Cursor == 'I'))
            {
                while (Cursor < End && !FCharAnsi::IsWhitespace(ANSICHAR(*Cursor))) ++Cursor;
                OutValue = NAN;
                return true;
            }

            double Value = 0.0;
            bool bHasDigit = false;
            while (Cursor < End && *Cursor >= '0' && *Cursor <= '9')
            {
                bHasDigit = true;
                Value = Value * 10.0 + double(*Cursor++ - '0');
            }
            if (Cursor < End && *Cursor == '.')
            {
                ++Cursor;
                double Place = 0.1;
                while (Cursor < End && *Cursor >= '0' && *Cursor <= '9')
                {
                    bHasDigit = true;
                    Value += double(*Cursor++ - '0') * Place;
                    Place *= 0.1;
                }
            }
            if (!bHasDigit) return false;

            int32 Exponent = 0;
            if (Cursor < End && (*Cursor == 'e' || *Cursor == 'E'))
            {
                ++Cursor;
                int32 ExponentSign = 1;
                if (Cursor < End && (*Cursor == '-' || *Cursor == '+'))
                {
                    if (*Cursor++ == '-') ExponentSign = -1;
                }
                bool bHasExponentDigit = false;
                while (Cursor < End && *Cursor >= '0' && *Cursor <= '9')
                {
                    bHasExponentDigit = true;
                    Exponent = Exponent * 10 + int32(*Cursor++ - '0');
                }
                if (!bHasExponentDigit) return false;
                Exponent *= ExponentSign;
            }
            OutValue = Sign * Value * FMath::Pow(10.0, double(Exponent));
            return true;
        }

    private:
        const uint8* Cursor;
        const uint8* End;
    };

    template<typename T>
    bool ReadPod(const uint8*& Cursor, const uint8* End, bool bSwapByteOrder, double& OutValue)
    {
        if (End - Cursor < int64(sizeof(T))) return false;
        T Value;
        if (bSwapByteOrder && sizeof(T) > 1)
        {
            uint8 Reversed[sizeof(T)];
            for (int32 Index = 0; Index < int32(sizeof(T)); ++Index) Reversed[Index] = Cursor[sizeof(T) - 1 - Index];
            FMemory::Memcpy(&Value, Reversed, sizeof(T));
        }
        else
        {
            FMemory::Memcpy(&Value, Cursor, sizeof(T));
        }
        Cursor += sizeof(T);
        OutValue = double(Value);
        return true;
    }

    bool ReadBinaryScalar(EScalarType Type, const uint8*& Cursor, const uint8* End, bool bSwapByteOrder, double& OutValue)
    {
        switch (Type)
        {
        case EScalarType::Int8: return ReadPod<int8>(Cursor, End, false, OutValue);
        case EScalarType::UInt8: return ReadPod<uint8>(Cursor, End, false, OutValue);
        case EScalarType::Int16: return ReadPod<int16>(Cursor, End, bSwapByteOrder, OutValue);
        case EScalarType::UInt16: return ReadPod<uint16>(Cursor, End, bSwapByteOrder, OutValue);
        case EScalarType::Int32: return ReadPod<int32>(Cursor, End, bSwapByteOrder, OutValue);
        case EScalarType::UInt32: return ReadPod<uint32>(Cursor, End, bSwapByteOrder, OutValue);
        case EScalarType::Float32: return ReadPod<float>(Cursor, End, bSwapByteOrder, OutValue);
        case EScalarType::Float64: return ReadPod<double>(Cursor, End, bSwapByteOrder, OutValue);
        default: return false;
        }
    }

    struct FRawPoint
    {
        double X = NAN, Y = NAN, Z = NAN;
        double Scale[3] = { NAN, NAN, NAN };
        double Rotation[4] = { NAN, NAN, NAN, NAN };
        double Dc[3] = { NAN, NAN, NAN };
        double Rgb[3] = { NAN, NAN, NAN };
        double Opacity = NAN;
        bool bLinearOpacity = false;
        bool bIntegerOpacity = false;
        bool bIntegerRgb = false;
        bool bExplicitQuaternionNames = false;
        double Rest[45];

        FRawPoint()
        {
            for (double& Value : Rest) Value = NAN;
        }
    };

    void Assign(FRawPoint& Point, const FString& Name, EScalarType Type, double Value)
    {
        const bool bIntegerType = Type == EScalarType::Int8 || Type == EScalarType::UInt8 ||
            Type == EScalarType::Int16 || Type == EScalarType::UInt16 ||
            Type == EScalarType::Int32 || Type == EScalarType::UInt32;
        if (Name == TEXT("x")) Point.X = Value;
        else if (Name == TEXT("y")) Point.Y = Value;
        else if (Name == TEXT("z")) Point.Z = Value;
        else if (Name == TEXT("scale_0") || Name == TEXT("scale_x")) Point.Scale[0] = Value;
        else if (Name == TEXT("scale_1") || Name == TEXT("scale_y")) Point.Scale[1] = Value;
        else if (Name == TEXT("scale_2") || Name == TEXT("scale_z")) Point.Scale[2] = Value;
        else if (Name == TEXT("rot_0")) Point.Rotation[0] = Value;
        else if (Name == TEXT("rot_1")) Point.Rotation[1] = Value;
        else if (Name == TEXT("rot_2")) Point.Rotation[2] = Value;
        else if (Name == TEXT("rot_3")) Point.Rotation[3] = Value;
        else if (Name == TEXT("qw") || Name == TEXT("q_w")) { Point.Rotation[0] = Value; Point.bExplicitQuaternionNames = true; }
        else if (Name == TEXT("qx") || Name == TEXT("q_x")) { Point.Rotation[1] = Value; Point.bExplicitQuaternionNames = true; }
        else if (Name == TEXT("qy") || Name == TEXT("q_y")) { Point.Rotation[2] = Value; Point.bExplicitQuaternionNames = true; }
        else if (Name == TEXT("qz") || Name == TEXT("q_z")) { Point.Rotation[3] = Value; Point.bExplicitQuaternionNames = true; }
        else if (Name == TEXT("f_dc_0")) Point.Dc[0] = Value;
        else if (Name == TEXT("f_dc_1")) Point.Dc[1] = Value;
        else if (Name == TEXT("f_dc_2")) Point.Dc[2] = Value;
        else if (Name == TEXT("red") || Name == TEXT("r") || Name == TEXT("diffuse_red")) { Point.Rgb[0] = Value; Point.bIntegerRgb |= bIntegerType; }
        else if (Name == TEXT("green") || Name == TEXT("g") || Name == TEXT("diffuse_green")) { Point.Rgb[1] = Value; Point.bIntegerRgb |= bIntegerType; }
        else if (Name == TEXT("blue") || Name == TEXT("b") || Name == TEXT("diffuse_blue")) { Point.Rgb[2] = Value; Point.bIntegerRgb |= bIntegerType; }
        else if (Name == TEXT("opacity")) Point.Opacity = Value;
        else if (Name == TEXT("alpha") || Name == TEXT("a"))
        {
            Point.Opacity = Value;
            Point.bLinearOpacity = true;
            Point.bIntegerOpacity = bIntegerType;
        }
        else if (Name.StartsWith(TEXT("f_rest_")))
        {
            const int32 Index = FCString::Atoi(*Name.RightChop(7));
            if (Index >= 0 && Index < UE_ARRAY_COUNT(Point.Rest)) Point.Rest[Index] = Value;
        }
    }

    bool IsComplete(const double* Values, int32 Count)
    {
        for (int32 Index = 0; Index < Count; ++Index)
        {
            if (!FMath::IsFinite(Values[Index])) return false;
        }
        return true;
    }

    bool HasProperty(const TArray<FPlyProperty>& Properties, const TCHAR* Name)
    {
        return Properties.ContainsByPredicate([Name](const FPlyProperty& Property)
        {
            return Property.Name == Name;
        });
    }

    EKasumiSplatImportProfile ResolveProfile(
        EKasumiSplatImportProfile Requested,
        const TArray<FPlyProperty>& Properties,
        const FString& HeaderHints)
    {
        if (Requested != EKasumiSplatImportProfile::AutoDetect)
        {
            return Requested;
        }
        if (HeaderHints.Contains(TEXT("luma"), ESearchCase::IgnoreCase))
        {
            return EKasumiSplatImportProfile::LumaAI;
        }
        const bool bHasGaussianAttributes =
            HasProperty(Properties, TEXT("f_dc_0")) ||
            HasProperty(Properties, TEXT("scale_0")) ||
            HasProperty(Properties, TEXT("rot_0")) ||
            HasProperty(Properties, TEXT("opacity"));
        return bHasGaussianAttributes
            ? EKasumiSplatImportProfile::Standard3DGS
            : EKasumiSplatImportProfile::RGBPointCloud;
    }

    FKasumiSplatPlyImportOptions BuildEffectiveOptions(
        const FKasumiSplatPlyImportOptions& Options,
        EKasumiSplatImportProfile ResolvedProfile)
    {
        FKasumiSplatPlyImportOptions Result = Options;
        Result.Profile = ResolvedProfile;
        if (ResolvedProfile == EKasumiSplatImportProfile::Custom)
        {
            return Result;
        }

        Result.bUseLumaAICoordinates = ResolvedProfile == EKasumiSplatImportProfile::LumaAI;
        Result.bSwapYZ = false;
        Result.bFlipX = false;
        Result.bFlipY = false;
        Result.bFlipZ = false;
        Result.bScaleValuesAreLogarithmic = ResolvedProfile != EKasumiSplatImportProfile::RGBPointCloud;
        Result.bOpacityValuesAreLogits = ResolvedProfile != EKasumiSplatImportProfile::RGBPointCloud;
        Result.bRgbValuesAreSRGB = ResolvedProfile == EKasumiSplatImportProfile::RGBPointCloud;
        Result.bQuaternionWFirst = true;
        return Result;
    }

    double SRGBToLinear(double Value, double Denominator)
    {
        const double SRGB = FMath::Clamp(Value / Denominator, 0.0, 1.0);
        return SRGB <= 0.04045
            ? SRGB / 12.92
            : FMath::Pow((SRGB + 0.055) / 1.055, 2.4);
    }

    FVector TransformCoordinateVector(const FVector& Value, const FKasumiSplatPlyImportOptions& Options)
    {
        if (Options.bUseLumaAICoordinates)
        {
            // Luma's Gaussian PLY follows the common PLY basis: +X right,
            // +Y down, +Z forward. Unreal uses +X forward, +Y right, +Z up.
            return FVector(Value.Z, Value.X, -Value.Y);
        }
        FVector Result = Value;
        if (Options.bSwapYZ) Swap(Result.Y, Result.Z);
        Result.X *= Options.bFlipX ? -1.0 : 1.0;
        Result.Y *= Options.bFlipY ? -1.0 : 1.0;
        Result.Z *= Options.bFlipZ ? -1.0 : 1.0;
        return Result;
    }

    FMatrix BuildLocalToSHDirection(const FKasumiSplatPlyImportOptions& Options)
    {
        const FVector LocalX = TransformCoordinateVector(FVector::ForwardVector, Options);
        const FVector LocalY = TransformCoordinateVector(FVector::RightVector, Options);
        const FVector LocalZ = TransformCoordinateVector(FVector::UpVector, Options);
        FMatrix SourceToLocal = FMatrix::Identity;
        SourceToLocal.SetAxes(&LocalX, &LocalY, &LocalZ);
        return SourceToLocal.GetTransposed();
    }

    FQuat TransformCoordinateRotation(const FQuat& Rotation, const FKasumiSplatPlyImportOptions& Options)
    {
        FVector AxisX = TransformCoordinateVector(Rotation.GetAxisX(), Options).GetSafeNormal();
        FVector AxisY = TransformCoordinateVector(Rotation.GetAxisY(), Options).GetSafeNormal();
        FVector AxisZ = TransformCoordinateVector(Rotation.GetAxisZ(), Options).GetSafeNormal();
        const bool bReflection = Options.bUseLumaAICoordinates ||
            (Options.bSwapYZ ^ Options.bFlipX ^ Options.bFlipY ^ Options.bFlipZ);
        if (bReflection) AxisX *= -1.0;
        FMatrix Matrix = FMatrix::Identity;
        Matrix.SetAxes(&AxisX, &AxisY, &AxisZ);
        return FQuat(Matrix).GetNormalized();
    }

    bool ConvertPoint(
        const FRawPoint& Raw,
        int32 StableId,
        const FKasumiSplatPlyImportOptions& Options,
        FKasumiSplatPoint& OutPoint)
    {
        if (!FMath::IsFinite(Raw.X) || !FMath::IsFinite(Raw.Y) || !FMath::IsFinite(Raw.Z)) return false;

        OutPoint.StableId = StableId;
        OutPoint.Position = TransformCoordinateVector(FVector(Raw.X, Raw.Y, Raw.Z), Options) * Options.UnitsToCentimeters;

        if (IsComplete(Raw.Scale, 3))
        {
            if (Options.bScaleValuesAreLogarithmic)
            {
                OutPoint.Sigma = FVector(
                    FMath::Exp(FMath::Clamp(Raw.Scale[0], -20.0, 20.0)),
                    FMath::Exp(FMath::Clamp(Raw.Scale[1], -20.0, 20.0)),
                    FMath::Exp(FMath::Clamp(Raw.Scale[2], -20.0, 20.0))) * Options.UnitsToCentimeters;
            }
            else
            {
                OutPoint.Sigma = FVector(
                    FMath::Max(Raw.Scale[0], 0.000001),
                    FMath::Max(Raw.Scale[1], 0.000001),
                    FMath::Max(Raw.Scale[2], 0.000001)) * Options.UnitsToCentimeters;
            }
        }
        else
        {
            OutPoint.Sigma = FVector(FMath::Max(Options.DefaultSigmaCentimeters, 0.001));
        }

        if (IsComplete(Raw.Rotation, 4))
        {
            const bool bWFirst = Raw.bExplicitQuaternionNames || Options.bQuaternionWFirst;
            const FQuat SourceRotation = bWFirst
                ? FQuat(Raw.Rotation[1], Raw.Rotation[2], Raw.Rotation[3], Raw.Rotation[0]).GetNormalized()
                : FQuat(Raw.Rotation[0], Raw.Rotation[1], Raw.Rotation[2], Raw.Rotation[3]).GetNormalized();
            OutPoint.Rotation = TransformCoordinateRotation(SourceRotation, Options);
        }

        constexpr double ShDcFactor = 0.28209479177387814;
        if (IsComplete(Raw.Dc, 3))
        {
            OutPoint.Color = FLinearColor(
                0.5 + ShDcFactor * Raw.Dc[0],
                0.5 + ShDcFactor * Raw.Dc[1],
                0.5 + ShDcFactor * Raw.Dc[2],
                1.0);
        }
        else if (IsComplete(Raw.Rgb, 3))
        {
            const double Denominator = Raw.bIntegerRgb ||
                FMath::Max3(Raw.Rgb[0], Raw.Rgb[1], Raw.Rgb[2]) > 1.0 ? 255.0 : 1.0;
            if (Options.bRgbValuesAreSRGB)
            {
                OutPoint.Color = FLinearColor(
                    SRGBToLinear(Raw.Rgb[0], Denominator),
                    SRGBToLinear(Raw.Rgb[1], Denominator),
                    SRGBToLinear(Raw.Rgb[2], Denominator),
                    1.0);
            }
            else
            {
                OutPoint.Color = FLinearColor(
                    FMath::Clamp(Raw.Rgb[0] / Denominator, 0.0, 1.0),
                    FMath::Clamp(Raw.Rgb[1] / Denominator, 0.0, 1.0),
                    FMath::Clamp(Raw.Rgb[2] / Denominator, 0.0, 1.0),
                    1.0);
            }
        }

        if (FMath::IsFinite(Raw.Opacity))
        {
            OutPoint.Color.A = Raw.bLinearOpacity || !Options.bOpacityValuesAreLogits
                ? FMath::Clamp(Raw.Opacity / (Raw.bIntegerOpacity || Raw.Opacity > 1.0 ? 255.0 : 1.0), 0.0, 1.0)
                : 1.0 / (1.0 + FMath::Exp(-FMath::Clamp(Raw.Opacity, -20.0, 20.0)));
        }
        return true;
    }
}

bool FKasumiSplatPlyParser::Parse(
    TConstArrayView<uint8> Bytes,
    const FKasumiSplatPlyImportOptions& Options,
    FKasumiSplatPlyImportResult& OutResult,
    FString& OutError)
{
    return Parse(Bytes.GetData(), int64(Bytes.Num()), Options, OutResult, OutError);
}

bool FKasumiSplatPlyParser::Parse(
    const uint8* Data,
    int64 Size,
    const FKasumiSplatPlyImportOptions& Options,
    FKasumiSplatPlyImportResult& OutResult,
    FString& OutError)
{
    OutResult = {};
    OutError.Reset();
    if (!Data || Size < 16)
    {
        OutError = TEXT("The file is too small to be a PLY file.");
        return false;
    }

    int64 Offset = 0;
    FString Line;
    if (!ReadHeaderLine(Data, Size, Offset, Line) || Line.TrimStartAndEnd() != TEXT("ply"))
    {
        OutError = TEXT("Missing PLY signature.");
        return false;
    }

    bool bAscii = false;
    bool bBinaryLittleEndian = false;
    bool bBinaryBigEndian = false;
    bool bInVertexElement = false;
    bool bFoundEndHeader = false;
    int64 VertexCount = INDEX_NONE;
    TArray<FPlyProperty> Properties;
    FString HeaderHints;

    while (ReadHeaderLine(Data, Size, Offset, Line))
    {
        Line.TrimStartAndEndInline();
        TArray<FString> Tokens;
        Line.ParseIntoArrayWS(Tokens);
        if (Tokens.IsEmpty()) continue;
        if (Tokens[0] == TEXT("comment") || Tokens[0] == TEXT("obj_info"))
        {
            HeaderHints += TEXT(" ");
            HeaderHints += Line;
        }
        if (Tokens[0] == TEXT("format") && Tokens.Num() >= 2)
        {
            bAscii = Tokens[1] == TEXT("ascii");
            bBinaryLittleEndian = Tokens[1] == TEXT("binary_little_endian");
            bBinaryBigEndian = Tokens[1] == TEXT("binary_big_endian");
        }
        else if (Tokens[0] == TEXT("element") && Tokens.Num() >= 3)
        {
            bInVertexElement = Tokens[1] == TEXT("vertex");
            if (bInVertexElement) VertexCount = FCString::Atoi64(*Tokens[2]);
        }
        else if (Tokens[0] == TEXT("property") && bInVertexElement)
        {
            if (Tokens.Num() >= 2 && Tokens[1] == TEXT("list"))
            {
                OutError = TEXT("List properties in the vertex element are unsupported.");
                return false;
            }
            if (Tokens.Num() < 3)
            {
                OutError = TEXT("Malformed vertex property declaration.");
                return false;
            }
            FPlyProperty& Property = Properties.AddDefaulted_GetRef();
            Property.Type = ParseType(Tokens[1]);
            Property.Name = Tokens[2].ToLower();
            if (Property.Type == EScalarType::Invalid)
            {
                OutError = FString::Printf(TEXT("Unsupported PLY scalar type: %s"), *Tokens[1]);
                return false;
            }
        }
        else if (Tokens[0] == TEXT("end_header"))
        {
            bFoundEndHeader = true;
            break;
        }
    }

    if (!bFoundEndHeader || (!bAscii && !bBinaryLittleEndian && !bBinaryBigEndian))
    {
        OutError = TEXT("Unsupported or incomplete PLY header. Use ascii, binary_little_endian, or binary_big_endian format.");
        return false;
    }
    const int64 EffectivePointLimit = FMath::Min<int64>(Options.MaxPointCount, MAX_int32);
    if (VertexCount < 0 || VertexCount > EffectivePointLimit)
    {
        OutError = FString::Printf(TEXT("Vertex count %lld is outside the supported limit of %lld."), VertexCount, EffectivePointLimit);
        return false;
    }
    if (Properties.IsEmpty())
    {
        OutError = TEXT("The PLY has no scalar vertex properties.");
        return false;
    }

    OutResult.ResolvedProfile = ResolveProfile(Options.Profile, Properties, HeaderHints);
    const FKasumiSplatPlyImportOptions EffectiveOptions =
        BuildEffectiveOptions(Options, OutResult.ResolvedProfile);
    OutResult.LocalToSHDirection = BuildLocalToSHDirection(EffectiveOptions);

    if (bBinaryLittleEndian || bBinaryBigEndian)
    {
        int64 Stride = 0;
        for (const FPlyProperty& Property : Properties) Stride += ScalarSize(Property.Type);
        if (Stride <= 0 || Offset > Size || VertexCount > (Size - Offset) / Stride)
        {
            OutError = TEXT("Binary PLY payload is truncated.");
            return false;
        }
    }

    OutResult.Points.Reserve(int32(VertexCount));
    int32 RestCoefficientCount = 0;
    for (const FPlyProperty& Property : Properties)
    {
        if (Property.Name.StartsWith(TEXT("f_rest_")))
        {
            RestCoefficientCount = FMath::Max(RestCoefficientCount, FCString::Atoi(*Property.Name.RightChop(7)) + 1);
        }
    }
    RestCoefficientCount = FMath::Clamp(RestCoefficientCount, 0, 45);
    OutResult.HigherOrderSHCoefficientsPerPoint = RestCoefficientCount;
    if (RestCoefficientCount > 0)
    {
        const int64 HigherOrderValueCount = VertexCount * int64(RestCoefficientCount);
        if (HigherOrderValueCount > MAX_int32)
        {
            OutError = TEXT("Higher-order SH data exceeds the maximum array size supported by Unreal Engine.");
            return false;
        }
        OutResult.HigherOrderSH.Reserve(int32(HigherOrderValueCount));
    }
    FAsciiScanner Ascii(Data + Offset, Data + Size);
    const uint8* BinaryCursor = Data + Offset;
    const uint8* BinaryEnd = Data + Size;
#if PLATFORM_LITTLE_ENDIAN
    const bool bSwapBinaryByteOrder = bBinaryBigEndian;
#else
    const bool bSwapBinaryByteOrder = bBinaryLittleEndian;
#endif

    for (int64 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
    {
        if ((VertexIndex & 4095) == 0 &&
            Options.ProgressCallback &&
            !Options.ProgressCallback(VertexIndex, VertexCount))
        {
            OutError = TEXT("Import cancelled.");
            OutResult.Points.Reset();
            OutResult.HigherOrderSH.Reset();
            return false;
        }
        FRawPoint Raw;
        for (const FPlyProperty& Property : Properties)
        {
            double Value = 0.0;
            const bool bRead = bAscii ? Ascii.Read(Value) : ReadBinaryScalar(Property.Type, BinaryCursor, BinaryEnd, bSwapBinaryByteOrder, Value);
            if (!bRead)
            {
                OutError = FString::Printf(TEXT("PLY payload ended while reading vertex %lld."), VertexIndex);
                OutResult.Points.Reset();
                return false;
            }
            Assign(Raw, Property.Name, Property.Type, Value);
        }

        FKasumiSplatPoint Point;
        if (!ConvertPoint(Raw, int32(VertexIndex), EffectiveOptions, Point))
        {
            if (Options.bSkipInvalidPoints)
            {
                ++OutResult.SkippedPointCount;
                continue;
            }
            OutError = FString::Printf(TEXT("Vertex %lld has invalid or missing position data."), VertexIndex);
            OutResult.Points.Reset();
            return false;
        }
        OutResult.Points.Add(MoveTemp(Point));
        for (int32 CoefficientIndex = 0; CoefficientIndex < RestCoefficientCount; ++CoefficientIndex)
        {
            OutResult.HigherOrderSH.Add(FMath::IsFinite(Raw.Rest[CoefficientIndex]) ? float(Raw.Rest[CoefficientIndex]) : 0.0f);
        }
    }

    if (Options.ProgressCallback && !Options.ProgressCallback(VertexCount, VertexCount))
    {
        OutError = TEXT("Import cancelled.");
        OutResult.Points.Reset();
        OutResult.HigherOrderSH.Reset();
        return false;
    }

    OutResult.Encoding = bAscii
        ? TEXT("PLY ascii 1.0")
        : (bBinaryLittleEndian ? TEXT("PLY binary_little_endian 1.0") : TEXT("PLY binary_big_endian 1.0"));
    return true;
}
