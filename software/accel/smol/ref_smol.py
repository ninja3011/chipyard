#!/usr/bin/env python3
"""Float32 numpy reference of SmolLM2-135M (HF Llama conventions: rotate_half RoPE, GQA, tied embeddings).
Ground truth for the INT8 converter/C port. Usage: ref_smol.py "prompt" [n_new]"""
import sys, numpy as np, json
from safetensors import safe_open
HF='hf'
cfg=json.load(open(f'{HF}/config.json'))
D=cfg['hidden_size']; H=cfg['num_attention_heads']; KV=cfg['num_key_value_heads']; L=cfg['num_hidden_layers']
HS=D//H; FF=cfg['intermediate_size']; V=cfg['vocab_size']; EPS=cfg['rms_norm_eps']; THETA=cfg['rope_theta']
def bf16(a):  # bfloat16 (as uint16) -> float32
    return (a.astype(np.uint32)<<16).view(np.float32)
def load():
    """Parse safetensors by hand (numpy has no bfloat16): 8-byte header length, JSON header, raw data."""
    import struct, mmap
    fh=open(f'{HF}/model.safetensors','rb'); n=struct.unpack('<Q',fh.read(8))[0]; hdr=json.loads(fh.read(n)); base=8+n
    buf=np.memmap(f'{HF}/model.safetensors',dtype=np.uint8,mode='r'); W={}
    for k,v in hdr.items():
        if k=='__metadata__': continue
        a,b=v['data_offsets']; raw=np.frombuffer(buf[base+a:base+b],dtype=np.uint16 if v['dtype']=='BF16' else np.float32)
        W[k]=(bf16(raw) if v['dtype']=='BF16' else raw).reshape(v['shape']).astype(np.float32)
    return W
def rope_tables(n):
    inv=1.0/(THETA**(np.arange(0,HS,2,dtype=np.float64)/HS))
    ang=np.outer(np.arange(n),inv); return np.cos(ang).astype(np.float32),np.sin(ang).astype(np.float32)
def rms(x,w): return w*(x/np.sqrt((x*x).mean(-1,keepdims=True)+EPS))
def rot_half(x,c,s):   # x[..., HS]; HF: pairs (i, i+HS/2)
    a,b=x[...,:HS//2],x[...,HS//2:]; return np.concatenate([a*c-b*s,b*c+a*s],-1)
class Ref:
    def __init__(s,W,maxpos=512):
        s.W=W; s.cos,s.sin=rope_tables(maxpos); s.k=np.zeros((L,maxpos,KV,HS),np.float32); s.v=s.k.copy(); s.emb=W['model.embed_tokens.weight']
    def step(s,tok,pos,mv=None):
        W=s.W; mv=mv or (lambda w,x: w@x); x=s.emb[tok].copy(); c,sn=s.cos[pos],s.sin[pos]
        for l in range(L):
            p=f'model.layers.{l}.'
            xb=rms(x,W[p+'input_layernorm.weight'])
            q=mv(W[p+'self_attn.q_proj.weight'],xb).reshape(H,HS); k=mv(W[p+'self_attn.k_proj.weight'],xb).reshape(KV,HS); v=mv(W[p+'self_attn.v_proj.weight'],xb).reshape(KV,HS)
            q=rot_half(q,c,sn); k=rot_half(k,c,sn); s.k[l,pos]=k; s.v[l,pos]=v
            out=np.zeros((H,HS),np.float32)
            for h in range(H):
                g=h//(H//KV); K=s.k[l,:pos+1,g]; Vv=s.v[l,:pos+1,g]
                sc=K@q[h]/np.sqrt(HS); sc=np.exp(sc-sc.max()); sc/=sc.sum(); out[h]=sc@Vv
            x=x+mv(W[p+'self_attn.o_proj.weight'],out.reshape(-1))
            xb=rms(x,W[p+'post_attention_layernorm.weight'])
            g_=mv(W[p+'mlp.gate_proj.weight'],xb); u=mv(W[p+'mlp.up_proj.weight'],xb)
            x=x+mv(W[p+'mlp.down_proj.weight'],(g_/(1+np.exp(-g_)))*u)
        x=rms(x,W['model.norm.weight']); return mv(s.emb,x)
if __name__=='__main__':
    from tokenizers import Tokenizer
    tk=Tokenizer.from_file(f'{HF}/tokenizer.json'); W=load(); r=Ref(W)
    user=sys.argv[1] if len(sys.argv)>1 else 'What is the capital of France?'; n=int(sys.argv[2]) if len(sys.argv)>2 else 30
    prompt=f"<|im_start|>system\nYou are a helpful AI assistant named SmolLM, trained by Hugging Face<|im_end|>\n<|im_start|>user\n{user}<|im_end|>\n<|im_start|>assistant\n"
    ids=tk.encode(prompt).ids; print('prompt tokens:',len(ids)); pos=0
    for t in ids: lg=r.step(t,pos); pos+=1
    out=[]
    for _ in range(n):
        t=int(lg.argmax()); 
        if t==2: break
        out.append(t); lg=r.step(t,pos); pos+=1
    print('reply ids:',out); print('reply:',tk.decode(out))
