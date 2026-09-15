# -*- coding: utf-8 -*-
"""BootLogoComponent.cpp の式をそのまま Scene + Motion へ書き出す。

C++ 側の定数はここへ写している。式は Motion の expression で完全一致させ、
キーは 1 本だけ置いて expression に上書きさせる。
"""
import math
import os

# ---- BootLogoComponent.cpp の定数 ----
DURATION = 3.87
CUT = 0.433
FLOW_END = 0.60
HOLD_END = 2.40
SLIDE_OUT_END = 2.97
STAR_FILL = 3.87
AXIS_ANGLE = 45.0
DIRX, DIRY = 0.70711, -0.70711
PERPX, PERPY = 0.70711, 0.70711
NAVY = (0.016, 0.077, 0.180)
PALE = (0.472, 0.678, 0.763)
BLUE = (0.094, 0.467, 0.949)
TONES = {0: PALE, 1: NAVY, 2: BLUE}

STREAKS = [
    (-300.0,  -600.0, 1500.0, 620.0, 0, 0.75),
    (-880.0, -1500.0,  900.0, 120.0, 1, 1.00),
    ( 560.0, -2600.0, 1300.0, 200.0, 1, 1.00),
    (-1180.0, -3400.0, 1100.0,  70.0, 2, 0.95),
    ( 180.0, -4200.0, 1800.0, 300.0, 1, 1.00),
    ( 940.0, -5000.0, 1400.0, 100.0, 0, 0.85),
    (-520.0, -5800.0, 2000.0, 420.0, 1, 1.00),
    (1240.0, -6600.0, 1200.0,  60.0, 2, 0.90),
    (-960.0, -7400.0, 1600.0, 240.0, 0, 0.80),
]

REF_W, REF_H = 1920.0, 1080.0
CX, CY = REF_W * 0.5, REF_H * 0.5

GUID_MARK = "c5df2c4beef61362cd7e459698b07bcb"
GUID_WORD = "1507c000c4b97bf495e89901541a9ead"
GUID_STAR = "cf09a62e7242455ae02a99ec8ceed36f"
GUID_NOTE = "874e484928a3262e2ac9b962708fc143"

SCENE_GUID = "b07100601eaa4f1ea9d3b0f0cae51001"
MOTION_GUID = "b07100601eaa4f1ea9d3b0f0cae51002"

OUT_DIR = "resources/RePlayEngine/BootLogoExact"
SCENE_PATH = OUT_DIR + "/BootLogoExact.replayscene"
MOTION_PATH = OUT_DIR + "/BootLogoExact.replaymotion"


def n(value):
    """桁ノイズを出さずに書く。"""
    text = '%.9g' % float(value)
    return text


def p_(value):
    """負の数を括弧で包む。式の中で -- にならないように。"""
    text = n(value)
    return '(' + text + ')' if text.startswith('-') else text


# ---- 式の部品（Motion expression 文法） ----
PROGRESS = "clamp(t/%s,0,1)" % n(FLOW_END)
ZOOM = "lerp(1+7*pow(clamp((t-0.22)/%s,0,1),2.6),1,step(%s,t))" % (
    n(CUT - 0.22), n(CUT))
SLIDE_P = "clamp((t-%s)/%s,0,1)" % (n(HOLD_END), n(SLIDE_OUT_END - HOLD_END))
SLIDE = "(-(%s)*(%s)*3000)" % (SLIDE_P, SLIDE_P)
SLIDE_X = "(%s*%s)" % (n(DIRX), SLIDE)
SLIDE_Y = "(%s*%s)" % (n(-DIRY), SLIDE)
DECO_A = "clamp((t-0.95)/0.22,0,1)"
TO_CENTER = SLIDE_P
GROW = "pow(clamp((t-%s)/%s,0,1),6)" % (n(SLIDE_OUT_END), n(STAR_FILL - SLIDE_OUT_END))
# 描画される窓。C++ の if 条件そのまま。
VISIBLE_LOGO = "(step(%s,t)*(1-step(%s,t)))" % (n(CUT), n(SLIDE_OUT_END))


