import re,sys,struct,collections
def fnv(s):
    h=0x811c9dc5
    for c in s.encode(): h=((h^c)*0x01000193)&0xffffffff
    return h
words=['light','color','colour','intensity','radius','range','size','scale','enabled','enable','flicker','speed','strength','brightness','falloff','shadow','shadows','cast','fire','flame','smoke','sparks','spark','glow','emissive','tint','alpha','opacity','height','width','length','lifetime','rate','amount','density','wind','main','base','secondary','inner','outer','core','hue','temperature','power','factor','multiplier','offset','position','direction','velocity','gravity','turbulence','noise','time','duration','delay','fade','in','out','start','end','min','max','count','spawn','emission','emit','is','has','use','show','hide','on','off','active','lit','burning','distance','attenuation','point','spot','angle','cone','fog','volumetric','ember','embers','heat','distortion','soft','hard','blend','a','b','c','1','2','3','x','y','z','r','g','hdr','exposure','sound','audio','volume','loop','looping','state','mode','type','variant','level','progress','fill','charge','energy','mana','health','damage','hit','impact','trail','beam','ring','wave','pulse','frequency','amplitude','phase','random','seed','mesh','model','texture','mask','gradient','ramp','top','bottom','front','back','left','right','up','down','world','local','target','source','owner','parent','child','bone','joint','socket','attach','follow','player','weapon','torch','lantern','candle','lamp','wisp','magic','spell','element','ice','shock','lightning','poison','shroud','water','wet','rain','snow','dust','dirt','leaves','grass','wood','stone','metal','blood','flames','small','big','large','medium','low','high','near','far','indoor','outdoor','day','night']
names={}
for w in words:
    names[fnv(w)]=w
for a in words:
    for b in words:
        for sep in ('_',''):
            n=a+sep+b; names[fnv(n)]=n
            n2=a+b.capitalize(); names[fnv(n2)]=n2
big=['light_intensity','light_color','light_radius']
for a in ('light','point_light','pointlight','glow','fire','flame'):
    for b in words:
        for c in words[:60]:
            n=f'{a}_{b}_{c}'; names[fnv(n)]=n
res={}
inst=[]
cur=None
for line in open(sys.argv[1],encoding='utf-8',errors='replace'):
    m=re.match(r'\s*res (\d+) guid (\w+) cull ([\d.]+)-([\d.]+) emitters (\d+) particles\[(.*?)\] lightFilters (\d+) sounds (\d+) models (\d+) meshes (\d+) images (\d+) decals (\d+) params\[(.*?)\] defaults (\w+)',line)
    if m:
        idx=int(m.group(1))
        res[idx]=dict(guid=m.group(2),cull=(m.group(3),m.group(4)),emit=int(m.group(5)),parts=[p.split(':') for p in m.group(6).split()],lf=int(m.group(7)),snd=int(m.group(8)),models=int(m.group(9)),meshes=int(m.group(10)),images=int(m.group(11)),decals=int(m.group(12)),params=[re.match(r'(\w+):(\w+)@(\d+)\+(\d+)',p).groups() for p in m.group(13).split()],defaults=bytes.fromhex(m.group(14)))
        if line.startswith('  ') and cur is not None: cur['res']=idx
        continue
    m=re.match(r'inst slot (\d+) handle (\w+) dist ([\d.]+) \(dx ([-\d.]+) dy ([-\d.]+) dz ([-\d.]+)\) scale ([\d.]+) .* record (\w+)',line)
    if m:
        cur=dict(slot=int(m.group(1)),dist=float(m.group(3)),d=(m.group(4),m.group(5),m.group(6)),rec=bytes.fromhex(m.group(8))); inst.append(cur)
def pname(h): return names.get(int(h,16),h)
def show(idx,data=None):
    r=res[idx]; data=data or r['defaults']
    out=[]
    for h,t,o,s in r['params']:
        o=int(o); s=int(s); raw=data[o:o+s]
        if t=='90b5085d' and s==4: v='%.4g'%struct.unpack('<f',raw)[0]
        elif t=='53039620': v='#'+raw.hex()
        elif s==4: v=str(struct.unpack('<I',raw)[0]) if struct.unpack('<I',raw)[0]<1000 else raw.hex()
        else: v=raw.hex()
        out.append(f'{pname(h)}({t[:4]})={v}')
    return f"res {idx} {r['guid']} cull {r['cull'][0]} emit {r['emit']} parts {[p[1] for p in r['parts']]} lf{r['lf']} snd{r['snd']} mod{r['models']} mesh{r['meshes']} img{r['images']} dec{r['decals']} | "+' '.join(out)
if __name__=='__main__':
    mode=sys.argv[2]
    if mode=='inst':
        for i in sorted(inst,key=lambda x:x['dist']):
            if i['dist']<=float(sys.argv[3]): print('%.2f'%i['dist'],i['d'],show(i['res'],i['rec'][0x74:0xb4]))
    if mode=='lightonly':
        c=0
        for idx,r in sorted(res.items()):
            if r['meshes']==0 and r['images']==0 and r['models']==0 and r['decals']==0:
                c+=1; print(show(idx))
        print(c)
    if mode=='light':
        for idx,r in sorted(res.items()):
            if any(h=='0fd9ca11' for h,_,_,_ in r['params']): print(show(idx))
    if mode=='stats':
        c=collections.Counter(); t=collections.Counter()
        for r in res.values():
            for h,ty,o,s in r['params']: c[pname(h)]+=1; t[ty]+=1
        print(len(res),t); print(c.most_common(80))
