#pragma once

#include "CoreMinimal.h"
#include "KasumiSplatPlyParser.h"

class FKasumiSplatSpzParser
{
public:
    static bool Parse(
        const uint8* Data,
        int64 Size,
        const FKasumiSplatPlyImportOptions& Options,
        FKasumiSplatPlyImportResult& OutResult,
        FString& OutError);

    static bool Parse(
        TConstArrayView<uint8> Bytes,
        const FKasumiSplatPlyImportOptions& Options,
        FKasumiSplatPlyImportResult& OutResult,
        FString& OutError);
};
