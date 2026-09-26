#!/usr/bin/env python3
"""Animate a small output-stationary systolic array (4x4 demo of our 8x8) using the SAME per-cycle rules as the hardware:
   row i of A is injected i cycles late, column j of B j cycles late; each PE does acc += a*b and forwards a right, b down.
The cycle-accurate simulation is checked against A x B before rendering. This is an animation, not a recording."""
import subprocess, sys, numpy as np
from PIL import Image, ImageDraw, ImageFont
N = 4; A = np.array([[1,2,0,3],[4,1,2,1],[0,3,1,2],[2,1,4,1]]); B = np.array([[2,1,0,1],[1,3,2,0],[0,2,1,4],[3,0,1,2]]); C = A @ B
# --- cycle-accurate model (same rules as SystolicPE / the Chisel engine) ---
a_r = np.zeros((N,N),int); b_r = np.zeros((N,N),int); acc = np.zeros((N,N),int); hist = []
T = 3*N - 2 + 2
for t in range(T):
    hist.append((a_r.copy(), b_r.copy(), acc.copy()))
    prod = a_r * b_r; na = np.zeros_like(a_r); nb = np.zeros_like(b_r)
    for i in range(N):
        k = t - i; na[i,0] = A[i,k] if 0 <= k < N else 0
        for j in range(1,N): na[i,j] = a_r[i,j-1]
    for j in range(N):
        k = t - j; nb[0,j] = B[k,j] if 0 <= k < N else 0
        for i in range(1,N): nb[i,j] = b_r[i-1,j]
    acc = acc + prod; a_r, b_r = na, nb
hist.append((a_r.copy(), b_r.copy(), acc.copy()))
assert (acc == C).all(), (acc, C)          # the animation's result equals the true matrix product
W, H = 1080, 1920; PE = 190; G = 22; X0 = (W - (N*PE + (N-1)*G)) // 2 + 30; Y0 = 640
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"; FB = FONT.replace("Mono.ttf","Mono-Bold.ttf")
fb = lambda s: ImageFont.truetype(FB, s); fr = lambda s: ImageFont.truetype(FONT, s)
BG=(13,17,23); CY=(77,210,255); LI=(182,255,92); AM=(255,176,32); TX=(232,241,248); DIM=(123,139,153); PANEL=(17,26,36)
def pe_xy(i, j): return X0 + j*(PE+G), Y0 + i*(PE+G)
def draw(ph, cyc_f, out):
    """cyc_f = fractional cycle (int part = state index, frac = motion between states)"""
    s = min(int(cyc_f), len(hist)-1); f = cyc_f - s
    a_r, b_r, acc = hist[s]
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    d.text((W//2, 90), "SYSTOLIC ARRAY", font=fb(76), fill=TX, anchor="mm")
    d.text((W//2, 165), "how our matrix engine computes A x B", font=fr(36), fill=DIM, anchor="mm")
    d.text((W//2, 250), f"cycle {min(s, T)}", font=fb(64), fill=AM, anchor="mm")
    d.text((W//2, 320), "every PE: acc += a x b, then passes a right and b down", font=fr(30), fill=DIM, anchor="mm")
    # PEs
    for i in range(N):
        for j in range(N):
            x, y = pe_xy(i, j); active = a_r[i,j] != 0 and b_r[i,j] != 0
            d.rounded_rectangle([x, y, x+PE, y+PE], 22, fill=PANEL, outline=LI if active else (48,54,61), width=6 if active else 3)
            d.text((x+PE//2, y+PE//2-8), str(acc[i,j]), font=fb(66), fill=LI if active else TX, anchor="mm")
            d.text((x+PE//2, y+30), "acc", font=fr(24), fill=DIM, anchor="mm")
    # moving a-values (left -> right) and b-values (top -> bottom): drawn between PE(i,j-1) -> PE(i,j) as they shift
    nxt = hist[min(s+1, len(hist)-1)]
    for i in range(N):
        for j in range(N):
            va, vb = nxt[0][i,j], nxt[1][i,j]
            if va != 0 or (j==0 and nxt[0][i,0]!=0):
                xs = X0 + (j-1)*(PE+G) if j > 0 else X0 - PE*0.9 - G
                x = xs + f*((X0 + j*(PE+G)) - xs) + 44; y = pe_xy(i, 0)[1] + PE - 34
                if va != 0: d.text((x, y), str(va), font=fb(46), fill=CY, anchor="mm")
            if vb != 0:
                ys = Y0 + (i-1)*(PE+G) if i > 0 else Y0 - PE*0.9 - G
                y = ys + f*((Y0 + i*(PE+G)) - ys) + PE - 34; x = pe_xy(0, j)[0] + PE - 44
                d.text((x, y), str(vb), font=fb(46), fill=AM, anchor="mm")
    # matrices
    d.text((X0 - 100, Y0 + (N*(PE+G))//2), "A", font=fb(90), fill=CY, anchor="mm")
    d.text((X0 + (N*(PE+G))//2, Y0 - 150), "B", font=fb(90), fill=AM, anchor="mm")
    d.text((W//2, Y0 + N*(PE+G) + 60), "row i of A enters i cycles late, column j of B j cycles late", font=fr(28), fill=DIM, anchor="mm")
    if s >= T - 1:
        d.text((W//2, 1600), "result = A x B  (checked)", font=fb(52), fill=LI, anchor="mm")
        for i in range(N): d.text((W//2, 1665 + i*0), "", font=fr(10), fill=DIM)
        d.text((W//2, 1690), "  ".join(str(v) for v in C[0]) + "   ...", font=fb(38), fill=TX, anchor="mm")
    d.rectangle([0, H-170, W, H], fill=(33,38,45))
    d.text((40, H-140), "animation (not a recording): 4x4 demo of our 8x8 array,", font=fr(28), fill=(240,136,62))
    d.text((40, H-100), "same per-cycle rules as the hardware; result checked vs A x B", font=fr(28), fill=DIM)
    out.write(im.tobytes())
out = sys.argv[1] if len(sys.argv) > 1 else 'systolic_animation.mp4'; fps = 30; per = 26
ff = subprocess.Popen(['ffmpeg','-y','-loglevel','error','-f','rawvideo','-pix_fmt','rgb24','-s',f'{W}x{H}','-r',str(fps),'-i','-','-c:v','libx264','-pix_fmt','yuv420p','-crf','19',out], stdin=subprocess.PIPE)
for k in range(int((T+1)*per) + 60): draw(None, min(k/per, T), ff.stdin)
ff.stdin.close(); ff.wait(); print('wrote', out, 'frames', int((T+1)*per)+60)
