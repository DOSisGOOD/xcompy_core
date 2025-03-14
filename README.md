---
title: XCompy core
description: A libretro core for viewing X11 Window contents with shaders applied.
author: gamedevjeff
created:  2025 Feb 23
---

XCompy core
===========

A quick hack to apply shaders to X11 Window contents on Linux, as inspired by [ShaderGlass](https://github.com/mausimus/ShaderGlass) which is Windows-only.

Tested with RetroArch v1.7.3 and v1.20.0 on Ubuntu 22.04.5. This core has not been tested on other X11 systems.

Installation / Usage
------------
1. run `make`
2. Copy the xcompy_core.so to where you keep your cores.  On Ubuntu, it's: /usr/lib/x86_64-linux-gnu/libretro/
3. Load the core in RetroArch.
4. Open a window to target.
5. Select your desired renderer and shader from the RetroArch menu (F1)
6. Mouse-wheel scroll through the active windows.

Screenshots
-----------

Commander Keen 6 - (ID Software 1991 - DOS)

[![ss-keen6-megabezel-1.png](ss-keen6-megabezel-1.png) View full image](ss-keen6-megabezel-1.png)

[![ss-keen6-crtlottes.png](ss-keen6-crtlottes.png) View full image](ss-keen6-crtlottes.png)


Warrior Blade: Rastan Saga Episode III (Taito 1991 - Arcade)

[![ss-crtaperature.png](ss-crtaperature.png) View full image](ss-crtaperature.png)

[![ss-crtconsumer.png](ss-crtconsumer.png) View full image](ss-crtconsumer.png)

[![ss-1tap-bloom.png](ss-1tap-bloom.png) View full image](ss-1tap-bloom.png)

[![crt-cyclon.png](crt-cyclon.png) View full image](crt-cyclon.png)

[![crt-hylian-sinc-composite.png](crt-hylian-sinc-composite.png) View full image](crt-hylian-sinc-composite.png)

[![crt-lottes-multipass-glow.png](crt-lottes-multipass-glow.png) View full image](crt-lottes-multipass-glow.png)
