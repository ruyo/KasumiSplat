#pragma once

#include "CoreMinimal.h"
#include "Misc/PackageName.h"

namespace KasumiSplatNaming
{
    inline FString StripKnownPrefix(FString Name)
    {
        if (Name.StartsWith(TEXT("KSA_"), ESearchCase::IgnoreCase))
        {
            Name.RightChopInline(4, EAllowShrinking::No);
        }
        else if (Name.StartsWith(TEXT("KS_"), ESearchCase::IgnoreCase))
        {
            Name.RightChopInline(3, EAllowShrinking::No);
        }
        return Name.IsEmpty() ? TEXT("Splat") : Name;
    }

    inline FName MakeAssetName(FName SourceName)
    {
        return FName(TEXT("KSA_") + StripKnownPrefix(SourceName.ToString()));
    }

    inline FString MakeAssetPackageName(const FString& SourcePackageName, FName AssetName)
    {
        return FPackageName::GetLongPackagePath(SourcePackageName) / AssetName.ToString();
    }

    inline FString MakeActorLabel(const UObject* Asset)
    {
        return TEXT("KS_") + StripKnownPrefix(Asset ? Asset->GetName() : FString());
    }
}
