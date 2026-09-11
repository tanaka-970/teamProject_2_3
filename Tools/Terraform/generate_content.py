"""TERRAFORM の native scene を生成する。ゲームルールは C# 側にあり、ここには無い。

地形は LandscapeComponent の inline mesh として書き出す。
形式はエンジン既存の LandscapeData::SerializeInline と同じ RPLM2。
初期地形はなだらかな起伏だけで、謎を解く形は作らない。
プレイヤーが C# から彫って作る。
"""
from pathlib import Path
import math

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'resources/Game/Terraform'
OUT.mkdir(parents=True, exist_ok=True)

SCRIPT_TYPE_GUID = '7a3e1c94b06d4f28ae5137c0d9b28e41'

JADE = (.35, .92, .72, 1)
SUN = (1.0, .82, .38, 1)
STONE = (.42, .45, .52, 1)
INK = (.03, .05, .08, 1)
CARD = (.05, .08, .13, .95)
CREAM = (.95, .97, .99, 1)
MUTED = (.63, .72, .82, 1)
SOIL = (.46, .60, .42, 1)


def q(s):
    return '"' + str(s).replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'


def value(v):
    if isinstance(v, (tuple, list)):
        return ' '.join(str(n) for n in v)
    if isinstance(v, bool):
        return '1' if v else '0'
    return str(v)


def prop(name, kind, v):
    return (name, kind, q(v) if kind in ('string', 'assetref') else value(v))


def comp(name, *properties):
    return (name, properties)


def script(class_name, guid, order=0):
    return comp('ScriptComponent',
                prop('__script.language', 'enum', 1),
                prop('__script.asset', 'assetref', ''),
                prop('__script.class', 'string', 'Game.Terraform.' + class_name),
                prop('__script.execution_order', 'int', order),
                prop('__script.type_id', 'string', guid))


# オーブは Inspector の hint を 1 つずつ変える。
# script() は共通の 5 つしか渡さないので、専用のヘルパにする。
def orb_script(hint):
    return comp('ScriptComponent',
                prop('__script.language', 'enum', 1),
                prop('__script.asset', 'assetref', ''),
                prop('__script.class', 'string', 'Game.Terraform.TerraformOrb'),
                prop('__script.execution_order', 'int', 0),
                prop('__script.type_id', 'string', '4a1b6c8d13e95f27a0c4b69d8e3bf502'),
                prop('field.hint', 'string', hint))


def landscape_inline(resolution, cell):
    """エンジンの LandscapeData::SerializeInline と同じ RPLM2 形式を作る。

    なだらかな起伏だけを入れる。オーブへ届く形は作らない。
    """
    width = height = resolution
    half = (resolution - 1) * 0.5
    vertices = []
    for z in range(height):
        for x in range(width):
            wx = (x - half) * cell
            wz = (z - half) * cell
            # 低い丘をいくつか。急な壁は作らない。
            y = (math.sin(wx * 0.055) * math.cos(wz * 0.048) * 1.9
                 + math.sin((wx + wz) * 0.031) * 1.15)
            # 外周へ向かってゆるく持ち上げ、場外が壁のように見えるようにする。
            radius = math.hypot(wx, wz)
            if radius > 40.0:
                y += (radius - 40.0) * 0.42
            vertices.append((wx, y, wz, x / (width - 1), z / (height - 1)))

    indices = []
    for z in range(height - 1):
        for x in range(width - 1):
            a = z * width + x
            b = a + 1
            c = a + width
            d = c + 1
            indices += [a, c, b, b, c, d]

    parts = ['RPLM2', str(width), str(height), repr(float(cell)),
             str(len(vertices)), str(len(indices))]
    for vx, vy, vz, u, v in vertices:
        parts += [repr(round(vx, 5)), repr(round(vy, 5)), repr(round(vz, 5)),
                  repr(round(u, 6)), repr(round(v, 6))]
    parts += [str(i) for i in indices]
    return ' '.join(parts)


