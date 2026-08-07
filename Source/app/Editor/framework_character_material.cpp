// このファイルは空になりました。
//
// draw_character_material_controls は draw_shader_inspector へ統合されました。
// キャラクター材質の編集は ShaderInspector の中の 1 セクションとして
// 表示されます（framework_shader_stack.cpp から呼ばれます）。
//
// 【なぜ統合したか】
//   同じマテリアルを編集する画面が 6 箇所あり、
//   編集結果がどこへ効くのか画面から判断できなかったため。
//   データ構造（CharacterMaterialProfile）は一切変えていません。
//
// ファイル自体は 3dgp.vcxproj から外すまで残します。
// 空の翻訳単位は許されないため、ダミーを 1 つ置いています。
namespace
{
    // 空の翻訳単位を避けるためだけの定義。
    inline void ReplayShaderEditingMerged() noexcept {}
}
