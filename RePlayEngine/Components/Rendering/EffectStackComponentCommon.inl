// Effect Stack 3 系統の重複実装を 1 箇所に保持する内部実装テンプレート。
// クラスのメンバ配置・公開 API は変えず、型名だけ呼び出し側マクロで差し替える。
// UI 固有の capture_backdrop は既存の 6 行だけ条件付きで残す。

#include "EffectStackComponentCommonPart1.inl"
#include "EffectStackComponentCommonPart2.inl"
#include "EffectStackComponentCommonPart3.inl"
#include "EffectStackComponentCommonPart4.inl"
