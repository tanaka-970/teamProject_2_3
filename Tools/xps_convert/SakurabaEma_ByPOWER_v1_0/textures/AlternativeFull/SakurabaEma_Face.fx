/* ------------------------------------------------------------
 * AlternativeFull
 * ------------------------------------------------------------ */
/* created by AlternativeFullFrontend. */
#define TEXTURE_THRESHOLD "shading_hint_default.png"
#define TEXTURE_SHADOW "../Sdw/T_SakurabaEma_Face_Sdw.png"
#define TEXTURE_SHADOW_BIAS "Shadow_Bias.png"
#define USE_SELFSHADOW_MODE
#define USE_NONE_SELFSHADOW_MODE
float SelfShadowPower = 0.1;
#define USE_MATERIAL_SPECULAR
#define USE_MATERIAL_SPHERE
float3 DefaultModeShadowColor = {1,1,1};
#define MAX_ANISOTROPY 0

#include "AlternativeFull.fxsub"
