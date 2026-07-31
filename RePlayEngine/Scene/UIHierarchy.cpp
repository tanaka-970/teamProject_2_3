#include "UIHierarchy.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

using namespace DirectX;

namespace ReplayEngine::Scene
{
    void UIHierarchy::Build(const SceneDocument& document)
    {
        nodes_.clear();
        resolved_.clear();
        cycle_count_ = 0;

        // --- UI要素を集める ---
        std::unordered_map<EntityId, std::size_t> index_of;
        for (const SceneEntity& entity : document.Entities())
        {
            if (!entity.ui_element) continue;
            Node node{};
            node.id = entity.id;
            node.parent = entity.ui_element->parent;
            node.order = entity.ui_element->order;
            node.entity = &entity;
            node.ui = &*entity.ui_element;
            index_of.emplace(entity.id, nodes_.size());
            nodes_.push_back(std::move(node));
        }
        if (nodes_.empty()) return;

        // --- 親子を結ぶ ---
        // 親が見つからない/自分自身を指す場合はルート扱いにする。
        // 壊れたデータでも落とさないことを優先する。
        std::vector<std::size_t> roots;
        for (std::size_t i = 0; i < nodes_.size(); ++i)
        {
            const EntityId parent = nodes_[i].parent;
            const auto found = parent != 0 && parent != nodes_[i].id
                ? index_of.find(parent) : index_of.end();
            if (found == index_of.end()) roots.push_back(i);
            else nodes_[found->second].children.push_back(i);
        }

        // --- 同じ親の中では order 昇順、同値なら定義順 ---
        const auto sort_by_order = [this](std::vector<std::size_t>& list)
        {
            std::stable_sort(list.begin(), list.end(),
                [this](std::size_t a, std::size_t b)
                {
                    return nodes_[a].order < nodes_[b].order;
                });
        };
        sort_by_order(roots);
        for (Node& node : nodes_) sort_by_order(node.children);

        // --- ルートから再帰的に解決する ---
        Resolved identity{};
        identity.screen_position = { 0.0f, 0.0f };
        identity.screen_size = { 1.0f, 1.0f };   // 親スケールの初期値として使う
        identity.rotation = 0.0f;
        identity.opacity = 1.0f;

        resolved_.reserve(nodes_.size());
        for (const std::size_t root : roots) Traverse(root, identity);

        // 循環で未訪問のまま残ったものを救済する(ルートとして描く)。
        for (std::size_t i = 0; i < nodes_.size(); ++i)
        {
            if (nodes_[i].visited) continue;
            ++cycle_count_;
            Traverse(i, identity);
        }
    }

    void UIHierarchy::Traverse(std::size_t index, const Resolved& parent_state)
    {
        Node& node = nodes_[index];
        // 循環しても無限再帰しないよう、訪問済みは打ち切る。
        if (node.visited) return;
        node.visited = true;

        const UIElementData& ui = *node.ui;

        // 親から受け継ぐスケールは screen_size に畳んである。
        const XMFLOAT2 inherited_scale = parent_state.screen_size;
        const XMFLOAT2 total_scale{
            inherited_scale.x * ui.scale.x,
            inherited_scale.y * ui.scale.y };

        // 実寸。size は元サイズで、scale とは独立して持つ。
        const XMFLOAT2 actual_size{
            ui.size.x * total_scale.x,
            ui.size.y * total_scale.y };

        // アンカーを考慮した左上座標を求める。
        // position はアンカー点の位置を指すので、そこから
        // アンカー分だけ戻した位置が矩形の左上になる。
        const XMFLOAT2 local_offset{
            ui.position.x * inherited_scale.x - actual_size.x * ui.anchor.x,
            ui.position.y * inherited_scale.y - actual_size.y * ui.anchor.y };

        // 親の回転を自分の位置へ適用する。回転中心は親のアンカー点
        // (= 親の screen_position に親アンカーを足した点)だが、
        // 親側で既にその点を basis として渡しているので、
        // ここでは親の原点まわりに回すだけでよい。
        XMFLOAT2 rotated = local_offset;
        if (std::abs(parent_state.rotation) > 1.0e-4f)
        {
            const float radians = XMConvertToRadians(parent_state.rotation);
            const float cosine = std::cos(radians);
            const float sine = std::sin(radians);
            rotated.x = local_offset.x * cosine - local_offset.y * sine;
            rotated.y = local_offset.x * sine + local_offset.y * cosine;
        }

        Resolved result{};
        result.id = node.id;
        result.screen_position = {
            parent_state.screen_position.x + rotated.x,
            parent_state.screen_position.y + rotated.y };
        result.screen_size = actual_size;
        result.rotation = parent_state.rotation + ui.rotation;
        result.opacity = parent_state.opacity * ui.opacity;
        result.color = {
            ui.color.x, ui.color.y, ui.color.z, ui.color.w * result.opacity };
        result.uv_rect = ui.uv_rect;
        result.alpha_mode = ui.alpha_mode;
        result.source = &ui;
        result.entity = node.entity;

        // 非表示でもリストには積む。エディタが選択できるようにするため。
        // 描画側で visible / active を見て弾く。
        resolved_.push_back(result);

        // 子へ渡す状態。screen_size にはスケールを畳んで渡し、
        // 位置はアンカー点(回転中心)を基準にする。
        Resolved child_basis = result;
        child_basis.screen_position = {
            result.screen_position.x + actual_size.x * ui.anchor.x,
            result.screen_position.y + actual_size.y * ui.anchor.y };
        child_basis.screen_size = total_scale;

        for (const std::size_t child : node.children) Traverse(child, child_basis);
    }
}
