#include "KasumiSplatShaders.h"

IMPLEMENT_GLOBAL_SHADER(FKasumiSplatCullCS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "CullCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FKasumiSplatFinalizeTiledCS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "FinalizeTiledCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FKasumiSplatPrefixCS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "PrefixCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FKasumiSplatScatterCS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "ScatterCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FKasumiSplatInstanceVS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FKasumiSplatInstancePS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "MainPS", SF_Pixel);
