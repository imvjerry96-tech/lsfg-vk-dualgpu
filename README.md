# lsfg-vk-dualgpu

A modified fork of [PancakeTAS/lsfg-vk](https://github.com/PancakeTAS/lsfg-vk) that adds **dual-GPU frame generation** running natively on **KDE Wayland** — no TTY3 required, no input loss.

## Goal

- **Render GPU:** NVIDIA RTX (game runs normally, anti-tamper bypassed)
- **Frame-gen GPU:** AMD RX 6600 (runs LSFGVK interpolation + presents to DP-5)
- **Environment:** KDE Wayland, nested gamescope via Heroic
- **Input:** works normally (no standalone gamescope)

## What was changed

### 1. Layer null-safe passthrough (`lsfg-vk-layer/src/entrypoint.cpp`)
- `vkCreateInstance` / `vkCreateDevice` / `vkCreateSwapchainKHR` / `vkQueuePresentKHR` have null-guards
- When no profile is loaded → clean passthrough (no gamescope crash, no `-3` error)
- Device is registered correctly into the loader chain (fixes "Bad destination in trampoline dispatch" + `vkSignalSemaphoreKHR -3`)

### 2. Anti-tamper bypass
- The layer loads into the **gamescope** process (outer), NOT into the game (inner)
- The game (Cyberpunk 2077 RUNE) does not see the injected module → no exit code 3
- Game runs at RTX 100%, no segfault

### 3. Cross-GPU DRM syncobj (`lsfg-vk-common`)
- New `drm_syncobj.cpp/hpp`: shares a syncobj between two GPUs via DMA-BUF
- `VKBPVK_DRM_NODE` = renderD128 (the RTX app GPU) so the LSFGVK backend (AMD) syncs correctly

### 4. Nested gamescope config (Heroic)
- `windowType: windowed` + `enableForceGrabCursor: true` → input stays alive (Wayland does not allow nested keyboard grab)
- `--prefer-vk-device 1002:73ff` → gamescope selects the RX 6600 as its Vulkan device
- `VK_INSTANCE_LAYERS=VK_LAYER_LSFGVK_frame_generation` (NO device_select, NO VK_DEVICE_SELECT)

### 5. Pacing Wait (`lsfg-vk-layer/src/swapchain.cpp` + config)
- `Pacing::Wait` → `VK_PRESENT_MODE_FIFO_KHR` to avoid tearing
- `conf.toml`: `pacing = "wait"`

## Build

```bash
git clone https://github.com/imvjerry96-tech/lsfg-vk-dualgpu
cd lsfg-vk-dualgpu
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target vkBasalt -j$(nproc)
sudo cp lsfg-vk-layer/libvkBasalt.so /usr/lib/liblsfg-vk-layer.so
```

The layer manifest is generated at `lsfg-vk-layer/VkLayer_LSFGVK_frame_generation.json`
during configure. Install it to the Vulkan implicit layer directory:

```bash
sudo cp lsfg-vk-layer/VkLayer_LSFGVK_frame_generation.json \
     /usr/share/vulkan/implicit_layer.d/
```

## Find your GPUs

List all GPUs Vulkan sees:

```bash
vulkaninfo --summary 2>/dev/null | grep -A1 deviceName
# or
ls /dev/dri/renderD*   # renderD128 = card1, renderD129 = card2, etc.
```

Note which `renderDXXX` belongs to your **frame-gen GPU** (the AMD card) and your
**render GPU** (the NVIDIA card). You need both below.

## Usage (Heroic + Cyberpunk 2077)

1. Add to the game's `GamesConfig` JSON:
```json
{ "key": "ENABLE_LSFGVK", "value": "1" },
{ "key": "VK_INSTANCE_LAYERS", "value": "VK_LAYER_LSFGVK_frame_generation" },
{ "key": "VKBPVK_GPU", "value": "YOUR SECONDARY GPU ID" },
{ "key": "VKBPVK_DRM_NODE", "value": "/dev/dri/renderD128" },
{ "key": "DXVK_FRAME_RATE", "value": "I RECOMMEND SET HALF OF YOUR MONITOR FRAME RATE" }
```
   - `VKBPVK_GPU` = the exact `deviceName` of your frame-gen GPU from `vulkaninfo`
   - `VKBPVK_DRM_NODE` = the `/dev/dri/renderDXXX` of your **render** GPU (NVIDIA)

2. `conf.toml` (`~/.config/lsfg-vk/conf.toml`):
```toml
active_in = [ "gamescope" ]
pacing = "wait"
```

3. Gamescope block (Heroic): `windowType: windowed`, `enableForceGrabCursor: true`

4. The frame-gen backend needs the VScale / Lossless Scaling shader DLL.
   Either set `dll = '/path/to/Lossless.dll'` in `conf.toml [global]`, or let it
   auto-detect from `~/.local/share/Steam/steamapps/common/Lossless Scaling/Lossless.dll`.

5. Launch the game → check the frame-gen GPU is working:
   `cat /sys/class/drm/card2/device/gpu_busy_percent`

## Tip: avoid tearing with MFG + LSFG

When DLSS MFG and LSFGVK are both enabled, frame time can spike during scene loads → tearing.
- **Workaround:** Ramp settings from low → high before loading the scene (shader cache warms up)
- Or add `DXVK_ASYNC=1` + `DXVK_STATE_CACHE_PATH` for async shader compilation (no frame blocking)

## Notes

- Tested only on: RTX 5070 Ti + RX 6600, CachyOS, KDE Wayland, GE-Proton
- Gamescope 3.16.23: `--prefer-vk-device` is required (NOT `--render-device`)
- Nested gamescope cannot truly force FIFO (KDE compositor owns vsync) — use the ramp trick

## Donate

If this project helped you, consider supporting development:

**USDT (ERC-20 / BEP-20):**
```
0x832572B6DA54AA8C78ccC73C76F3807B6D60f739
```
