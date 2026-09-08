"""SWORD CLASH の native scene を生成する。ゲームルールは C# 側にあり、ここには無い。

横スクロールの対戦。奥行きは使わないので Z は 0 に固定し、
Rigidbody の freeze_position で engine 側からも押さえる。
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'resources/Game/SwordClash'
OUT.mkdir(parents=True, exist_ok=True)

SCRIPT_TYPE_GUID = '7a3e1c94b06d4f28ae5137c0d9b28e41'   # ScriptComponent の型 GUID

BLUE   = (.38, .74, 1.0, 1)
RED    = (1.0, .42, .44, 1)
STEEL  = (.78, .84, .94, 1)
GOLD   = (1.0, .82, .35, 1)
INK    = (.04, .05, .10, 1)
CARD   = (.10, .14, .24, .95)
CREAM  = (.94, .97, 1.0, 1)
MUTED  = (.60, .70, .85, 1)
DECK   = (.46, .52, .66, 1)
LEDGE  = (.56, .66, .86, 1)
GAUGE  = (.10, .13, .22, 1)


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


def script(class_name, guid, order=0, extra=()):
    """ScriptComponent 1 つ。extra には field.xxx の初期値を渡せる。"""
    return comp('ScriptComponent',
                prop('__script.language', 'enum', 1),
                prop('__script.asset', 'assetref', ''),
                prop('__script.class', 'string', 'Game.SwordClash.' + class_name),
                prop('__script.execution_order', 'int', order),
                prop('__script.type_id', 'string', guid),
                *extra)


class Scene:
    def __init__(self, title):
        self.title = title
        self.objects = []
        self.ids = {}

    def obj(self, name, components=(), pos=(0, 0, 0), scale=(1, 1, 1), rot=(0, 0, 0),
            parent=0, enabled=True, key=None):
        # key は生成器の中だけで使う識別名。同名の子（Blade など）を
        # 2 人ぶん作れるようにするため、Scene へ書く name とは分けてある。
        identifier = len(self.objects) + 1
        key = key or name
        assert key not in self.ids, key
        self.ids[key] = identifier
        self.objects.append((identifier, name, parent, pos, rot, scale, components, enabled))
        return identifier

    def mesh(self, name, kind, pos, scale, color, lit=True, parent=0, rot=(0, 0, 0),
             visible=True, extra=(), shadow=True, key=None):
        renderer = comp('PrimitiveMeshRendererComponent',
                        prop('primitive_type', 'enum', kind),
                        prop('material_override', 'bool', True),
                        prop('tint', 'color', color),
                        prop('shading_model', 'enum', 1 if lit else 3),
                        prop('cast_shadow', 'bool', shadow),
                        prop('receive_shadow', 'bool', shadow),
                        prop('visible', 'bool', visible))
        return self.obj(name, [renderer, *extra], pos, scale, rot, parent, key=key)

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

    def image(self, name, x, y, w, h, color, parent=None, enabled=True, fill=1.0):
        return self.rect(name, x, y, w, h, parent, enabled,
                         [comp('UIImageComponent', prop('color', 'color', color),
                               prop('fill_amount', 'float', fill))])

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


# ---- Component ヘルパ -------------------------------------------------------

def rigidbody():
    # 横スクロールなので Z への移動と全軸の回転を engine 側で止める。
    # C# で毎フレーム押し戻すより、Solver に食わせた方が暴れない。
    return comp('RigidbodyComponent',
                prop('body_type', 'enum', 2),
                prop('mass', 'float', 1.0),
                prop('linear_damping', 'float', .02),
                prop('angular_damping', 'float', 1.0),
                prop('gravity_scale', 'float', 2.35),
                prop('restitution', 'float', 0.0),
                prop('friction', 'float', .30),
                prop('freeze_position', 'vec3', (0, 0, 1)),
                prop('freeze_rotation', 'vec3', (1, 1, 1)),
                prop('use_ccd', 'bool', True))


def box_collider(size=(1, 1, 1), trigger=False):
    return comp('BoxColliderComponent',
                prop('size', 'vec3', size),
                prop('center_offset', 'vec3', (0, 0, 0)),
                prop('is_trigger', 'bool', trigger),
                prop('collision_layer', 'int', 0),
                prop('collision_mask', 'int', -1))


def audio():
    # clip は載せない。Play しても鳴らないが、差し替えるだけで鳴るようにしておく。
    return comp('AudioSourceComponent',
                prop('clip_path', 'assetref', ''),
                prop('loop', 'bool', False),
                prop('volume', 'float', .5),
                prop('pitch', 'float', 1.0),
                prop('play_on_start', 'bool', False),
                prop('spatial', 'bool', False))


def trail(color):
    # 剣先の軌跡。world_space にしないと親の回転で帯ごと回ってしまう。
    return comp('TrailComponent',
                prop('emitting', 'bool', False),
                prop('lifetime', 'float', .22),
                prop('min_distance', 'float', .05),
                prop('max_points', 'int', 48),
                prop('world_space', 'bool', True),
                prop('width_start', 'float', .30),
                prop('width_end', 'float', .02),
                prop('color', 'color', color),
                prop('fill_color', 'color', color),
                prop('intensity', 'float', 3.4),
                prop('billboard', 'bool', True),
                prop('cast_shadows', 'bool', False))


def sparks(color):
    # 常時は止めておき、当たった瞬間に C# から Emit する。
    return comp('ParticleEmitterComponent',
                prop('emitting', 'bool', False),
                prop('spawn_rate', 'float', 0.0),
                prop('lifetime', 'float', .45),
                prop('start_speed', 'float', 7.5),
                prop('gravity', 'float', 9.0),
                prop('drag', 'float', 1.4),
                prop('start_size', 'float', .16),
                prop('end_size', 'float', .01),
                prop('start_color', 'color', color),
                prop('end_color', 'color', (color[0], color[1], color[2], 0)),
                prop('direction', 'vec3', (0, 1, 0)),
                prop('cone_angle', 'float', 180.0),
                prop('blend_mode', 'enum', 1),
                prop('max_particles', 'int', 220))


def point_light(color, intensity=1.1, rng=4.5):
    return comp('PointLightComponent',
                prop('color', 'color', color),
                prop('intensity', 'float', intensity),
                prop('range', 'float', rng),
                prop('cast_shadows', 'bool', False))


def effect(index, kind, intensity, **extra):
    """画面エフェクト 1 枚ぶんのプロパティ。名前は effects[N].xxx。"""
    name = f'effects[{index}].'
    # 種類の property 名は kind ではなく type。C++ 側の綴りに合わせる。
    out = [prop(name + 'enabled', 'bool', True),
           prop(name + 'type', 'enum', kind),
           prop(name + 'intensity', 'float', intensity)]
    for key, v in extra.items():
        kind_name = 'color' if key.startswith('color') else (
            'vec2' if key == 'direction' else 'float')
        out.append(prop(name + key, kind_name, v))
    return out


def screen_effects():
    # 並び順が C# の添字になる。0=ビネット 1=色収差 2=グリッチ。
    # Director がこの 3 枚の intensity を動かす。種類は増やさない。
    props = [prop('enabled', 'bool', True),
             prop('use_preset', 'bool', False),
             prop('effect_count', 'int', 3),
             prop('apply_stage', 'enum', 0),
             prop('target_mode', 'enum', 0),
             prop('target_rendering_layer', 'int', 0)]
    props += effect(0, 15, .18, radius=.95, softness=.70)      # Vignette
    props += effect(1, 9, .06, amount=1.0)                      # ChromaticAberration
    props += effect(2, 37, .0, amount=1.0, speed=8.0, seed=7.0)  # Glitch
    return comp('ScreenEffectStackComponent', *props)


def build():
    s = Scene('Sword Clash')

    # ---- カメラ -----------------------------------------------------------
    # Rig が動き、子の Camera が描く。Rig を動かす方が画角と分けて扱える。
    rig = s.obj('CameraRig', [script('SwordClashCamera', 'b52d8f14e6c73a09d81f4b62c095a7e3')],
                pos=(0, 3, -17))
    s.obj('Camera', [comp('CameraComponent',
                          prop('projection_mode', 'enum', 0),
                          prop('field_of_view_degrees', 'float', 60),
                          prop('near_clip', 'float', .1),
                          prop('far_clip', 'float', 400),
                          prop('priority', 'int', 100))], parent=rig)

    # 画面全体の後処理。Camera の子ではなく独立に置いて Director から触る。
    s.obj('ScreenFx', [screen_effects()])

    # ---- 光と空 -----------------------------------------------------------
    s.obj('Sun', [comp('DirectionalLightComponent',
                       prop('color', 'color', (1, .96, .89, 1)),
                       prop('intensity', 'float', 5.6),
                       prop('cast_shadows', 'bool', True))], rot=(.95, -.5, 0))
    s.obj('Rim', [comp('DirectionalLightComponent',
                       prop('color', 'color', (.52, .66, 1.0, 1)),
                       prop('intensity', 'float', 2.8),
                       prop('cast_shadows', 'bool', False))], rot=(-.35, 2.5, 0))

    s.obj('Front', [comp('DirectionalLightComponent',
                         prop('color', 'color', (1.0, .96, .92, 1)),
                         prop('intensity', 'float', 2.0),
                         prop('cast_shadows', 'bool', False))], rot=(.25, .1, 0))

    s.obj('Sky', [comp('SkyboxComponent',
                       prop('sky_enabled', 'bool', True),
                       prop('intensity', 'float', 1.0),
                       prop('clouds_enabled', 'bool', True),
                       prop('cloud_layer1_speed', 'float', .012),
                       prop('cloud_layer1_scale', 'float', 2.2),
                       prop('cloud_layer1_density', 'float', .35),
                       prop('cloud_layer1_color', 'color', (.55, .68, .92, 1)),
                       prop('stars_enabled', 'bool', True),
                       prop('star_density', 'float', .5),
                       prop('star_intensity', 'float', .7))])

    # ---- ステージ ---------------------------------------------------------
    # 落ちたら撃墜。足場は 4 枚だけにして間合いを覚えやすくする。
    s.mesh('Deck', 1, (0, -.5, 0), (18, 1, 4), DECK, extra=[box_collider((18, 1, 4))])
    for name, x, y, w in [('LedgeL', -5.6, 3.4, 4.2), ('LedgeR', 5.6, 3.4, 4.2),
                          ('LedgeTop', 0, 6.7, 5.0)]:
        s.mesh(name, 1, (x, y, 0), (w, .45, 3), LEDGE, extra=[box_collider((w, .45, 3))])

    # 奥の飾り。当たり判定は付けない。奥行きの手掛かりだけ担う。
    for i in range(9):
        x = -16 + i * 4
        s.mesh('Pillar' + str(i), 1, (x, 5.5, 9.0), (1.1, 15, 1.1),
               (.30, .36, .50, 1), shadow=False)
    s.mesh('Backdrop', 1, (0, 6, 13.0), (46, 30, .5), (.22, .28, .44, 1), shadow=False)

    # ---- ファイター -------------------------------------------------------
    stage = s.obj('Fighters')
    for index, (key, color, x, second, brain) in enumerate([
            ('Fighter1', BLUE, -4.2, False, False),
            ('Fighter2', RED, 4.2, True, True)]):
        # secondPlayer を立てた方は矢印キー側になる。Inspector の初期値として渡す。
        extra = (prop('field.secondPlayer', 'bool', True),) if second else ()
        parts = [rigidbody(), box_collider((.9, 1.7, .9)), audio(),
                 script('SwordClashFighter', '9c1f6a3d84b25e70af38d1c62b45e9f7',
                        order=index, extra=extra)]
        if brain:
            parts.append(script('SwordClashBrain', '3e7b25c9f0a648d1b93c7e5a2f81d604',
                                order=10 + index))

        body = s.mesh(key, 4, (x, .90, 0), (.9, 1.7, .9), color,
                      parent=stage, extra=parts, key=key)

        # 剣。振りは C# が localEulerAngles で回す。帯は Trail が引く。
        s.mesh('Blade', 1, (.62, .18, 0), (1.35, .10, .10), STEEL,
               parent=body, extra=[trail(GOLD)], shadow=False, key=key + 'Blade')

        # 当たった瞬間の火花と、構えを知らせる光。どちらも C# から動かす。
        s.obj('Sparks', [sparks(GOLD)], pos=(0, .2, 0), parent=body, key=key + 'Sparks')
        s.obj('Glow', [point_light(color, 1.1, 5.0)], pos=(0, .4, 0), parent=body,
              key=key + 'Glow')

    # ---- 進行役 -----------------------------------------------------------
    s.obj('Director', [script('SwordClashGame', '1a48d93e7c0b562f8e41ad35b9726c08',
                              order=100)])

    # ---- UI ---------------------------------------------------------------
    s.canvas()
    hud = s.rect('Hud', 0, 0, 1600, 900, enabled=False)

    for side, (label, color, x) in enumerate([('1P', BLUE, -470), ('CPU', RED, 470)]):
        n = str(side + 1)
        s.image('Panel' + n, x, -350, 420, 150, CARD, hud)
        s.text('Name' + n, label, x - 150, -395, 140, 44, 26, color, hud)
        s.text('Damage' + n + 'Value', '0%', x + 70, -378, 260, 88, 60, CREAM, hud)
        s.text('Stock' + n + 'Value', '◆◆◆', x - 150, -348, 160, 40, 24, color, hud)
        s.image('Gauge' + n + 'Back', x, -300, 380, 14, GAUGE, hud)
        s.image('Gauge' + n + 'Fill', x, -300, 380, 14, color, hud, fill=0.0)

    s.text('Hint', 'A / D 移動    SPACE ジャンプ    J 斬り    K + 方向 で必殺',
           0, 372, 1100, 44, 22, MUTED, hud)

    banner = s.rect('Banner', 0, 0, 1600, 900)
    s.image('BannerVeil', 0, 0, 1600, 900, (.01, .02, .05, .72), banner)
    s.image('BannerCard', 0, 0, 1120, 560, CARD, banner)
    s.image('BannerEdgeL', -555, 0, 10, 560, BLUE, banner)
    s.image('BannerEdgeR', 555, 0, 10, 560, RED, banner)
    s.text('BannerTitle', 'SWORD CLASH', 0, -150, 1040, 130, 76, CREAM, banner)
    s.text('BannerBody', 'SPACE ではじめる', 0, 60, 1000, 300, 26, MUTED, banner)

    s.save('SwordClash.replayscene')


if __name__ == '__main__':
    build()
