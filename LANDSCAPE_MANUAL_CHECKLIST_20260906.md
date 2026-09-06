# Landscape / Inspector 手動確認表

確認日：________　確認者：________　使用ビルド：________

検証用に複製したSceneで実施してください。結果欄は「OK / NG / 未確認」で記入します。
必要な検証シーンは `Saved/Validation/LandscapeRegression` にあります。

## 優先確認：今回の2症状と追加症状

| No. | 操作手順 | 合格基準 | 結果 |
| --- | --- | --- | --- |
| 1 | Landscapeの同じ場所で「細かくする」を繰り返す。その場所からブラシを外し、再び合わせる。何度か往復する。 | ImGui全体やInspectorが点滅・消失しない。表面上のブラシ円が消えない。 | □ |
| 2 | No.1の場所でPreviewをリング・Falloff・Grid・Contour・Grid＋Contourへ切り替える。 | どの表示でもUIが残り、外周のブラシ円を確認できる。高密度時のFace/Grid表示の間引きは正常。 | □ |
| 3 | 細かくした場所を斜め・横から狙い、Raise・Lower・Smoothする。 | 狙った表面を拾い続ける。ブラシ位置と実際の変形位置が一致する。 | □ |
| 4 | 重いモデルを選択してInspectorを開く。Scene Viewで右ドラッグ、中央ドラッグ、Wheel、WASD、Q/Eを操作する。選択解除時とも比較する。 | 選択中だけ急激に重くなる症状がない。ProfilerのScene/Captureがカメラ操作に反応して増えない。 | □ |
| 5 | 同じComponentを持つObjectを2個以上選択し、Inspectorを開いたまま待つ。そのままカメラを動かす。 | 表示だけでScene/Captureが発生しない。選択数・Property数が増えてもSnapshotを連発しない。 | □ |

## Inspector・Undo

| No. | 操作手順 | 合格基準 | 結果 |
| --- | --- | --- | --- |
| 6 | 単体選択で数値ドラッグ、Checkbox、Combo、文字入力、色変更をそれぞれ行い、Undo→Redoする。 | 最初の変更前まで正しく戻り、Redoで編集結果に戻る。1回の連続ドラッグが細切れのUndoにならない。 | □ |
| 7 | 複数選択で共通Propertyを変更し、Undo→Redoする。 | 選択したObjectすべてへ反映し、Undoで各Objectの元の値に戻る。 | □ |
| 8 | Propertyをドラッグして離し、Inspectorを閉じる・別タブへ移る。その後、別Objectを編集してUndoする。 | 先の編集が未確定のまま残らず、別の編集と混ざらない。 | □ |
| 9 | 大きなLandscapeがあるSceneで、別Objectの位置・影のON/OFF・Material色を変更し、Undo→Redoする。 | 別Objectの編集が戻り、Landscape形状は保たれる。不要な長い停止がない。 | □ |
| 10 | 1ストロークで複数回「細かくする」を行い、Undo→Redoする。これを数回繰り返す。 | 1ストローク分の細分化を戻せる。他のObjectは変わらず、Scene/Captureを発生させない。 | □ |
| 11 | Sculpt・細分化したSceneを別名保存し、読み直す。既存のSceneも開く。 | 編集した地形形状・設定が保存され、既存Sceneも読み込める。 | □ |

## Landscapeの表面・形状

| No. | 操作手順 | 合格基準 | 結果 |
| --- | --- | --- | --- |
| 12 | 平坦地形でRaise・Lower・Smooth・Flattenを実施し、それぞれUndo→Redoする。 | 各操作が意図どおり働き、Undo/Redoで形状を正しく復元する。 | □ |
| 13 | 上下に近接したSurfaceの上面をLocalYで編集し、下面も確認する。次に洞窟内の下面を編集する。 | 狙っていない別Surfaceが変形しない。Previewが地上側へ飛ばない。 | □ |
| 14 | 崖・オーバーハングをLocalYとVertexNormalで編集する。画面中央と端の両方で試す。 | 円と加工位置が一致する。LocalYはY方向、VertexNormalは表面法線方向に変形する。 | □ |
| 15 | Chunk境界をまたいでRaise・Smooth・VertexNormalを繰り返す。光の当たり方を変えて見る。 | 境界だけに法線の継ぎ目が出ず、VertexNormalが不自然に横滑りしない。 | □ |
| 16 | 細分化の密度が不均一な境目でSmoothする。 | 特定の辺や三角形の並びへ極端に引っ張られない。 | □ |
| 17 | 同じ速さ・同じ軌跡のStrokeを120/60/30/15 FPS相当で比較する。 | 低FPSでも点々と途切れず、おおむね同じ強さ・形になる。自動テストの形状差は約2.8%以内。 | □ |
| 18 | ブラシを地形外へ出して戻す。Strokeの途中でカメラ操作へ移り、再びSculptする。 | 離れた位置同士をつなぐ意図しない加工線ができない。 | □ |
| 19 | 高くRaiseした後、Lower/FlattenしてStrokeを終了する。地形を画面端へ移す。 | 古い高さのBoundsが残らず、表示が突然消えたり、当たり判定だけが浮いたりしない。 | □ |
| 20 | VertexNormalやSmoothで横へ動かすStrokeを多数行い、終了後に地形を各方向から見る。 | Chunk境界付近の表示・選択・当たり判定が壊れない。 | □ |
| 21 | 局所Sculptを終了して、その場所を歩く／Collider表示で確認する。周辺の未編集部分も確認する。 | 編集後の形に当たり判定が追従し、未編集部分も正常。 | □ |

