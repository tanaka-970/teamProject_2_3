# Shader Pass Validation 1711 fix

## 原因

`ShaderPassValidation.cpp` が `ShaderLayerStack::Add()` から返る
`ShaderLayer&` を保持したまま、同じ `std::vector<ShaderLayer>` に
もう一度 `Add()` していました。

2回目の `push_back` で vector が再確保されると最初の参照 `a` は無効になります。
その後 `a.id` を読む Validation 自体が dangling reference を参照しており、
`[FAIL 1711] Layer -> その Shader passes の順で展開する`
が誤って失敗する可能性がありました。

## 修正

各 Layer の persistent ID を、次の `Add()` より前に `std::uint64_t` として退避します。

- `a_id`
- `b_id`

ExecutionPlan の比較は参照ではなく、この値を使用します。

Runtime の `ShaderExecutionPlan`、Material v4、Shader Layer/Pass の保存仕様には変更ありません。
