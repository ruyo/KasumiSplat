#include "KasumiSplatShaders.h"

IMPLEMENT_GLOBAL_SHADER(FKasumiSplatCullCS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "CullCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FKasumiSplatInitializeClusterDispatchCS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "InitializeClusterDispatchCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FKasumiSplatClusterCullCS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "ClusterCullCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FKasumiSplatFinalizeClusterDispatchCS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "FinalizeClusterDispatchCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FKasumiSplatFinalizeTiledCS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "FinalizeTiledCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FKasumiSplatPrefixCS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "PrefixCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FKasumiSplatScatterCS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "ScatterCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FKasumiSplatInstanceVS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FKasumiSplatInstancePS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FKasumiSplatVelocityPS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "VelocityPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FKasumiSplatGroupVS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "GroupVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FKasumiSplatGroupPS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "GroupPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FKasumiSplatGroupVelocityPS, "/Plugin/KasumiSplat/Private/KasumiSplatProbe.usf", "GroupVelocityPS", SF_Pixel);