def streak_pos_expr(lane, start):
    along = "(%s+(2600-%s)*%s)" % (p_(start), p_(start), PROGRESS)
    ln = "(-297+(%s+297)*%s)" % (p_(lane), ZOOM)
    za = "(-1060+(%s+1060)*%s)" % (along, ZOOM)
    x = "(%s*(%s+%s))" % (n(PERPX), ln, za)
    y = "(%s*(%s-%s))" % (n(PERPY), za, ln)
    return "lerp(%s,%s,i)" % (x, y)


def streak_size_expr(length, thickness):
    return "lerp(%s*%s,%s*%s,i)" % (n(length), ZOOM, n(thickness), ZOOM)


def streak_opacity_expr(alpha):
    # C++ は time_ <= kFlowEnd のときだけ描く。
    return "%s*(1-step(%s,t))" % (n(alpha), n(FLOW_END + 1.0e-4))


# ---- Scene ----
def rect(anchor, pos, size, rotation=0.0, sort_order=0, stable=1):
    return [
        '  COMPONENT "RectTransformComponent" 1',
        '    STABLE_ID %d' % stable,
        '    TYPE_GUID "00000000000000000000000000000000"',
        '    TYPE_MODULE ""',
        '    TYPE_VERSION 1',
        '    PROPERTY_COUNT 8',
        '    PROPERTY "anchor_min" vec2 %s %s' % (n(anchor[0]), n(anchor[1])),
        '    PROPERTY "anchor_max" vec2 %s %s' % (n(anchor[2]), n(anchor[3])),
        '    PROPERTY "anchored_position" vec2 %s %s' % (n(pos[0]), n(pos[1])),
        '    PROPERTY "size_delta" vec2 %s %s' % (n(size[0]), n(size[1])),
        '    PROPERTY "pivot" vec2 0.5 0.5',
        '    PROPERTY "rotation" float %s' % n(rotation),
        '    PROPERTY "scale" vec2 1 1',
        '    PROPERTY "sort_order" int %d' % sort_order,
        '  END_COMPONENT',
    ]


def image(sprite, color, opacity, stable=2):
    return [
        '  COMPONENT "UIImageComponent" 1',
        '    STABLE_ID %d' % stable,
        '    TYPE_GUID "00000000000000000000000000000000"',
        '    TYPE_MODULE ""',
        '    TYPE_VERSION 1',
        '    PROPERTY_COUNT 3',
        '    PROPERTY "sprite" assetref "%s"' % sprite,
        '    PROPERTY "color" color %s %s %s 1' % (n(color[0]), n(color[1]), n(color[2])),
        '    PROPERTY "opacity" float %s' % n(opacity),
        '  END_COMPONENT',
    ]


def game_object(object_id, name, parent, components):
    lines = [
        'OBJECT',
        '  ID %d' % object_id,
        '  NAME "%s"' % name,
        '  ENABLED 1',
        '  PARENT %d' % parent,
        '  TRANSFORM 0 0 0 0 0 0 1 1 1',
        '  PREFAB "" 0 0',
        '  COMPONENT_COUNT %d' % len(components),
    ]
    for block in components:
        lines.extend(block)
    lines.append('END_OBJECT')
    return lines


