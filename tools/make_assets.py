#!/usr/bin/env python3
"""Regenerate the GlowOrb tray icon and the README preview image.

Pure Python + Pillow + NumPy. Run from anywhere:
    python tools/make_assets.py
The executable loads assets/gloworb.ico at runtime; preview.png is docs only.
"""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")
os.makedirs(ASSETS, exist_ok=True)

STOPS = np.array([
    [0x42, 0x85, 0xF4],  # Google blue
    [0x34, 0xA8, 0x53],  # green
    [0xFB, 0xBC, 0x05],  # yellow
    [0xEA, 0x43, 0x35],  # red
    [0x42, 0x85, 0xF4],
], dtype=np.float32) / 255.0


def orb_rgba(size: int) -> Image.Image:
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    half = size * 0.5
    u = (xx - half) / half
    v = (half - yy) / half
    r = np.sqrt(u * u + v * v)
    ang = np.arctan2(v, u)

    # Rotating conic halo color
    t = ((ang / (2 * np.pi)) + 0.5) % 1.0
    f = t * 4.0
    i = np.clip(np.floor(f).astype(np.int32), 0, 3)
    frac = (f - i)[..., None]
    conic = STOPS[i] * (1.0 - frac) + STOPS[i + 1] * frac

    R = 0.70
    pd = np.clip(r / R, 0.0, 1.5)
    edge = np.clip(1.0 - pd, 0.0, 1.0)
    sphere_mask = np.clip((R - r) * size * 0.5, 0.0, 1.0)

    # Silk-sheet-like interference ribbons inside the ball
    env = np.clip(np.cos(np.clip(np.abs(u) * 0.9, 0.0, 1.0) * np.pi * 0.5), 0.0, 1.0) ** 2
    band = np.zeros_like(r)
    for phase in [-1.85, -0.62, 0.62, 1.85]:
        y = (0.055 + 0.022 * np.cos(u * 2.0 + phase)) * np.sin(u * 2.4 + phase) * env
        d = np.abs(v - y)
        band += 0.010 / (d * d * 6.0 + 0.010)
    band = np.clip(band / 2.2, 0.0, 1.0)
    energy = (1.0 - np.exp(-np.maximum(band - 0.08, 0.0) * 1.8)) * env

    shade = np.clip(1.0 - 0.45 * pd * pd + 0.20 * (1.0 - pd), 0.0, 1.0)[..., None]
    body = (np.array([0.035, 0.045, 0.085], dtype=np.float32) +
            conic * energy[..., None] * 1.20) * shade
    rim = np.exp(-((r - R) / 0.045) ** 2)[..., None]
    body += conic * rim * 0.55
    spec = np.exp(-(((u + 0.34) / 0.28) ** 2 + ((v - 0.42) / 0.30) ** 2))[..., None]
    body += np.array([0.9, 0.95, 1.0], dtype=np.float32) * spec * 0.30

    halo = conic * np.exp(-np.maximum(r - R, 0.0) * 7.5)[..., None]
    halo *= np.clip(1.0 - (r - 0.72) / 0.30, 0.0, 1.0)[..., None]

    rgb = np.clip(body * sphere_mask[..., None] + halo * 0.85, 0.0, 1.0)
    alpha_body = np.clip(sphere_mask * 255.0, 0, 255)
    alpha_halo = np.clip(np.max(halo, axis=-1) * 255.0 * 0.9, 0, 255)
    alpha = np.clip(np.maximum(alpha_body, alpha_halo), 0, 255)

    rgba = np.concatenate([rgb * 255.0, alpha[..., None]], axis=-1).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA")


def make_icon():
    base = orb_rgba(512)
    path = os.path.join(ASSETS, "gloworb.ico")
    base.save(path, format="ICO",
              sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64),
                     (128, 128), (256, 256)])
    print("wrote", path)


def font(size, bold=False):
    candidates = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
    ]
    for c in candidates:
        if os.path.exists(c):
            return ImageFont.truetype(c, size)
    return ImageFont.load_default()


def make_preview():
    W, H = 1280, 760
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    bg = np.zeros((H, W, 3), dtype=np.float32)
    bg[..., 0] = 0.035 + 0.035 * (yy / H)
    bg[..., 1] = 0.040 + 0.030 * (xx / W)
    bg[..., 2] = 0.065 + 0.055 * (yy / H)
    img = Image.fromarray((bg * 255).astype(np.uint8), "RGB").convert("RGBA")
    d = ImageDraw.Draw(img, "RGBA")

    # Fake desktop windows for context
    d.rounded_rectangle((70, 80, 560, 520), radius=12,
                        fill=(28, 32, 48, 235), outline=(90, 100, 135, 120), width=2)
    d.rounded_rectangle((740, 110, 1210, 430), radius=12,
                        fill=(24, 28, 42, 235), outline=(90, 100, 135, 120), width=2)
    for i in range(7):
        d.rounded_rectangle((100, 140 + i * 48, 300 + (i * 37) % 200, 160 + i * 48),
                            radius=6, fill=(120, 140, 200, 40 + i * 6))
    for i in range(5):
        d.rounded_rectangle((775, 160 + i * 46, 980 + (i * 29) % 180, 178 + i * 46),
                            radius=6, fill=(110, 190, 210, 40 + i * 8))

    orb = orb_rgba(560)
    img.alpha_composite(orb, ((W - 560) // 2, 100))

    d = ImageDraw.Draw(img, "RGBA")
    d.text((70, 690), "GlowOrb Desktop  -  DirectX 12 + WASAPI loopback",
           font=font(30, True), fill=(232, 238, 255, 255))
    d.text((72, 728), "Reacts to any system audio: Spotify, YouTube, VLC, FL Studio, games",
           font=font(18), fill=(150, 165, 200, 255))

    path = os.path.join(ASSETS, "preview.png")
    img.convert("RGB").save(path)
    print("wrote", path)


if __name__ == "__main__":
    make_icon()
    make_preview()
