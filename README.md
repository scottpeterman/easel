# Easeletch

A balanced layered image editor: Paint.NET approachability with Photoshop's layer model. C++20, Qt 6.

## Status

M3. Paint and erase with a round brush: pen pressure for size and opacity, hardness, flow, spacing and a stabilizer. Opacity caps within a stroke, as in Photoshop, and strokes blend in linear light. Undo and redo keep only the tiles each stroke changed, within a 1 GB budget, and the History panel jumps to any step. The Color panel has a hue ring with a saturation/value square, hex entry, recent colours and a saved palette; the eyedropper shows a before/after ring while you pick.

Layers: raster layers and groups, each with visibility, lock, opacity and one of 12 blend modes (Normal, Multiply, Screen, Overlay, Soft Light, Darken, Lighten, Color Dodge, Color Burn, Difference, Hue, Color). Painting, erasing, smudging, cut, paste, move and Color to Alpha act on the active layer; the eyedropper, Trim and Export use the whole picture; Crop cuts every layer. Every layer change is an undo step. Normal blending and opacity mix in linear light; the other modes compare colours as sRGB values, so they give the results other editors do.

Selections: rectangle, ellipse, lasso (freehand or point by point) and magic wand, with add and subtract, invert, grow, shrink and feather. A feathered selection fades at its edge, and everything done through it (paint, delete, cut, copy, move, Color to Alpha) fades the same way.

Text: the Text tool types with any font installed on the machine, in any size, bold or italic, left, centred or right, smooth or hard-edged for pixel art. The canvas shows it as you type and you can drag it into place. Placed text lands on a new layer of its own as ordinary pixels: it can be moved, faded, masked or erased like anything else, but not retyped.

Layer masks: any layer or group can have a mask that hides part of it without erasing anything. Paint on the mask with the ordinary brush: black hides, white shows, and brush opacity gives the in-between. A mask added while something is selected shows only the selection. Masks can be switched off, applied (erasing what they hide) or removed.

The canvas draws on the GPU through QRhi (Direct3D 11 on Windows, Metal on macOS, OpenGL on Linux) from a sparse tile store (64×64, RGBA16F, linear light), with pan, zoom and rotate. Layers are composited on the CPU, tile by tile, into the store the canvas draws. Opening an image runs in the background. Transforms, adjustments and the rest arrive by milestone.

## Controls

| Action | Input |
| --- | --- |
| New / Open | Ctrl+N / Ctrl+O (Easeletch documents and images) |
| Save / Save As | Ctrl+S / Ctrl+Shift+S |
| Export PNG, JPEG or WebP | Ctrl+Shift+E |
| Paint | Left-drag or pen |
| Brush / Eraser / Smudge / Eyedropper | B / E / S / I |
| New layer / duplicate / group | Ctrl+Shift+N / Ctrl+J / Ctrl+G |
| Merge down (or merge a group) | Ctrl+E |
| Show, lock, rename, reorder a layer | Layers panel: tick the box, tick Lock, double-click the name, drag the row (onto a group to put it inside) |
| Layer blend mode and opacity | Top of the Layers panel |
| Delete layer | Delete key with the layer list clicked, the panel's Delete button, or the Layer menu |
| Move layer up / down, flatten | Layers panel buttons, or the Layer menu |
| Hard 1 px pixels (sprites) | Tick **Pixel** in the tool options |
| Rectangle / ellipse select | M / Shift+M; drag, Shift for square or circle, click to deselect |
| Select all / deselect | Ctrl+A / Ctrl+D |
| Cut / copy / paste / delete | Ctrl+X / Ctrl+C / Ctrl+V / Delete |
| Move selected pixels | V, then drag; arrows nudge 1 px, Shift+arrows 10 px |
| Drop / cancel floating pixels | Enter / Escape |
| Lasso | L; drag around something and let go, or click point by point and press Enter (or click the first point). Escape gives up. Shift adds, Ctrl subtracts |
| Text | T; click where it goes, type in the Text window, drag on the canvas to move it. Ctrl+Enter (or Place) puts it on a new layer; Escape or Cancel drops it |
| Move placed text (or any layer) | With the Text tool, drag on the canvas; or the Move tool (V) |
| Feather selection | Shift+F6 |
| Add a layer mask | Layers panel: Add Mask (from the selection, if there is one) |
| Paint on the mask / on the layer | Ctrl+M, or Paint Mask in the Layers panel |
| Switch a mask off, apply or remove it | Layers panel: the Mask tick box, Apply, Remove |
| Magic wand | W; click selects similar colour, Shift+click adds, Ctrl+click subtracts |
| Invert selection | Ctrl+Shift+I |
| Grow / shrink selection | Edit > Grow Selection, Shrink Selection |
| Crop to selection | Ctrl+Shift+X |
| Trim transparent edges | Ctrl+Alt+T |
| Color to Alpha | Ctrl+Alt+A (in the selection, or the whole canvas) |
| Export selection as PNG | Ctrl+Alt+E |
| Pixel grid (from 600%) | Ctrl+' |
| Sprite grid on / off | Ctrl+Shift+' (cell size, offset and snapping under View > Sprite Grid Settings) |
| Pick a colour from any tool | Hold Alt and click or drag |
| Smaller / larger brush | [ / ] |
| Undo / Redo | Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y |
| Pan | Middle-drag, or hold Space and drag |
| Zoom at cursor | Mouse wheel (whole-pixel steps above 100%: 200%, 300%, 400%...) |
| Rotate view | Shift + wheel, or Ctrl+[ and Ctrl+] |
| Reset rotation | Ctrl+Shift+R |
| Fit to window | Ctrl+0 |
| Actual pixels | Ctrl+1 |