def build_scene():
    objects = []
    canvas = [
        '  COMPONENT "CanvasComponent" 1',
        '    STABLE_ID 1',
        '    TYPE_GUID "00000000000000000000000000000000"',
        '    TYPE_MODULE ""',
        '    TYPE_VERSION 1',
        '    PROPERTY_COUNT 6',
        '    PROPERTY "reference_resolution" vec2 1920 1080',
        '    PROPERTY "render_mode" enum 0',
        '    PROPERTY "scale_mode" enum 1',
        '    PROPERTY "match_width_or_height" float 0.5',
        '    PROPERTY "sort_order" int 0',
        '    PROPERTY "opacity" float 1',
        '  END_COMPONENT',
    ]
    player = [
        '  COMPONENT "MotionPlayerComponent" 1',
        '    STABLE_ID 3',
        '    TYPE_GUID "00000000000000000000000000000000"',
        '    TYPE_MODULE ""',
        '    TYPE_VERSION 1',
        '    PROPERTY_COUNT 10',
        '    PROPERTY "motion" assetref "%s"' % MOTION_GUID,
        '    PROPERTY "key" string "BootLogoExact"',
        '    PROPERTY "play_on_start" bool 1',
        '    PROPERTY "trigger" enum 0',
        '    PROPERTY "loop" bool 0',
        '    PROPERTY "wrap_mode" enum 0',
        '    PROPERTY "auto_stop_on_end" bool 1',
        '    PROPERTY "ignore_time_scale" bool 1',
        '    PROPERTY "speed" float 1',
        '    PROPERTY "weight" float 1',
        '  END_COMPONENT',
    ]
    objects.append(game_object(
        100, 'BootLogoCanvas', 0,
        [canvas, rect((0, 0, 1, 1), (0, 0), (0, 0), 0.0, 0, 2), player]))

    # 背景は白の全画面。C++ の最初の AddQuad と同じ。
    objects.append(game_object(
        101, 'Background', 100,
        [rect((0, 0, 1, 1), (0, 0), (0, 0), 0.0, 0),
         image("", (1.0, 1.0, 1.0), 1.0)]))

    next_id = 110
    for index, (lane, start, length, thickness, tone, alpha) in enumerate(STREAKS):
        color = TONES[tone]
        objects.append(game_object(
            next_id + index, 'Streak%d' % index, 100,
            [rect((0.5, 0.5, 0.5, 0.5), (0, 0), (length, thickness), AXIS_ANGLE, 1 + index),
             image("", color, alpha)]))

    objects.append(game_object(
        130, 'Mark', 100,
        [rect((0.5, 0.5, 0.5, 0.5), (505.0 - CX, CY - 520.0), (380.0, 380.0), 0.0, 20),
         image(GUID_MARK, (1.0, 1.0, 1.0), 0.0)]))
    word_w = 880.0
    word_h = word_w / 7.3
    objects.append(game_object(
        131, 'Word', 100,
        [rect((0.5, 0.5, 0.5, 0.5), (725.0 + word_w * 0.5 - CX, CY - 520.0),
              (word_w, word_h), 0.0, 21),
         image(GUID_WORD, (1.0, 1.0, 1.0), 0.0)]))
    objects.append(game_object(
        132, 'Note', 100,
        [rect((0.5, 0.5, 0.5, 0.5), (1435.0 + 55.0 - CX, CY - (190.0 + 55.0)),
              (110.0, 110.0), 14.0, 22),
         image(GUID_NOTE, PALE, 0.0)]))
    objects.append(game_object(
        133, 'Star', 100,
        [rect((0.5, 0.5, 0.5, 0.5), (1517.0 - CX, CY - 255.0), (150.0, 150.0), 0.0, 23),
         image(GUID_STAR, PALE, 0.0)]))

    head = [
        'REPLAY_SCENE 11',
        'SCENE "RePlayEngine Boot Logo (Exact)"',
        'SCENE_STATE 100',
        'COLLISION_STATE 2 0',
        'OBJECT_COUNT %d' % (len(objects)),
    ]
    body = []
    for block in objects:
        body.extend(block)
    return '\n'.join(head + body) + '\n'


# ---- Motion ----
def track(name, path, component, prop, value_type, default, expression):
    lines = [
        '',
        'TRACK "%s"' % name,
        'OBJECT 100',
        'BINDING_ORIGIN 3',
        'BINDING_PATH "%s"' % path,
        'COMPONENT_TYPE "%s"' % component,
        'COMPONENT_INDEX 0',
        'PROPERTY "%s"' % prop,
        'VALUE_TYPE %s' % value_type,
        'ENABLED 1',
        'BLEND_MODE Override',
        'EXPRESSION 1 "%s"' % expression,
        'KEY 0 %s EASING Linear' % default,
        'END_TRACK',
    ]
    return lines


