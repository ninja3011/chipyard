import matplotlib.animation as animation
import matplotlib.pyplot as plt
import numpy as np

# Set up figure size for Vertical Instagram Reel (9:16 ratio)
fig, ax = plt.subplots(figsize=(6, 10.67), facecolor='#0b0f19')
ax.set_facecolor('#0b0f19')

# Configure axis bounds (normalized coordinate space: 0 to 10 for X, 0 to 18 for Y)
ax.set_xlim(0, 10)
ax.set_ylim(0, 18)
ax.axis('off')

# --- HEADER TITLE (Safe margins) ---
ax.text(
    5,
    16.7,
    'HOW WE TALK TO SILICON',
    color='#00ffcc',
    fontsize=15,
    fontweight='bold',
    ha='center',
    va='center',
)
ax.text(
    5,
    15.8,
    'USB Differential Pair ➔ UART Bridge ➔ Serial TX/RX',
    color='#ffffff',
    fontsize=9.5,
    ha='center',
    va='center',
    alpha=0.7,
)

# --- HARDWARE BLOCKS (Safely spaced out) ---
# 1. Host PC Box (Left)
pc_box = plt.Rectangle(
    (0.4, 10.5),
    2.1,
    3.5,
    facecolor='#1e293b',
    edgecolor='#38bdf8',
    linewidth=2,
    zorder=3,
)
ax.add_patch(pc_box)
ax.text(
    1.45,
    13.3,
    'HOST PC',
    color='#38bdf8',
    fontsize=9,
    fontweight='bold',
    ha='center',
)
ax.text(
    1.45,
    12.0,
    'Terminal\n(USB Host)',
    color='#cbd5e1',
    fontsize=7,
    ha='center',
    va='center',
)

# 2. UART Bridge Chip (Middle)
bridge_box = plt.Rectangle(
    (3.5, 10.2),
    3.0,
    4.1,
    facecolor='#1e293b',
    edgecolor='#fbbf24',
    linewidth=2.5,
    zorder=3,
)
ax.add_patch(bridge_box)
ax.text(
    5.0,
    13.6,
    'UART BRIDGE',
    color='#fbbf24',
    fontsize=9,
    fontweight='bold',
    ha='center',
)
ax.text(
    5.0,
    12.0,
    '• FTDI / CP2102\n• Translates USB\n• Sets Baud Rate',
    color='#cbd5e1',
    fontsize=7.0,
    ha='center',
    va='center',
)

# 3. FPGA Chip (Right)
fpga_box = plt.Rectangle(
    (7.5, 10.5),
    2.1,
    3.5,
    facecolor='#1e293b',
    edgecolor='#ec4899',
    linewidth=2,
    zorder=3,
)
ax.add_patch(fpga_box)
ax.text(
    8.55,
    13.3,
    'FPGA CHIP',
    color='#ec4899',
    fontsize=9,
    fontweight='bold',
    ha='center',
)
ax.text(
    8.55,
    12.0,
    'Rocket Core\n(ALU Math)',
    color='#cbd5e1',
    fontsize=7,
    ha='center',
    va='center',
)

# --- NON-CLASHING WIRING & HIGH LABELS ---
ax.plot([2.5, 3.5], [12.2, 12.2], color='#38bdf8', lw=2.5, zorder=2)
ax.text(
    3.0,
    12.8,
    'D+/D-',
    color='#38bdf8',
    fontsize=8,
    fontweight='bold',
    ha='center',
)

ax.plot([6.5, 7.5], [12.8, 12.8], color='#38bdf8', lw=2, zorder=2)
ax.text(
    7.0,
    13.4,
    'TX Line',
    color='#38bdf8',
    fontsize=7,
    fontweight='bold',
    ha='center',
)

ax.plot([7.5, 6.5], [11.2, 11.2], color='#ec4899', lw=2, zorder=2)
ax.text(
    7.0,
    10.6,
    'RX Line',
    color='#ec4899',
    fontsize=7,
    fontweight='bold',
    ha='center',
)

# --- ANIMATED PACKET PIECES ---
(forward_dot,) = ax.plot(
    [], [], marker='o', color='#38bdf8', markersize=9, zorder=5
)
(return_dot,) = ax.plot(
    [], [], marker='o', color='#ec4899', markersize=9, zorder=5
)

# --- SAFE CENTERED CAPTION BOX ---
caption_text = ax.text(
    5,
    4.5,
    '',
    color='#ffffff',
    fontsize=11,
    fontweight='bold',
    ha='center',
    va='center',
    multialignment='center',
)

# --- TIMELINE SEQUENCE EXTENDED TO 360 FRAMES (12 Seconds at 30 FPS) ---
total_frames = 360


def init():
  forward_dot.set_data([], [])
  return_dot.set_data([], [])
  caption_text.set_text('')
  return forward_dot, return_dot, caption_text


def animate(i):
  if i < 120:
    # Phase 1: Forward path (0 to 4 seconds) — PC -> Bridge -> FPGA
    progress = i / 120.0
    if progress <= 0.5:
      sub_p = progress / 0.5
      x = 1.45 + sub_p * (3.5 - 1.45)
      forward_dot.set_data([x], [12.2])
    else:
      sub_p = (progress - 0.5) / 0.5
      x = 3.5 + sub_p * (7.5 - 3.5)
      forward_dot.set_data([x], [12.8])

    return_dot.set_data([], [])
    caption_text.set_text(
        '1️⃣ SENDING QUERY:\nPC streams "12+7" via USB into UART Bridge & TX.'
    )

  elif i < 240:
    # Phase 2: Processing on Silicon (4 to 8 seconds)
    forward_dot.set_data([], [])
    return_dot.set_data([7.5], [11.2])
    caption_text.set_text(
        '2️⃣ SILICON CALCULATION:\nRISC-V Rocket core crunches the numbers in ALU!'
    )

  else:
    # Phase 3: Return path (8 to 12 seconds) — FPGA -> Bridge -> PC
    progress = (i - 240) / 120.0
    if progress <= 0.5:
      sub_p = progress / 0.5
      x = 7.5 - sub_p * (7.5 - 3.5)
      return_dot.set_data([x], [11.2])
    else:
      sub_p = (progress - 0.5) / 0.5
      x = 3.5 - sub_p * (3.5 - 1.45)
      return_dot.set_data([x], [12.2])

    forward_dot.set_data([], [])
    caption_text.set_text(
        '3️⃣ RETURNING ANSWER:\nResult ("19") travels back over RX & USB to PC! 🚀'
    )

  return forward_dot, return_dot, caption_text


# Create the animation at 30 fps
ani = animation.FuncAnimation(
    fig,
    animate,
    init_func=init,
    frames=total_frames,
    interval=33,
    blit=True,
)

# Save as slow-paced MP4
writer = animation.FFMpegWriter(fps=30, bitrate=2500)
output_filename = 'uart_slow_voiceover.mp4'
ani.save(output_filename, writer=writer)

print(
    f'✨ Slow-paced voiceover animation successfully saved as'
    f' "{output_filename}"!'
)