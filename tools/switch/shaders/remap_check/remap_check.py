import os,re,sys,struct,random,glob
S=sys.argv[1]; mode=sys.argv[2]
keys=dict(l.split() for l in open(f'{S}/muestra_keys.txt'))  # nN -> spirv key
def cache(path):
    d=open(path,'rb').read(); n=struct.unpack_from('<I',d,16)[0]; off=32+n*24; out={}
    for k in range(n):
        h,e,v,b=struct.unpack_from('<QIII',d,32+k*24); out[(h,v)]=d[off:off+b]; off=(off+b+3)&~3
    return out
if mode=='make':
    random.seed(1); lines=[]; plan={}
    for f in glob.glob(f'{S}/g1/*.frag'):
        n=int(re.match(r'ps(\d+)_',os.path.basename(f)).group(1))
        g=open(f).read(); locs=[int(x) for x in re.findall(r'layout\(location = (\d+)\) (?:\w+ )*in ',g)]
        if not locs: continue
        tgt=random.sample(range(22),len(locs)); origen=list(range(32))
        for l,t in zip(locs,tgt): origen[l]=t
        if origen==list(range(32)): continue
        key=keys[f'n{n}']; lines.append(f'{key} 00000000 00000080 '+''.join('%02X'%x for x in origen)); plan[key]=origen
    open(f'{S}/var2.txt','w').write('\n'.join(lines)+'\n'); open(f'{S}/plan.txt','w').write(repr(plan)); print('remaps',len(lines))
else:
    plan=eval(open(f'{S}/plan.txt').read()); c1=cache(f'{S}/c1.bin'); c2=cache(f'{S}/c2.bin')
    def patch(dk,origen):
        dk=bytearray(dk); code=struct.unpack_from('<I',dk,8)[0]  # code starts at control_sz
        sph=code
        imap=bytes(dk[sph+0x48:sph+0x48+32]); new=bytearray(32)
        for l in range(32):
            if imap[l]: new[origen[l]]=imap[l]
        dk[sph+0x48:sph+0x48+32]=new
        for o in range(sph+0x50,len(dk)-7,8):
            pass  # scheduling words never carry the IPA opcode
            w=struct.unpack_from('<Q',dk,o)[0]
            if (w>>53)==0x702:  # IPA
                a=(w>>28)&0x3FF
                if 0x80<=a<0x80+16*32:
                    slot=(a-0x80)//16; a2=0x80+16*origen[slot]+(a&0xF)
                    w=(w&~(0x3FF<<28))|(a2<<28); struct.pack_into('<Q',dk,o,w)
        return bytes(dk)
    ok=bad=0; ex=[]
    for key,origen in plan.items():
        h=int(key,16); a=c1.get((h,0)); b=c2.get((h,0x80))
        if a is None or b is None: continue
        if patch(a,origen)==b: ok+=1
        else:
            bad+=1
            if len(ex)<3: ex.append((key,[i for i in range(min(len(a),len(b))) if patch(a,origen)[i]!=b[i]][:12],len(a),len(b)))
    print('identical',ok,'different',bad); [print(e) for e in ex]
