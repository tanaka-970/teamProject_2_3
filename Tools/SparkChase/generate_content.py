"""SPARK CHASE の native scene を生成する。ゲームルールは C# 側にあり、ここには無い。

三人称の背後追従カメラ前提で組む。見下ろし視点にはしない。
"""
from pathlib import Path
import math, random

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'resources/Game/SparkChase'
OUT.mkdir(parents=True, exist_ok=True)

SCRIPT_TYPE_GUID = '7a3e1c94b06d4f28ae5137c0d9b28e41'   # ScriptComponent の型 GUID

CYAN   = (.24, .93, .96, 1)
AMBER  = (1.0, .74, .24, 1)
ROSE   = (.96, .32, .45, 1)
INK    = (.03, .05, .09, 1)
CARD   = (.06, .09, .16, .95)
CREAM  = (.94, .97, 1.0, 1)
MUTED  = (.62, .72, .85, 1)
FLOOR  = (.13, .17, .27, 1)
EDGE   = (.30, .46, .72, 1)


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
                prop('__script.class', 'string', 'Game.SparkChase.' + class_name),
                prop('__script.execution_order', 'int', order),
                prop('__script.type_id', 'string', guid))


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

    # ---- UI ----------------------------------------------------------------
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

    def text(self, name, body, x, y, w, h, size=28, color=CREAM, parent=None, enabled=True):
        return self.rect(name, x, y, w, h, parent, enabled,
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


def rigidbody(mass=1.0, damping=.35, gravity=1.0):
    return comp('RigidbodyComponent',
                prop('body_type', 'enum', 2),          # Dynamic
                prop('mass', 'float', mass),
                prop('linear_damping', 'float', damping),
                prop('angular_damping', 'float', .5),
                prop('gravity_scale', 'float', gravity),
                prop('restitution', 'float', .15),
                prop('friction', 'float', .45),
                prop('use_ccd', 'bool', True))


def sphere_collider(radius=.5, trigger=False):
    return comp('SphereColliderComponent',
                prop('radius', 'float', radius),
                prop('center_offset', 'vec3', (0, 0, 0)),
                prop('is_trigger', 'bool', trigger),
                prop('collision_layer', 'int', 0),
                prop('collision_mask', 'int', -1))


def box_collider(size=(1, 1, 1), trigger=False):
    return comp('BoxColliderComponent',
                prop('size', 'vec3', size),
                prop('center_offset', 'vec3', (0, 0, 0)),
                prop('is_trigger', 'bool', trigger),
                prop('collision_layer', 'int', 0),
                prop('collision_mask', 'int', -1))


def build():
    s = Scene('Spark Chase')

    # ---- カメラ（三人称・背後追従） -------------------------------------
    #
    # CameraRig が動き、その子の Camera が実際の描画をする。
    # 見下ろしにならないよう、Rig はプレイヤーとほぼ同じ高さに置く。
    rig = s.obj('CameraRig', [script('SparkChaseCamera', '8d6ec05249f78bab0d25cf7391b64f85')],
                pos=(0, 2.4, -7.4))
    s.obj('Camera', [comp('CameraComponent',
                          prop('projection_mode', 'enum', 0),
                          prop('field_of_view_degrees', 'float', 68),
                          prop('near_clip', 'float', .08),
                          prop('far_clip', 'float', 400),
                          prop('priority', 'int', 100))], parent=rig)

    # ---- 光と背景 ---------------------------------------------------------
    s.obj('Sun', [comp('DirectionalLightComponent',
                       prop('color', 'color', (1, .95, .86, 1)),
                       prop('intensity', 'float', 2.6),
                       prop('cast_shadows', 'bool', True))], rot=(.85, -.55, 0))
    s.obj('Fill', [comp('DirectionalLightComponent',
                        prop('color', 'color', (.42, .58, .95, 1)),
                        prop('intensity', 'float', 1.1),
                        prop('cast_shadows', 'bool', False))], rot=(-.4, 2.3, 0))

    # 内側から見る空。前面カリングされるので背景として働く。
    s.mesh('Sky', 2, (0, 0, 0), (260, 260, 260), (.05, .08, .16, 1), lit=False, shadow=False)

    # ---- 足場 -------------------------------------------------------------
    s.mesh('Ground', 1, (0, -.5, 0), (40, 1, 40), FLOOR,
           extra=[box_collider((40, 1, 40))])
    for i, (x, z, sx, sz) in enumerate([(0, 19.5, 40, 1), (0, -19.5, 40, 1),
                                        (-19.5, 0, 1, 40), (19.5, 0, 1, 40)]):
        s.mesh('Wall' + str(i), 1, (x, 1.0, z), (sx, 3, sz), EDGE,
               extra=[box_collider((sx, 3, sz))])

    # 走り回る楽しさのための起伏。三人称視点なので高さがそのまま見応えになる。
    rng = random.Random(20260907)
    for i in range(14):
        angle = i * math.tau / 14
        radius = 8.5 + rng.random() * 6.5
        x, z = math.sin(angle) * radius, math.cos(angle) * radius
        height = 1.2 + rng.random() * 2.6
        s.mesh('Block' + str(i), 1, (x, height * .5 - .1, z), (2.4, height, 2.4),
               (.18 + rng.random() * .08, .24, .38, 1),
               extra=[box_collider((2.4, height, 2.4))])

    for i in range(10):
        angle = i * math.tau / 10 + .3
        radius = 16.5
        x, z = math.sin(angle) * radius, math.cos(angle) * radius
        s.mesh('Pillar' + str(i), 4, (x, 3.0, z), (1.1, 6.0, 1.1), EDGE,
               extra=[box_collider((1.1, 6.0, 1.1))])

    # ---- プレイヤー -------------------------------------------------------
    s.mesh('Player', 3, (0, 1.1, 0), (1.0, 1.0, 1.0), CYAN,
           extra=[rigidbody(mass=1.15, damping=.55),
                  sphere_collider(.5),
                  script('SparkChasePlayer', '5a3b9d2f16c45e78baf29d406e831c52')])

    # ---- 集めものと敵 -----------------------------------------------------
    field = s.obj('Field')
    for i in range(18):
        s.mesh('Spark' + str(i), 2, (0, -20, 0), (.42, .42, .42), AMBER, lit=False,
               parent=field, shadow=False,
               extra=[sphere_collider(.55, trigger=True),
                      script('SparkChasePickup', '7c5dbf4138e67a9adc14bf6280a53e74')])
    for i, (x, z) in enumerate([(-13, -13), (13, -13), (0, 14)]):
        s.mesh('Chaser' + str(i), 1, (x, 1.0, z), (1.5, 1.5, 1.5), ROSE,
               parent=field,
               extra=[box_collider((1.5, 1.5, 1.5)),
                      script('SparkChaseChaser', '6b4cae3027d56f89cb03ae517f942d63')])

    # ---- 進行役 -----------------------------------------------------------
    # execution_order を後ろにして、Player / Pickup の Awake より後に走らせる。
    s.obj('Director', [script('SparkChaseGame', '4f2a8c1e05b34d67a9e18c3f5d720b41', order=100)])

    # ---- UI ---------------------------------------------------------------
    s.canvas()

    hud = s.rect('Hud', 0, 0, 1600, 900, enabled=False)
    s.image('ScorePanel', -600, -372, 300, 96, CARD, hud)
    s.text('ScoreLabel', 'SCORE', -700, -392, 120, 34, 22, MUTED, hud)
    s.text('ScoreValue', '0', -560, -368, 180, 62, 46, CYAN, hud)

    s.image('TimerPanel', 0, -372, 220, 96, CARD, hud)
    s.text('TimerLabel', 'TIME', 0, -398, 160, 30, 20, MUTED, hud)
    s.text('TimerValue', '1:15', 0, -362, 200, 58, 42, CREAM, hud)

    s.image('LifePanel', 600, -372, 300, 96, CARD, hud)
    s.text('LifeLabel', 'LIFE', 700, -392, 120, 34, 22, MUTED, hud)
    s.text('LifeValue', '♥♥♥', 560, -368, 180, 62, 40, ROSE, hud)

    s.text('Hint', 'WASD 移動    SPACE ジャンプ    マウス 視点', 0, 372, 900, 44, 22, MUTED, hud)

    banner = s.rect('Banner', 0, 0, 1600, 900)
    s.image('BannerVeil', 0, 0, 1600, 900, (.01, .02, .05, .68), banner)
    s.image('BannerCard', 0, 0, 1040, 520, CARD, banner)
    s.image('BannerAccentA', -505, 0, 10, 520, CYAN, banner)
    s.image('BannerAccentB', 505, 0, 10, 520, AMBER, banner)
    s.text('BannerTitle', 'SPARK CHASE', 0, -110, 960, 130, 78, CREAM, banner)
    s.text('BannerBody', 'SPACE ではじめる', 0, 60, 900, 220, 30, MUTED, banner)

    s.save('SparkChase.replayscene')


if __name__ == '__main__':
    build()