class Scene:
    def __init__(self, title):
        self.title = title
        self.objects = []
        self.ids = {}

    def obj(self, name, components=(), pos=(0, 0, 0), scale=(1, 1, 1), rot=(0, 0, 0),
            parent=0, enabled=True):
        identifier = len(self.objects) + 1
        assert name not in self.ids, name
        self.ids[name] = identifier
        self.objects.append((identifier, name, parent, pos, rot, scale, components, enabled))
        return identifier

    def mesh(self, name, kind, pos, scale, color, lit=True, parent=0, rot=(0, 0, 0),
             visible=True, extra=(), shadow=True):
        renderer = comp('PrimitiveMeshRendererComponent',
                        prop('primitive_type', 'enum', kind),
                        prop('material_override', 'bool', True),
                        prop('tint', 'color', color),
                        prop('shading_model', 'enum', 1 if lit else 3),
                        prop('cast_shadow', 'bool', shadow),
                        prop('receive_shadow', 'bool', shadow),
                        prop('visible', 'bool', visible))
        return self.obj(name, [renderer, *extra], pos, scale, rot, parent)

    def canvas(self):
        self.obj('Canvas', [
            comp('RectTransformComponent',
                 prop('anchor_min', 'vec2', (0, 0)), prop('anchor_max', 'vec2', (1, 1)),
                 prop('size_delta', 'vec2', (0, 0))),
            comp('CanvasComponent',
                 prop('reference_resolution', 'vec2', (1600, 900)),
                 prop('render_mode', 'enum', 0), prop('scale_mode', 'enum', 1),
                 prop('match_width_or_height', 'float', .5), prop('sort_order', 'int', 100))])

    def rect(self, name, x=0, y=0, w=1600, h=900, parent=None, enabled=True, extra=()):
        if parent is None:
            parent = self.ids['Canvas']
        return self.obj(name, [
            comp('RectTransformComponent',
                 prop('anchor_min', 'vec2', (.5, .5)), prop('anchor_max', 'vec2', (.5, .5)),
                 prop('anchored_position', 'vec2', (x, -y)), prop('size_delta', 'vec2', (w, h)),
                 prop('pivot', 'vec2', (.5, .5)), prop('scale', 'vec2', (1, 1)),
                 prop('rotation', 'float', 0), prop('sort_order', 'int', 0)),
            *extra], parent=parent, enabled=enabled)

    def image(self, name, x, y, w, h, color, parent=None, enabled=True):
        return self.rect(name, x, y, w, h, parent, enabled,
                         [comp('UIImageComponent', prop('color', 'color', color),
                               prop('fill_amount', 'float', 1))])

    def text(self, name, body, x, y, w, h, size=28, color=CREAM, parent=None):
        return self.rect(name, x, y, w, h, parent, True,
                         [comp('UITextComponent',
                               prop('text', 'string', '<b>' + body + '</b>'),
                               prop('font_size', 'float', size * 1.2),
                               prop('rich_text', 'bool', True),
                               prop('color', 'color', color),
                               prop('horizontal_align', 'enum', 1),
                               prop('vertical_align', 'enum', 1),
                               prop('word_wrap', 'bool', True))])

    def save(self, filename):
        lines = ['REPLAY_SCENE 11', f'SCENE {q(self.title)}', 'SCENE_STATE 0',
                 'COLLISION_STATE 2 0', f'OBJECT_COUNT {len(self.objects)}']
        for oid, name, parent, pos, rot, scale, components, enabled in self.objects:
            lines += ['OBJECT', f'  ID {oid}', f'  NAME {q(name)}',
                      f'  ENABLED {int(enabled)}', f'  PARENT {parent}',
                      '  TRANSFORM ' + value((*pos, *rot, *scale)),
                      '  PREFAB "" 0 0', f'  COMPONENT_COUNT {len(components)}']
            for cid, (cn, props) in enumerate(components, 1):
                is_script = cn == 'ScriptComponent'
                lines += [f'  COMPONENT {q(cn)} 1', f'    STABLE_ID {cid}',
                          '    TYPE_GUID ' + q(SCRIPT_TYPE_GUID if is_script else '0' * 32),
                          '    TYPE_MODULE ' + q('RePlayEngine.Scripting' if is_script else ''),
                          '    TYPE_VERSION 1', f'    PROPERTY_COUNT {len(props)}']
                lines += [f'    PROPERTY {q(n)} {t} {v}' for n, t, v in props]
                lines += ['  END_COMPONENT']
            lines += ['END_OBJECT']
        lines += ['END_SCENE']
        (OUT / filename).write_text('\n'.join(lines) + '\n', encoding='utf-8')
        print(filename, len(self.objects), 'objects')


