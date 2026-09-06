"""Generate editable native scenes and short original audio cues; no game rules here."""
from pathlib import Path
import math, struct, wave, random

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'resources/Game/GrowRush'
OUT.mkdir(parents=True, exist_ok=True)
MINT = (.13, .87, .64, 1)
ORANGE = (1, .42, .17, 1)
INK = (.035, .09, .10, 1)
CARD = (.055, .13, .14, .98)
CREAM = (.97, .98, .90, 1)
MUTED = (.74, .83, .80, 1)

def q(s): return '"' + str(s).replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'
def value(v):
    if isinstance(v, (tuple,list)): return ' '.join(str(n) for n in v)
    if isinstance(v, bool): return '1' if v else '0'
    return str(v)
def prop(name, kind, v): return (name, kind, q(v) if kind in ('string','assetref') else value(v))
def comp(name, *properties): return (name, properties)

class Scene:
    def __init__(self, title): self.title=title; self.objects=[]; self.ids={}
    def obj(self, name, components=(), pos=(0,0,0), scale=(1,1,1), rot=(0,0,0), parent=0, enabled=True):
        identifier=len(self.objects)+1
        assert name not in self.ids
        self.ids[name]=identifier
        self.objects.append((identifier,name,parent,pos,rot,scale,components,enabled))
        return identifier
    def mesh(self,name,kind,pos,scale,color,lit=True,parent=0,rot=(0,0,0),visible=True):
        return self.obj(name,[comp('PrimitiveMeshRendererComponent',prop('primitive_type','enum',kind),
            prop('material_override','bool',True),prop('tint','color',color),prop('shading_model','enum',1 if lit else 3),
            prop('cast_shadow','bool',False),prop('receive_shadow','bool',False),prop('visible','bool',visible))],pos,scale,rot,parent)
    def rect(self,name,x=0,y=0,w=1600,h=900,parent=None,enabled=True,extra=()):
        if parent is None: parent=self.ids['Canvas']
        return self.obj(name,[comp('RectTransformComponent',prop('anchor_min','vec2',(.5,.5)),prop('anchor_max','vec2',(.5,.5)),
            prop('anchored_position','vec2',(x,-y)),prop('size_delta','vec2',(w,h)),prop('pivot','vec2',(.5,.5)),
            prop('scale','vec2',(1,1)),prop('rotation','float',0),prop('sort_order','int',0)),*extra],parent=parent,enabled=enabled)
    def image(self,name,x,y,w,h,color,parent=None,enabled=True):
        return self.rect(name,x,y,w,h,parent,enabled,[comp('UIImageComponent',prop('color','color',color),prop('fill_amount','float',1))])
    def text(self,name,text,x,y,w,h,size=28,color=CREAM,parent=None):
        return self.rect(name,x,y,w,h,parent,True,[comp('UITextComponent',prop('text','string','<b>'+text+'</b>'),prop('font_size','float',size*1.2),prop('rich_text','bool',True),
            prop('color','color',color),prop('horizontal_align','enum',1),prop('vertical_align','enum',1),prop('word_wrap','bool',False))])
    def button(self,name,label,x,y,w=290,h=76,color=MINT,parent=None):
        node=self.rect(name,x,y,w,h,parent,True,[comp('UIImageComponent',prop('color','color',color)),
            comp('UIButtonComponent',prop('interactable','bool',True),prop('normal_color','color',color),
            prop('hover_color','color',tuple(min(1,c+.12) for c in color[:3])+(1,)),
            prop('pressed_color','color',tuple(c*.72 for c in color[:3])+(1,)))])
        self.text(name+'Label',label,0,0,w-12,h-4,30,INK,node)
        return node
    def canvas(self):
        self.obj('Canvas',[comp('RectTransformComponent',prop('anchor_min','vec2',(0,0)),prop('anchor_max','vec2',(1,1)),
            prop('size_delta','vec2',(0,0))),comp('CanvasComponent',prop('reference_resolution','vec2',(1600,900)),
            prop('render_mode','enum',0),prop('scale_mode','enum',1),prop('match_width_or_height','float',.5),prop('sort_order','int',100))])
    def script(self,typename,guid):
        self.obj('Director',[comp('ScriptComponent',prop('__script.language','enum',1),prop('__script.asset','assetref',''),
            prop('__script.class','string','Game.GrowRush.'+typename),prop('__script.execution_order','int',0),prop('__script.type_id','string',guid))])
    def loading(self):
        root=self.rect('Loading',enabled=False)
        self.image('LoadingPanel',0,360,620,68,INK,root)
        self.text('LoadingText','…',0,360,590,60,27,CREAM,root)
    def save(self,filename):
        lines=['REPLAY_SCENE 11',f'SCENE {q(self.title)}','SCENE_STATE 0','COLLISION_STATE 2 0',f'OBJECT_COUNT {len(self.objects)}']
        for oid,name,parent,pos,rot,scale,components,enabled in self.objects:
            lines += ['OBJECT',f'  ID {oid}',f'  NAME {q(name)}',f'  ENABLED {int(enabled)}',f'  PARENT {parent}',
                '  TRANSFORM '+value((*pos,*rot,*scale)),'  PREFAB "" 0 0',f'  COMPONENT_COUNT {len(components)}']
            for cid,(cn,props) in enumerate(components,1):
                script=cn=='ScriptComponent'
                lines += [f'  COMPONENT {q(cn)} 1',f'    STABLE_ID {cid}',
                    '    TYPE_GUID '+q('7a3e1c94b06d4f28ae5137c0d9b28e41' if script else '0'*32),
                    '    TYPE_MODULE '+q('RePlayEngine.Scripting' if script else ''),'    TYPE_VERSION 1',f'    PROPERTY_COUNT {len(props)}']
                lines += [f'    PROPERTY {q(n)} {t} {v}' for n,t,v in props]
                lines += ['  END_COMPONENT']
            lines += ['END_OBJECT']
        lines+=['END_SCENE']
        (OUT/filename).write_text('\n'.join(lines)+'\n',encoding='utf-8')
        print(filename,len(self.objects),'objects')

