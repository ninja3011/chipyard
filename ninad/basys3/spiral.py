import os

import matplotlib.animation as animation
import matplotlib.pyplot as plt
import numpy as np

# 1. Set up the figure in polar coordinates
fig, ax = plt.subplots(subplot_kw={'projection': 'polar'})
fig.patch.set_facecolor('#0b0f19')  # Dark background for tech-tuber aesthetic
ax.set_facecolor('#0b0f19')

# Archimedean spiral parameters: r = a + b * theta
a = 0
b = 1

# Generate angle data
theta = np.linspace(0, 10 * np.pi, 500)

# Style the plot lines and hide grid/ticks
(line,) = ax.plot([], [], color='#00ffcc', lw=2.5)
ax.axis('off')

# Set limits so the window stays fixed while the spiral grows
ax.set_rmax(max(a + b * theta))


# Initialization function for the animation
def init():
  line.set_data([], [])
  return (line,)


# Update function that draws more of the spiral frame by frame
def animate(i):
  current_theta = theta[:i]
  current_r = a + b * current_theta
  line.set_data(current_theta, current_r)
  return (line,)


# Create the animation (50 frames, smooth interval)
ani = animation.FuncAnimation(
    fig, animate, init_func=init, frames=len(theta), interval=20, blit=True
)

# Save the animation as an MP4 file (requires ffmpeg installed)
writer = animation.FFMpegWriter(fps=30, bitrate=1800)
ani.save(os.path.join(os.path.dirname(__file__), 'archimedean_spiral.mp4'), writer=writer)

print('Animation saved successfully as archimedean_spiral.mp4! 🚀')