def build():
    s = Scene('Terraform')

    resolution = 97
    cell = 1.15

    # ---- 地形 -------------------------------------------------------------
    #
    # LandscapeComponent が形の正本。Renderer と Collider は
    # LandscapeData の Revision を見て自動で作り直す。
    # C# から高さを変えると、見た目も当たり判定も同じフレームで追従する。
    s.obj('Ground', [
        comp('LandscapeComponent',
             prop('default_resolution', 'int', resolution),
             prop('default_cell_size', 'float', cell),
             prop('source_model_asset', 'string', ''),
             prop('mesh_data', 'string', landscape_inline(resolution, cell))),
        comp('LandscapeRendererComponent',
             prop('tint', 'color', SOIL),
             prop('visible', 'bool', True),
             prop('cast_shadow', 'bool', False),
             # 地形は 110m 四方あり、1 枚のシャドウマップでは精度が足りない。
             # 受けさせると自分の影で黒い縞（シャドウアクネ）が出るので切る。
             prop('receive_shadow', 'bool', False)),
        comp('LandscapeColliderComponent',
             prop('collision_layer', 'int', 0),
             prop('collision_mask', 'int', -1)),
    ])

    # ---- 光と空 -----------------------------------------------------------
    s.obj('Sun', [comp('DirectionalLightComponent',
                       prop('color', 'color', (1, .96, .88, 1)),
                       prop('intensity', 'float', 4.6),
                       prop('cast_shadows', 'bool', True))], rot=(.9, -.6, 0))
    s.obj('Fill', [comp('DirectionalLightComponent',
                        prop('color', 'color', (.62, .74, 1.0, 1)),
                        prop('intensity', 'float', 2.0),
                        prop('cast_shadows', 'bool', False))], rot=(-.35, 2.4, 0))
    s.mesh('Sky', 2, (0, 0, 0), (300, 300, 300), (.42, .62, .86, 1), lit=False, shadow=False)

    # ---- カメラ（三人称・背後追従） ---------------------------------------
    rig = s.obj('CameraRig', [script('TerraformCamera', '6c3d8ea035ab7149c2e6d8bf0a5d1724')],
                pos=(0, 3.2, -32.0))
    s.obj('Camera', [comp('CameraComponent',
                          prop('projection_mode', 'enum', 0),
                          prop('field_of_view_degrees', 'float', 66),
                          prop('near_clip', 'float', .08),
                          prop('far_clip', 'float', 500),
                          prop('priority', 'int', 100))], parent=rig)

    # ---- プレイヤー -------------------------------------------------------
    #
    # 地形は毎フレーム形が変わるので Rigidbody に任せず、
    # C# が Landscape.SampleWorldHeight で足元を取って乗せる。
    s.mesh('Player', 3, (0, 2.0, -24.0), (1.2, 1.2, 1.2), JADE,
           extra=[script('TerraformWalker', '3f9a5b7c02d84e16f9b3a58c7d2ae491'),
                  script('TerraformSculptor', '2e8f4a6b91c73d05e8a2f47b6c19d380')])

    # 狙っている場所の印。C# が位置と大きさを毎フレーム更新する。
    s.mesh('AimMarker', 4, (0, -50, 0), (6.8, .06, 6.8), SUN, lit=False, shadow=False)

    # ---- オーブ -----------------------------------------------------------
    #
    # 位置は動かさない。届く形はプレイヤーが地形で作る。
    # どれも解き方を 1 つに決めていない。
    orbs = s.obj('Orbs')
    layout = [
        # (x, y, z, ヒント)
        (16.0, 15.5, 8.0, '高いところに光がある'),
        (-20.0, -4.2, 14.0, '地面の下から光が漏れている'),
        (26.0, 9.0, -20.0, '谷の向こう側に光がある'),
        (-14.0, 21.0, -26.0, 'ずっと高いところに光がある'),
    ]
    for index, (x, y, z, hint) in enumerate(layout):
        s.mesh('Orb' + str(index), 2, (x, y, z), (1.5, 1.5, 1.5), JADE, lit=False,
               parent=orbs, shadow=False, extra=[orb_script(hint)])
        # 光の柱。遠くからでもオーブの位置が分かるようにする。
        s.mesh('OrbBeam' + str(index), 4, (x, y - 30.0, z), (0.5, 60.0, 0.5),
               (JADE[0], JADE[1], JADE[2], .18), lit=False, parent=orbs, shadow=False)

    # ---- ゲート -----------------------------------------------------------
    s.mesh('Gate', 4, (0, 6.0, 0), (3.2, 12.0, 3.2), STONE)

    # ---- 進行役 -----------------------------------------------------------
    s.obj('Director', [
        script('TerraformGame', '5b2c7d9e24fa6038b1d5c7ae9f4c0613', order=100),
        script('TerraformDemo', '7d4e9fa146bc825ad3f7e9c01b6e2835', order=200),
    ])

    # ---- UI ---------------------------------------------------------------
    s.canvas()

    hud = s.rect('Hud', 0, 0, 1600, 900, enabled=False)
    s.image('OrbPanel', -640, -376, 260, 92, CARD, hud)
    s.text('OrbLabel', 'ORB', -730, -396, 100, 32, 21, MUTED, hud)
    s.text('OrbValue', '0 / 4', -610, -372, 200, 58, 40, JADE, hud)

    s.image('TimePanel', 640, -376, 240, 92, CARD, hud)
    s.text('TimeLabel', 'TIME', 560, -396, 100, 32, 21, MUTED, hud)
    s.text('TimeValue', '0:00', 680, -372, 160, 58, 38, CREAM, hud)

    s.text('HintText', '', 0, -318, 1000, 44, 24, SUN, hud)
    s.text('Controls',
           '左クリック 盛る    右クリック 削る    F ならす    ホイール 大きさ    R やり直し',
           0, 376, 1200, 44, 21, MUTED, hud)

    # 画面中央の照準。
    s.image('Crosshair', 0, 0, 8, 8, (1, 1, 1, .55), hud)

    banner = s.rect('Banner', 0, 0, 1600, 900)
    s.image('BannerVeil', 0, 0, 1600, 900, (.01, .02, .04, .72), banner)
    s.image('BannerCard', 0, 0, 1120, 620, CARD, banner)
    s.image('BannerAccentA', -545, 0, 10, 620, JADE, banner)
    s.image('BannerAccentB', 545, 0, 10, 620, SUN, banner)
    s.text('BannerTitle', 'TERRAFORM', 0, -160, 1040, 130, 76, CREAM, banner)
    s.text('BannerBody', 'SPACE ではじめる', 0, 50, 1000, 340, 26, MUTED, banner)

    s.save('Terraform.replayscene')


if __name__ == '__main__':
    build()