def environment(s, arena=False):
    s.obj('Camera',[comp('CameraComponent',prop('projection_mode','enum',0),prop('field_of_view_degrees','float',60),
        prop('near_clip','float',.1),prop('far_clip','float',300),prop('priority','int',100))],pos=(8,5,-13))
    s.obj('Sun',[comp('DirectionalLightComponent',prop('color','color',(1,.98,.89,1)),prop('intensity','float',1.2),prop('cast_shadows','bool',False))],rot=(.8,-.5,0))
    s.mesh('Backdrop',2,(0,0,0),(180,180,180),(.17,.31,.38,1),False)
    # The camera is inside the backdrop sphere (front-face culled): distant silhouettes provide depth.
    s.mesh('Surroundings',1,(0,-.55,0),(95,.8,95),(.19,.28,.23,1))
    s.mesh('ArenaBase',1,(0,-.12,0),(27.8,.4,22.4),(.40,.48,.37,1))
    for i,(x,z,sx,sz) in enumerate([(0,11.2,28,.22),(0,-11.2,28,.22),(-14,0,.22,22.5),(14,0,.22,22.5)]):
        s.mesh('Rim'+str(i),1,(x,.2,z),(sx,.55,sz),(.69,.79,.63,1))
    rng=random.Random(1902)
    for i in range(26):
        a=i*math.tau/26
        x,z=math.sin(a)*(20+rng.random()*8),math.cos(a)*(19+rng.random()*8)
        height=2+rng.random()*6
        s.mesh('SceneryTrunk'+str(i),4,(x,height*.35,z),(.6,height*.7,.6),(.28,.31,.23,1))
        s.mesh('SceneryCrown'+str(i),2,(x,height*.8,z),(2.5,height*.65,2.5),(.26+rng.random()*.12,.43,.35,1))
    for i,(x,z,c) in enumerate([(-6,-6,MINT),(6,6,ORANGE)]):
        s.mesh('SpawnRing'+str(i),4,(x,.11,z),(2.0,.04,2.0),c,False)
    if not arena:
        for i in range(30):
            x=rng.uniform(-10,10);z=rng.uniform(-8,8);height=rng.uniform(.4,2.4);c=MINT if x<0 else ORANGE
            s.mesh('GardenStem'+str(i),4,(x,height*.4,z),(.18,height*.8,.18),(.34,.23,.16,1))
            s.mesh('GardenLeaf'+str(i),2,(x,height,z),(.9,height*.6,.9),c)

