<img width="1232" height="722" alt="Screenshot 2026-11-21 135747" src="https://github.com/user-attachments/assets/46d40aaf-066f-4a67-be1f-f9fe2d581bea" />
<img width="1229" height="722" alt="Screenshot 2026-11-21 135737" src="https://github.com/user-attachments/assets/5ad453a5-cde9-47c3-88de-ce3134444fdc" />
<img width="1226" height="718" alt="Screenshot 2026-11-21 135724" src="https://github.com/user-attachments/assets/c0a6b727-177d-4151-8cfc-ce2db9ba16fa" />
<img width="1228" height="722" alt="Screenshot 2026-11-21 135708" src="https://github.com/user-attachments/assets/363b336c-f9c2-4cb8-8ae2-af60bb435051" />
<img width="1226" height="719" alt="Screenshot 2026-11-21 135623" src="https://github.com/user-attachments/assets/2341cd0c-39a1-4154-ad8c-6316c355f459" />


# Mandelbrot-Fractal-using-OpenCl-3.0-SFML

A real-time, interactive fractal explorer. The escape-time computation runs on the GPU
with **OpenCL**, and the result is displayed with **SFML**. It renders at 1280x720
and supports deep zooming without pixelation.

## Features

- **GPU-parallel rendering:** one OpenCL work-item per pixel
- **Double precision (`cl_khr_fp64`):** keeps the image sharp at much deeper zoom levels than 32-bit floats, which start to pixelate around 10^4 zoom
- **Smooth coloring:** continuous escape-time formula that removes color banding
- **Animated palette:** the colors flow over time, with adjustable speed
- **Lemniscate mode (Julia-set variant):** switch between the Mandelbrot set and a Julia-style set, and tweak its constant live with the arrow keys
- **Auto-zoom:** a dive into a spiral region, with iteration count increasing as you go deeper
- **Zoom toward the cursor** and drag-to-pan
- **HUD** showing mode, zoom, coordinates, iterations and FPS

## Controls

| Input | Action |
|---|---|
| Scroll | Zoom toward the cursor |
| Left drag | Pan |
| `J` | Toggle Mandelbrot / Lemniscate mode |
| Arrow keys | Adjust the Lemniscate constant |
| `A` | Toggle auto-zoom |
| `R` | Reset view |
| `+` / `-` | Increase / decrease max iterations |
| `P` | Cycle palette animation speed |

## How it works

For each pixel, the kernel maps screen coordinates to a point `c` in the complex plane and
iterates `z = z² + c` until `|z|` escapes or the iteration limit is reached. The escape
count is smoothed with a logarithmic correction and sent back to the CPU, where it is
converted into a color by a time-varying sine-wave palette.

## Requirements

- C++17 compiler
- OpenCL runtime and headers
- SFML 3.1.0
- A GPU that supports `cl_khr_fp64` (double precision)

