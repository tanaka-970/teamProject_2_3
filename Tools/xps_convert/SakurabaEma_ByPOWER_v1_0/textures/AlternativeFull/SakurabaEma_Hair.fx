/* ------------------------------------------------------------
 * AlternativeFull
 * ------------------------------------------------------------ */
/* created by AlternativeFullFrontend. */
#define TEXTURE_THRESHOLD "shading_hint_default.png"
#define TEXTURE_SHADOW "../Sdw/T_SakurabaEma_Hair_Sdw.png"
#define USE_SELFSHADOW_MODE
#define USE_NONE_SELFSHADOW_MODE
float SelfShadowPower = 0.1;
#define USE_MATERIAL_SPECULAR
#define USE_MATERIAL_SPHERE
float3 DefaultModeShadowColor = {1,1,1};
#define MAX_ANISOTROPY 0

//#define USE_SPHERE_CHEET
//float SphereBoost = 0.5;

#include "AlternativeFull.fxsub"