def title():
    s=Scene('Grow Rush - Title');environment(s);s.script('GrowTitle','beea44001a1543a2bfe8807732410001');s.canvas()
    s.image('Veil',0,0,1600,900,(.015,.055,.065,.62))
    s.image('TitleCard',0,0,960,580,CARD)
    s.image('MintAccent',-465,0,8,580,MINT);s.image('OrangeAccent',465,0,8,580,ORANGE)
    s.text('TitleEyebrow','WATER  /  GROW  /  WIN',0,-218,820,42,24,MUTED)
    s.text('Logo','GROW RUSH',0,-130,850,110,86)
    s.text('Tagline','水をまいて、育てて、陣地を広げよう。',0,-40,850,52,28)
    s.button('OneMinute','1分',-168,72,290,96,MINT)
    s.button('TwoMinutes','2分',168,72,290,96,ORANGE)
    s.text('MenuControls','WASD 移動     マウス 照準     SPACE 水',0,172,850,50,25,MUTED)
    s.text('MenuRule','芽 1点  →  草 2点  →  茂み 3点  →  樹 5点',0,226,850,44,23,MUTED)
    s.button('Quit','終了',650,376,150,52,(.49,.61,.57,1))
    s.loading();s.save('GrowRush_Title.replayscene')

def arena():
    s=Scene('Grow Rush - Arena');environment(s,True);s.script('GrowArena','beea44001a1543a2bfe8807732410002')
    for i in range(320):
        x=(i%20+.5)*1.35-13.5; z=(i//20+.5)*1.35-10.8
        s.mesh('Plot'+str(i),1,(x,.095,z),(1.30,.04,1.30),(.30,.38,.29,1),False)
        s.mesh('Stem'+str(i),4,(x,.2,z),(.07,.3,.07),MINT,visible=False)
        s.mesh('Leaf'+str(i),2,(x,.35,z),(.25,.2,.25),MINT,visible=False)
    for prefix,c,x,z in [('You',MINT,-6,-6),('Cpu',ORANGE,6,6)]:
        s.mesh(prefix+'Body',3,(x,.95,z),(.85,.9,.85),c)
        s.mesh(prefix+'Visor',1,(x,1.35,z+.4),(.57,.25,.14),INK,False)
        s.mesh(prefix+'Gun',4,(x,1.05,z+.7),(.19,.68,.19),(.83,.93,.90,1),rot=(90,0,0))
        for k in range(6):s.mesh(prefix+'Hp'+str(k),1,(x+(k-2.5)*.16,2.05,z),(.13,.10,.10),c,False)
    for i in range(64):s.mesh('Drop'+str(i),2,(0,-8,0),(.19,.19,.32),MINT,False,visible=False)
    for i in range(16):s.mesh('Splash'+str(i),4,(0,-8,0),(.4,.02,.4),MINT,False,visible=False)
    s.mesh('AimMarker',4,(0,.14,0),(.8,.025,.8),CREAM,False)
    s.canvas()
    s.image('ClockPanel',0,-370,176,100,CARD);s.text('Timer','1:00',0,-370,166,90,58)
    for side,prefix,c in [(-1,'You',MINT),(1,'Cpu',ORANGE)]:
        x=side*470
        s.image(prefix+'HudCard',x,-364,380,104,CARD)
        s.text(prefix+'Name',prefix.upper(),x-120,-384,100,36,23,c)
        s.text(prefix+'Score','0',x,-361,130,70,48,c)
        s.text(prefix+'AreaHud','0%',x+128,-361,110,60,28)
        s.image(prefix+'BarBack',x,-309,380,7,(.11,.21,.20,1))
        s.image(prefix+'Territory',x,-309,380,7,c)
    cross=s.rect('Crosshair')
    s.image('CrossDot',0,0,5,5,CREAM,cross)
    for name,x,y,w,h in [('Left',-13,0,8,3),('Right',13,0,8,3),('Top',0,-13,3,8),('Bottom',0,13,3,8)]:
        s.image('Cross'+name,x,y,w,h,CREAM,cross)
    s.text('HitMark','×',0,0,65,65,56,ORANGE)
    s.image('HealthCard',-575,357,300,90,CARD)
    s.text('HealthLabel','HP',-675,343,66,40,23,MUTED)
    for k in range(6):s.image('HpPip'+str(k),-634+k*31,378,24,12,MINT)
    s.text('ControlHint','WASD 移動   マウス 照準   SPACE 水   ESC 停止',200,386,1030,50,23,CREAM)
    s.text('CenterMessage','3',0,-108,780,145,90)
    pause=s.rect('Pause',enabled=False)
    s.image('PauseVeil',0,0,1600,900,(.015,.05,.06,.82),pause)
    s.image('PauseCard',0,0,640,410,CARD,pause)
    s.text('PauseTitle','PAUSE',0,-110,560,90,60,CREAM,pause)
    s.button('Resume','つづける',0,10,350,78,MINT,pause)
    s.button('BackTitle','タイトル',0,110,350,65,(.61,.73,.66,1),pause)
    s.loading();s.save('GrowRush_Arena.replayscene')

def result():
    s=Scene('Grow Rush - Result');environment(s);s.script('GrowResult','beea44001a1543a2bfe8807732410003');s.canvas()
    s.image('Veil',0,0,1600,900,(.015,.055,.065,.75))
    s.image('ResultCard',0,-15,1120,730,CARD)
    s.text('Duration','1分 MATCH',0,-318,600,44,23,MUTED)
    s.text('ResultTitle','YOU WIN!',0,-241,1000,100,72,MINT)
    for x,prefix,c in [(-305,'You',MINT),(305,'Cpu',ORANGE)]:
        s.text(prefix+'Name',prefix.upper(),x,-146,260,44,25,c)
        s.text(prefix+'Total','0',x,-66,300,112,88,c)
        s.image(prefix+'BarBack',x,8,340,12,(.14,.23,.22,1))
        s.image(prefix+'Bar',x,8,340,12,c)
        s.text(prefix+'Area','0',x,76,260,54,35)
        s.text(prefix+'Growth','+ 0',x,137,260,54,35)
        s.text(prefix+'Trees','0',x,192,260,44,26,MUTED)
    s.text('AreaLabel','面積',0,76,180,50,26,MUTED)
    s.text('GrowthLabel','成長',0,137,180,50,26,MUTED)
    s.text('TreesLabel','樹',0,192,180,44,24,MUTED)
    s.text('ScoreRule','面積 ＋ 成長 ＝ 合計',0,244,900,40,22,MUTED)
    s.button('Retry','もう一度  1分',-228,313,370,74,MINT)
    s.button('Title','タイトル',228,313,370,74,(.67,.77,.70,1))
    s.loading();s.save('GrowRush_Result.replayscene')

def audio():
    folder=OUT/'Audio';folder.mkdir(exist_ok=True)
    for name,freq,duration in [('water',680,.10),('hit',180,.12),('tree',440,.40),('start',660,.24),('tick',880,.08),('finish',330,.48),('win',660,.65)]:
        rng=random.Random(12);samples=[];rate=22050
        for n in range(int(duration*rate)):
            t=n/rate;envelope=min(1,t/.008)*max(0,1-t/duration)**2
            f=freq*(1+t*(2 if name in ('tree','win','start') else -1))
            signal=math.sin(math.tau*f*t)*.35
            if name=='water':signal=signal*.3+(rng.random()*2-1)*.18
            samples.append(struct.pack('<h',int(24000*signal*envelope)))
        with wave.open(str(folder/(name+'.wav')),'wb') as out:
            out.setparams((1,2,rate,0,'NONE','not compressed'));out.writeframes(b''.join(samples))

title();arena();result();audio()
db=ROOT/'resources/AssetDatabase.replaydb'
text=db.read_text(encoding='utf-8-sig')
for n,name in enumerate(['Title','Arena','Result'],1):
    guid=f'beea44001a1543a2bfe880773240000{n}'
    path=f'resources/Game/GrowRush/GrowRush_{name}.replayscene'
    if f'"{guid}"' not in text:
        text=text.rstrip()+f'\n"{guid}" 5 "GrowRush_{name}" "{path}" ""\n'
lines=text.splitlines()
flow_guid='beea44001a1543a2bfe8807732400004'
if not any(line.startswith('"'+flow_guid+'"') for line in lines):
    lines.append(f'"{flow_guid}" 8 "GrowRush_Flow" "resources/Game/GrowRush/GrowRush_Flow.replaysceneflow" ""')
# The SceneFlow asset is authored in the editor; regeneration preserves its transitions.
lines[0]='REPLAY_ASSET_DB 2 '+str(sum(line.startswith('"') for line in lines[1:]))
db.write_text('\n'.join(lines)+'\n',encoding='utf-8')
