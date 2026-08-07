# MSVC max macro fix

修正対象:
- `RePlayEngine/Rendering/Materials/MaterialAsset.cpp`

変更:
- `std::numeric_limits<std::uint64_t>::max()` を
  `(std::numeric_limits<std::uint64_t>::max)()` に変更

置換箇所数: 2

理由:
`windows.h` が定義する `max` マクロと `std::numeric_limits<T>::max()` が
MSVC プリプロセッサ上で衝突するため。括弧で関数名を囲むことで
function-like macro として展開されないようにする。

この修正は比較条件だけを変えずにコンパイル互換性を直すもので、
Material v4 / Shader Layer の保存仕様には変更を加えない。