def build_motion():
    lines = [
        'MOTION "RePlayEngine Boot Logo (Exact)"',
        'MOTION_VERSION 7',
        'DURATION %s' % n(DURATION),
    ]
    for index, (lane, start, length, thickness, tone, alpha) in enumerate(STREAKS):
        name = 'Streak%d' % index
        lines.extend(track(name + ' 位置', name, 'RectTransformComponent',
                           'anchored_position', 'VEC2', 'VEC2 0 0',
                           streak_pos_expr(lane, start)))
        lines.extend(track(name + ' 大きさ', name, 'RectTransformComponent',
                           'size_delta', 'VEC2', 'VEC2 %s %s' % (n(length), n(thickness)),
                           streak_size_expr(length, thickness)))
        lines.extend(track(name + ' 濃さ', name, 'UIImageComponent',
                           'opacity', 'FLOAT', 'FLOAT %s' % n(alpha),
                           streak_opacity_expr(alpha)))

    mark_x = 505.0 - CX
    mark_y = CY - 520.0
    lines.extend(track('Mark 位置', 'Mark', 'RectTransformComponent',
                       'anchored_position', 'VEC2', 'VEC2 %s %s' % (n(mark_x), n(mark_y)),
                       'lerp(%s+%s,%s+%s,i)' % (p_(mark_x), SLIDE_X, p_(mark_y), SLIDE_Y)))
    lines.extend(track('Mark 濃さ', 'Mark', 'UIImageComponent', 'opacity', 'FLOAT',
                       'FLOAT 1', VISIBLE_LOGO))

    word_w = 880.0
    word_x = 725.0 + word_w * 0.5 - CX
    word_y = CY - 520.0
    lines.extend(track('Word 位置', 'Word', 'RectTransformComponent',
                       'anchored_position', 'VEC2', 'VEC2 %s %s' % (n(word_x), n(word_y)),
                       'lerp(%s+%s,%s+%s,i)' % (p_(word_x), SLIDE_X, p_(word_y), SLIDE_Y)))
    lines.extend(track('Word 濃さ', 'Word', 'UIImageComponent', 'opacity', 'FLOAT',
                       'FLOAT 1', VISIBLE_LOGO))

    note_x = 1435.0 + 55.0 - CX
    note_y = CY - (190.0 + 55.0)
    lines.extend(track('Note 位置', 'Note', 'RectTransformComponent',
                       'anchored_position', 'VEC2', 'VEC2 %s %s' % (n(note_x), n(note_y)),
                       'lerp(%s+%s,%s+%s,i)' % (p_(note_x), SLIDE_X, p_(note_y), SLIDE_Y)))
    lines.extend(track('Note 濃さ', 'Note', 'UIImageComponent', 'opacity', 'FLOAT',
                       'FLOAT 0', '%s*%s*0.8' % (VISIBLE_LOGO, DECO_A)))

    star_x0 = 1517.0 - CX
    star_y0 = CY - 255.0
    star_x = '(%s+(0-%s)*%s)' % (p_(star_x0), p_(star_x0), TO_CENTER)
    star_y = '(%s+(0-%s)*%s)' % (p_(star_y0), p_(star_y0), TO_CENTER)
    lines.extend(track('Star 位置', 'Star', 'RectTransformComponent',
                       'anchored_position', 'VEC2', 'VEC2 %s %s' % (n(star_x0), n(star_y0)),
                       'lerp(%s,%s,i)' % (star_x, star_y)))
    star_size = '((150+150*%s)+4900*%s)' % (TO_CENTER, GROW)
    lines.extend(track('Star 大きさ', 'Star', 'RectTransformComponent',
                       'size_delta', 'VEC2', 'VEC2 150 150',
                       'lerp(%s,%s,i)' % (star_size, star_size)))
    lines.extend(track('Star 濃さ', 'Star', 'UIImageComponent', 'opacity', 'FLOAT',
                       'FLOAT 0', '%s*(0.85+0.15*%s)' % (DECO_A, TO_CENTER)))

    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    os.makedirs(OUT_DIR, exist_ok=True)
    open(SCENE_PATH, 'w', encoding='utf-8', newline='\n').write(build_scene())
    open(MOTION_PATH, 'w', encoding='utf-8', newline='\n').write(build_motion())
    print('wrote', SCENE_PATH)
    print('wrote', MOTION_PATH)