## Editorの負荷・キャッシュ

| No. | 操作手順 | 合格基準 | 結果 |
| --- | --- | --- | --- |
| 22 | 1万・10万・100万Face程度の地形で、同じ小ブラシのSculptと細分化を比較する。 | 総Face数100倍に対して小ブラシの処理時間がそのまま100倍へ増えない。Stroke終了時の時間は別に記録する。 | □ |
| 23 | ブラシを止めた時と動かした時のPreviewを比較する。別Surfaceへ移動し、地形も編集する。 | 停止中に不要な再計算を繰り返さず、移動・編集後は古い位置のPreviewが残らない。 | □ |
| 24 | 通常Sculptと細分化のLandscape/ChunkUploadを比較する。 | 通常SculptはIndex Buffer再作成を行わない。これは画面だけでは判定できないため、GPU検証済み項目として扱ってもよい。 | □ |
| 25 | 重いSkinnedモデルでRigパネルとRig Debugを閉じ、次に開く。Pose編集も試す。 | 通常時に不要なRigPose処理がない。Rig表示・Pose編集が必要な時は正しく機能する。 | □ |
| 26 | Material Slotの多いモデルを選択し、別モデルへの選択変更・Slot変更・モデル再読込を試す。 | Slot名や設定が正しく切り替わり、古いモデルの情報が残らない。表示だけで大きく重くならない。 | □ |
| 27 | Materialの描画方式を変更・保存し、Inspectorの影関連設定を確認する。 | 保存／再読込後に対応する設定表示へ更新される。未読込MaterialでもUIが破綻しない。 | □ |
| 28 | RMB＋WheelでCamera速度を連続変更し、RMBを離す。再起動して速度を確認する。 | Wheelのたびに引っかからず、確定した速度が保存される。 | □ |
| 29 | Collider・Rig・AI/Stage DebugをOFF→ONする。非選択AI・Stageの表示トグルも切り替える。 | OFF時に不要な表示がなく、ON時は必要な表示が復帰する。非選択表示は明示トグルに従う。 | □ |
| 30 | DirectionalLightを複数置き、有効／無効・親ObjectのActive・追加／削除を切り替える。 | 有効なDirectionalLight数の警告が正しく更新され、古い数が残らない。 | □ |
| 31 | load_rangeを有効にし、範囲境界の近くをカメラで小さく往復する。さらに十分離れて戻る。 | 境界付近で毎回Unload/Uploadを繰り返さない。十分離れたChunkは解放され、戻ると表示される。 | □ |

## 内部検証で確認済みの項目

画面操作で直接判定しにくい項目は、以下の自動検証を通過しています。

| 項目 | 確認結果 |
| --- | --- |
| LandscapeMeshGeneratorが指定Chunkだけを返す | PASS |
| 50万頂点のUndoで形状を共有し、mesh_data文字列を作らない | PASS |
| Chunk境界の部分法線と全Face再計算法線が一致 | PASS |
| 微小な縦向き三角形のRaycast・法線・Surface候補 | PASS |
| 同じ場所を100回細分化後のブラシ円・Preview転送量 | PASS（約7.6万Face、約0.74MB） |
| 8MBを超えるImGuiの4フレーム連続描画 | PASS（GPU拡張検証365項目） |

## NGがあった場合の記録

| 項目 | 記入欄 |
| --- | --- |
| 確認No. | |
| 使用Scene／モデル | |
| ブラシ種別・方向・半径・目標辺長 | |
| 何回目／何秒後に発生したか | |
| 直前の操作 | |
| 期待した状態／実際の状態 | |
| 再現頻度 | 毎回 ／ ときどき ／ 1回のみ |
| 選択なし／選択ありのFPS | |
| Inspector閉／開のFPS | |
| Undoで戻るか | |

Profilerで記録する項目：`Scene/Capture`、`Editor/Inspector`、`Landscape/Viewport`、`Landscape/Preview`、`Landscape/Brush`、`Landscape/ChunkUpload`、`Item/RigPose`、`Item/BonePalette`。