## Files

Easeletch saves `.easeletch` documents: a zip with a `manifest.json` (canvas size and the layer list: name, group, visibility, lock, opacity, blend mode, mask), a flattened `preview.png` (up to 2048 px) you can look at without Easeletch, and each layer's and mask's tiles stored exactly (16-bit float, linear light), so saving and reopening never loses quality. Areas you haven't painted take no space. Saves and exports run in the background, and a failed save never damages the previous file. Export writes a full-size flattened PNG, JPEG or WebP.

The app was called Easel before 0.2.0: `.easel` files from then still open, and saving one asks for a new `.easeletch` name. Documents saved before layers (format 1) open as a single layer. Documents saved now are format 3 (layers and masks), which older builds decline to open.

## Build

Requires CMake 3.21+, Ninja, a C++20 compiler and Qt 6.7+ with the Qt Shader Tools module. CI and releases use Qt 6.10.3.

On Linux and macOS, `scripts/build.sh` configures, builds and runs the tests. On Windows, `scripts\build-windows.bat` does the same (see below). It finds Qt at `~/Qt/6.10.3/gcc_64` (Linux) or `~/Qt/6.10.3/macos` by default, and wipes a build directory that was configured from another source tree, another Qt or another generator.

```
scripts/build.sh
scripts/build.sh --run
scripts/build.sh --qt ~/Qt/6.10.2/gcc_64
scripts/build.sh --clean
scripts/build.sh --appimage
scripts/build.sh --dmg
```

`--appimage` writes `dist/Easeletch-linux-x86_64.AppImage`; `--dmg` writes `dist/Easeletch-macos.dmg`. CI runs the same script. `--debug` builds into `build-debug/` and `--no-tests` skips ctest.

If Qt reports no Shader Tools module, add it:

```
aqt install-qt linux desktop 6.10.3 linux_gcc_64 --noarchives -m qtshadertools -O ~/Qt
```

On Windows, `scripts\build-windows.bat` takes the same options, with `--zip` (writes `dist\Easeletch-windows-x64.zip`) in place of `--appimage`/`--dmg`. It runs from a plain cmd prompt: it loads the x64 MSVC environment itself through vswhere, finds Qt at `C:\Qt\6.10.3\msvc2022_64` (or the newest `C:\Qt\6.*\msvc*_64`), and takes Ninja and CMake from PATH, Visual Studio or `C:\Qt\Tools`. Needs Visual Studio 2022 or its Build Tools with "Desktop development with C++".

```
scripts\build-windows.bat
scripts\build-windows.bat --run
scripts\build-windows.bat --qt C:\Qt\6.10.3\msvc2022_64
scripts\build-windows.bat --zip
```

If your Qt came over from another project without Shader Tools, add it with the Qt Maintenance Tool (Additional Libraries > Qt Shader Tools) or:

```
aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 --noarchives -m qtshadertools -O C:\Qt
```

Manual build, any platform:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.10.3/gcc_64
cmake --build build
ctest --test-dir build --output-on-failure
```

## Releases

Every push to `main` builds and tests on Linux, Windows and macOS and uploads packages as workflow artifacts. Pushing a `v*` tag also publishes a GitHub release:

```
git tag v0.2.0
git push origin v0.2.0
```

| Platform | Package |
| --- | --- |
| Linux x86_64 | `Easeletch-linux-x86_64.AppImage` |
| Windows x64 | `Easeletch-windows-x64.zip` (run `bin/easeletch.exe`) |
| macOS (Apple Silicon + Intel) | `Easeletch-macos.dmg` |

The macOS build is ad-hoc signed, not notarized. After copying it to Applications, clear the quarantine flag once or macOS reports it as damaged:

```
xattr -dr com.apple.quarantine /Applications/Easeletch.app
```

## Layout

```
src/core   tile store and color math, no widgets (easeletch_core)
src/ui     canvas view, main window, panels (easeletch_ui)
src/app    executable, icon, install and deploy rules
scripts    build.sh (Linux, macOS) and build-windows.bat: build, test and package
tests      Qt Test suites; CPU tests run offscreen, GPU tests need a display
```
