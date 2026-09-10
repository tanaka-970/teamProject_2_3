# ソースを含めない standalone 書き出し

書き出し時に次のファイルを生成する。standalone の起動時はソース走査と DXC の読み込みを行わず、パックの破損・不足・DLL 不一致をエラーにする。

- `resources/ShaderCache.replaypack`: DXIL と ShaderLibrary のメタデータ・スキーマ・追加パス。
- `resources/ScriptCatalog.replaypack`: C# 型カタログ、フィールドスキーマ、配布する DLL の構成と指紋。

パックは `RPACK001` ヘッダー、内容のチェックサム、CBOR 本体で構成する。CBOR に種類と形式バージョンを保持する。チェックサムは破損検出用であり、改ざん防止や暗号化ではない。

## シェーダーを追加するとき

`Shader/Materials`、`Shader/Layers`、`Shader/PostProcess` 配下は書き出しのたびに再走査する。新規シェーダー、プロパティ、追加パスは既存の pragma 宣言で追加できる。Surface / Layer の Static・Skinned と、PostProcess の Static をコンパイルする。

カスタム UI 用には、既存 UI 経路と同様に定数バッファを b1 に配置した変種も生成する。Texture プロパティを持つカスタム UI は既存レンダラーでも非対応のため、通常の PostProcess 変種だけを生成する。

組み込みシェーダーは、エンジンのビルド時に `Tools/GenerateShaderPackInventory.ps1` が C++ の `CompileFile` 呼び出しとファイル名配列から列挙し、`$(IntDir)/ShaderPackInventory.inl` を生成する。描画側の既存呼び出しを一覧の正本とし、本数は固定しない。検証専用コードとコンパイラ・パック実装自体は対象外。

現在と同じ形のファイル・入口・プロファイル指定は自動で収集する。新しい動的なパス式、defines などのオプション指定、生成ソース経路を追加した場合は、生成ツールと書き出しの生成処理を対応させる。解釈できない呼び出しはビルド時の一覧生成でエラーになり、黙って配布対象から漏れない。`replay-pack-source` の注釈だけを増やして対応済みとしないこと。

キーは Shader からの相対パス、entry point、SM6 profile、defines、debug、optimize、warnings-as-errors、include 検索順。配布用コンパイルは debug=false / optimize=true とし、既存ディスクキャッシュを使わず DXC を実行する。standalone の GPU debug layer 指定はシェーダーの debug 情報とは分離し、Release bytecode を引く。

## C# を追加・変更するとき

既存の ReplayGuid とクラス発見規則に従い、C# をビルドしてエディタへリロードしてから書き出す。書き出し時にカタログを再収集し、各型をロードして得たスキーマを保存する。ソースが DLL より新しい場合、ロード後に DLL が変更された場合、型・スキーマが欠ける場合は失敗する。

エディタが実際にロードした Debug / Release 構成をパックへ記録する。両構成の bin コピーは維持するが、standalone はパックが指定した構成を選ぶ。コピー後と起動時に Game DLL・Managed API DLL・runtimeconfig の指紋を照合する。

standalone はパックから型を復元し、バックエンドの型登録まで完了させてからシーンを開始する。プロジェクトファイル生成、ソース発見、AssetDatabase 保存はこの経路で行わない。新しいフィールド型を導入した場合は ScriptPack の値の保存・復元も追加する。未対応の型は書き出しを失敗させる。

## 配布物と失敗時の扱い

`Shader/`、`Shader/compiled/`、`ThirdParty/DXC/` はコピーしない。resources と bin のコピーにもソース除外を適用し、最後に .hlsl / .hlsli / .cs がないことを確認する。PDB・C# プロジェクトファイルも除外する。

パックと同じエンジン実装を配布するため、実行中の exe をコピーする。Release の exe を配布する場合は Release エディタから書き出す。

一時フォルダーで全生成・コピーを完了してから配布先を置き換える。失敗時は前回の配布先を維持し、理由と欠けたキーを export_errors に残す。

コピーした Scene / Prefab のスクリプト参照と、Material / Layer のシェーダー参照もカタログと照合する。配布対象に参照切れがあれば、ファイル名と不足 ID を示して失敗する。

エディタ実行時のソースコンパイルとホットリロードは維持する。パックの有無だけでは standalone に切り替えず、既存の standalone_game_mode から初期化する。
