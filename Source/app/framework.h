#pragma once

// framework 宣言の互換入口。既存の include 元は引き続きこのファイルだけを見る。
//
//   framework.h                  … 宣言ヘッダをまとめる入口（このファイル）
//   framework_application.h      … アプリ共通の外部宣言・定数・基礎依存
//   framework_class.h            … framework クラス宣言全体
//
// framework クラスはメンバの宣言順が寿命と挙動を決める単一のクラス宣言なので、
// クラス本体は framework_class.h に一体のまま置く。

#include "framework_application.h"
#include "framework_class.h"
