// =============================================================================
//  GlowOrb Desktop - HLSL port of glow-orb/script.js (WebGL FRAG shader)
//  Entry points:
//    VSFullscreen / PSOrb         -> the glowing orb (fullscreen triangle)
//    VSParticles / PSParticles    -> additive stardust (instanced quads)
//
//  The cbuffer mirrors OrbConstants in src/ShaderTypes.h (512 bytes).
//  Output is PREMULTIPLIED alpha (DXGI_ALPHA_MODE_PREMULTIPLIED).
// =============================================================================

#define TAU 6.283185307179586f
static const float RAD  = 0.86f;
static const float SOFT = 0.005f;

struct Particle {
    float2 pos;
    float2 extent;
    float4 color;
};

cbuffer OrbCB : register(b0) {
    float2 uResolution;
    float  uSize;
    float  uLevel;

    float4 uParams0;   // warp, ridge, sharp, zoom
    float4 uParams1;   // exposure, glow, rim, hear
    float4 uParams2;   // fade, bass, mid, treble
    float4 uParams3;   // beat, haloAngle, haloNoise, particleGlow
    float4 uParams4;   // phase, voice, opaque, time
    float4 uParams5;   // orbScale, spare, spare, spare

    float4 uColA, uColB, uColC, uColD;
    float4 uHi, uCool, uWarm, uGlowCol;
    float4 uHalo[5];
    float4 uPad[12];
};

Texture2D<float4>          gWaveTex   : register(t0);  // 256x1 R8_UNORM, 128 = silence
StructuredBuffer<Particle> gParticles : register(t1);
SamplerState               gSampler   : register(s0);

struct Uniforms {
    float level, warp, ridge, sharp, zoom;
    float exposure, glow, rim, hear, fade;
    float bass, mid, treble, beat;
    float phase, voice, opaque, time;
    float haloAngle, haloNoise, particleGlow;
    float3 colA, colB, colC, colD, hi, cool, warm, glowCol;
};

Uniforms MakeUniforms() {
    Uniforms U;
    U.level      = uLevel;
    U.warp       = uParams0.x + uParams2.y * 0.25f;   // bass pushes warp
    U.ridge      = uParams0.y + uParams2.z * 0.20f;   // mid pushes ridge
    U.sharp      = uParams0.z;
    U.zoom       = uParams0.w;
    U.exposure   = uParams1.x;
    U.glow       = uParams1.y * (1.0f + uParams2.w * 0.30f);
    U.rim        = uParams1.z;
    U.hear       = uParams1.w;
    U.fade       = uParams2.x;
    U.bass       = uParams2.y;
    U.mid        = uParams2.z;
    U.treble     = uParams2.w;
    U.beat       = uParams3.x;
    U.haloAngle  = uParams3.y;
    U.haloNoise  = uParams3.z;
    U.particleGlow = uParams3.w;
    U.phase      = uParams4.x;
    U.voice      = uParams4.y;
    U.opaque     = uParams4.z;
    U.time       = uParams4.w;
    U.colA = uColA.rgb;  U.colB = uColB.rgb;
    U.colC = uColC.rgb;  U.colD = uColD.rgb;
    U.hi = uHi.rgb;      U.cool = uCool.rgb;
    U.warm = uWarm.rgb;  U.glowCol = uGlowCol.rgb;
    return U;
}

// ---------------------------------------------------------------- noise -----
float hash21(float2 p) {
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float noise2(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(hash21(i + float2(0, 0)), hash21(i + float2(1, 0)), f.x),
                lerp(hash21(i + float2(0, 1)), hash21(i + float2(1, 1)), f.x), f.y);
}

float fbm(float2 p) {
    float s = 0.0f;
    float a = 0.5f;
    for (int i = 0; i < 4; ++i) {
        s += a * noise2(p);
        // GLSL mat2(0.8,0.6,-0.6,0.8) is column-major -> row-vector mul here.
        p = mul(p, float2x2(0.8f, 0.6f, -0.6f, 0.8f)) * 2.03f;
        a *= 0.5f;
    }
    return s / 0.9375f;
}

// Smooth minimum and maximum to prevent C0 derivative pinch creases
float smin(float a, float b, float k) {
    float h = saturate(0.5f + 0.5f * (b - a) / k);
    return lerp(b, a, h) - k * h * (1.0f - h);
}
float smax(float a, float b, float k) {
    float h = saturate(0.5f + 0.5f * (a - b) / k);
    return lerp(b, a, h) + k * h * (1.0f - h);
}

// ------------------------------------------------------------ silk sheet ----
float2 band(float2 q, float drift, float offset, float amp, float mainY,
            float env, float soft, Uniforms U) {
    float u = saturate(q.x * 0.5f + 0.5f);
    // SampleLevel (not Sample) because sheet() is called inside dynamic
    // branches - avoids undefined screen-space derivatives.
    float realWave = (gWaveTex.SampleLevel(gSampler, float2(u, 0.5f), 0.0f).r - 0.5f) * 2.0f;
    float y = env * (amp * sin(q.x + drift + offset) + realWave * U.level * 0.28f);
    float d = abs(q.y - y);
    float lineI = 0.018f / (sqrt(d * d + soft * soft) + 0.026f);
    float maxY = smax(mainY, y, 0.035f);
    float minY = smin(mainY, y, 0.035f);
    float bd = max(0.0f, max(q.y - maxY, minY - q.y));
    float fillB = 0.014f / (bd * bd * 5.0f + 0.06f);
    return float2(lineI, fillB);
}

float3 sheet(float2 p, float t, Uniforms U) {
    float scale = 0.74f + U.zoom * 0.34f;
    float2 q = p / scale;
    float2 w = float2(fbm(q * 1.1f + float2(0.0f, t * 0.09f)),
                      fbm(q * 1.1f + float2(7.7f, -t * 0.07f))) - 0.5f;
    q += U.warp * 0.075f * w;

    float envB = cos(1.57079633f * min(abs(0.9f * q.x), 1.0f));
    float env = envB * envB;
    float low  = 0.5f + 0.5f * cos(t * 0.37f) + U.level * 0.6f;
    float mid  = 0.5f + 0.5f * sin(t * 0.51f + 1.2f) + U.level * 0.8f;
    float high = 0.5f + 0.5f * cos(t * 0.73f + 2.1f) + U.level * 0.5f;
    float drift = t * 2.4f;
    float mainAmp = 0.1f + U.ridge * 0.17f + low * 0.018f;
    float bandAmp = mainAmp + mid * 0.025f + high * 0.018f;
    float mainY = mainAmp * env * sin(q.x * 1.1f + drift);
    float sep = 1.85f + U.warp * 0.12f + mid * 0.28f;
    float soft = 0.03f / max(U.sharp, 0.2f) + mid * 0.006f;

    float2 b0 = band(q, drift, -sep,        bandAmp, mainY, env, soft, U);
    float2 b1 = band(q, drift, -sep * 0.34f, bandAmp, mainY, env, soft, U);
    float2 b2 = band(q, drift,  sep * 0.34f, bandAmp, mainY, env, soft, U);
    float2 b3 = band(q, drift,  sep,        bandAmp, mainY, env, soft, U);
    float w0 = b0.x + b0.y, w1 = b1.x + b1.y;
    float w2 = b2.x + b2.y, w3 = b3.x + b3.y;
    float total = w0 + w1 + w2 + w3;
    float d0 = w0 * w0, d1 = w1 * w1, d2 = w2 * w2, d3 = w3 * w3;
    float3 spectral = (U.colA * d0 + U.colC * d1 + U.colB * d2 + U.colD * d3) /
                      max(d0 + d1 + d2 + d3, 0.0001f);
    float energy = (1.0f - exp(-max(total - 0.12f, 0.0f) * 0.75f)) * env;
    float md = abs(q.y - mainY);
    float core = exp(-md * md / (0.0028f / max(U.sharp, 0.2f))) * env;

    float haze = fbm(q * 1.6f + float2(t * 0.05f, -t * 0.03f));
    float3 atmos = lerp(U.colD, U.colB, smoothstep(-0.7f, 0.7f, q.y)) *
                   (0.012f + 0.022f * haze);
    atmos += lerp(U.colA, U.colC, haze) * 0.035f * exp(-q.y * q.y * 7.0f) * haze;

    float3 col = atmos + spectral * energy * 1.14f;
    col += U.hi * core * (0.2f + 0.1f * low);
    col = col / (1.0f + col * 0.18f);
    col = lerp(col, U.hi, 0.06f * smoothstep(0.15f, 1.15f, dot(p, float2(-0.32f, 0.78f))));
    col *= 1.0f - 0.3f * smoothstep(-0.1f, 1.2f, dot(p, float2(0.45f, -0.62f)));
    return col;
}

float profile(float t) {
    float d = saturate(t);
    return 1.0f - sqrt(max(1.0f - (1.0f - d) * (1.0f - d), 0.0f));
}
float lobe(float2 n, float2 dir, float cut, float power) {
    return pow(saturate((dot(n, dir) - cut) / max(1.0f - cut, 0.001f)), power);
}
float3 over(float3 dst, float3 src, float a) {
    float k = saturate(a);
    return src * k + dst * (1.0f - k);
}

// Rotating conic-gradient halo (replaces the two CSS conic-gradient divs).
float3 conicHalo(float ang, float rot) {
    float x = frac(ang / TAU + rot) * 5.0f;
    float3 c = 0.0f;
    for (int i = 0; i < 5; ++i) {
        float d = abs(x - (float)i);
        d = min(d, 5.0f - d);
        float w = max(0.0f, 1.0f - d);
        c += uHalo[i].rgb * w;
    }
    return c;
}

// High-frequency screen-space dither (Jorge Jimenez Interleaved Gradient Noise)
// Eliminates 8-bit quantization banding without grid artifacts
float ScreenDither(float2 screenPos) {
    return (frac(52.9829189f * frac(dot(screenPos, float2(0.06711056f, 0.00583715f)))) - 0.5f) / 255.0f;
}

// ------------------------------------------------------------- orb pass -----
struct VSOut { float4 pos : SV_Position; };

VSOut VSFullscreen(uint vid : SV_VertexID) {
    float2 verts[3] = { float2(-1.0f, -1.0f), float2(3.0f, -1.0f), float2(-1.0f, 3.0f) };
    VSOut o;
    o.pos = float4(verts[vid], 0.0f, 1.0f);
    return o;
}

float4 PSOrb(VSOut input) : SV_Target {
    Uniforms U = MakeUniforms();

    // SV_Position origin is top-left in D3D; gl_FragCoord was bottom-left.
    float2 px = float2(input.pos.x, uResolution.y - input.pos.y) - 0.5f * uResolution;
    float2 uv = px * 2.0f / max(uSize, 1.0f);
    uv /= max(uParams5.x, 0.001f);        // size pulse

    float r = length(uv);
    float ang = atan2(uv.y, uv.x);
    float t = U.phase;

    float contour = U.hear * U.level *
                    (0.015f * sin(ang * 3.0f + t * 1.9f) +
                     0.009f * sin(ang * 5.0f - t * 1.3f + 1.7f));
    float rad = RAD * (1.0f + contour);

    float3 dynamicHaloCol = lerp(
        lerp(U.colA, U.colB, 0.5f + 0.5f * sin(ang + t * 0.9f)),
        lerp(U.colC, U.colD, 0.5f + 0.5f * cos(ang * 2.0f - t * 0.7f)), 0.5f);
    float3 glowCol = lerp(U.glowCol, dynamicHaloCol, 0.75f) * (0.75f + 0.55f * U.level);
    float glowAmt = U.glow * (1.0f + 1.25f * U.level);

    // Smooth fade to complete zero before touching square window boundary (window side edge is r >= 1.72)
    float haloFade = 1.0f - smoothstep(1.05f, 1.62f, r);

    if (r > rad * (1.01f + SOFT)) {
        float3 halo = glowCol * glowAmt * exp(-(r - rad) * 8.5f) * haloFade;

        // Matches web version: outer ambient aura (CSS blur 44px) + inner accent ring (CSS blur 18px)
        float outerOpacity = (0.32f + 0.68f * U.level) * U.glow;
        float innerOpacity = (0.45f + 0.55f * U.level) * U.glow;
        float auraFalloff = exp(-(r - rad) * 2.8f) * haloFade;
        float accentFalloff = exp(-(r - rad) * 5.2f) * haloFade;

        halo += conicHalo(ang, U.haloAngle) * auraFalloff * outerOpacity * 0.45f;
        halo += conicHalo(ang, -U.haloAngle * 1.4f) * accentFalloff * innerOpacity * 0.55f;

        // Interleaved gradient dither to eradicate 8-bit color banding
        halo += ScreenDither(input.pos.xy);
        halo = saturate(halo);

        if (U.opaque > 0.5f) {
            return float4(saturate(halo * U.fade + float3(0.014f, 0.014f, 0.024f)), 1.0f);
        }
        // rgb <= alpha, so this is valid premultiplied color.
        return float4(halo, saturate(max(halo.r, max(halo.g, halo.b)))) * U.fade;
    }

    float2 p = uv / rad;
    float pd = length(p);
    float2 n = pd > 0.0001f ? p / pd : float2(0.0f, 0.0f);
    float edge = max(1.0f - pd, 0.0f);
    // Smooth Hermite profile: continuous C1 transition, no infinite slope or derivative ring
    float profRaw = profile(edge / 0.32f);
    float prof = profRaw * profRaw * (3.0f - 2.0f * profRaw);
    float2 rp = p - n * prof * 0.5f;

    // Smooth chromatic aberration without sudden threshold branching
    float split = 0.10f * prof;
    float3 fcol = float3(sheet(rp - n * split, t, U).r,
                         sheet(rp, t, U).g,
                         sheet(rp + n * split, t, U).b);

    float lum = dot(fcol, float3(0.213f, 0.715f, 0.072f));
    float3 col = saturate(float3(lum, lum, lum) + (fcol - float3(lum, lum, lum)) * 1.18f);

    float rimReact = U.rim * (1.0f + 0.45f * U.hear * U.level);
    float surfW = 0.035f + 0.03f * U.hear * U.level;
    float surf = 1.0f - smoothstep(0.0f, surfW, edge);
    float optical = pow(surf, 1.8f);
    col = over(col, lerp(U.colA, U.hi, 0.5f) * 0.35f, optical * 0.1f * rimReact);

    float coolS = lobe(n, normalize(float2(0.84f, 0.54f)), 0.05f, 1.6f);
    float warmS = lobe(n, normalize(float2(-0.62f, -0.78f)), 0.05f, 1.8f);
    float disp = optical * 0.5f * rimReact;
    col = over(col, U.cool, disp * coolS);
    col = over(col, U.warm, disp * warmS);

    col *= 1.0f - optical * 0.12f * (0.15f + 0.85f * max(dot(n, float2(0.45f, -0.89f)), 0.0f));
    float key = optical * lobe(n, normalize(float2(-0.68f, 0.73f)), 0.2f, 2.8f) * 0.6f * rimReact;
    float fillL = optical * lobe(n, normalize(float2(0.74f, -0.67f)), 0.4f, 3.6f) * 0.4f * rimReact;
    col = over(col, lerp(U.hi, float3(1, 1, 1), 0.35f), key);
    col = over(col, lerp(U.colC, U.hi, 0.55f), fillL);

    col *= 1.0f - 0.4f * smoothstep(0.95f, 1.0f, pd);
    col += lerp(U.colA, U.colB, 0.3f + 0.3f * sin(ang + t * 0.6f)) *
           optical * 0.3f * U.hear * U.level;

    float2 sp = p - float2(-0.38f, 0.46f);
    float spec = exp(-dot(sp, sp) * 22.0f);
    col += U.hi * spec * 0.07f * U.rim;

    float ballA = 1.0f - smoothstep(0.99f - SOFT, 1.01f + SOFT, pd);
    col = saturate(col * max(U.exposure, 0.0f)) * ballA;
    float outside = smoothstep(rad - SOFT, rad + SOFT, r);
    col += glowCol * glowAmt * exp(-max(r - rad, 0.0f) * 9.5f) * haloFade * outside;

    // Interleaved gradient dither to eradicate banding on dark orb body gradients
    col += ScreenDither(input.pos.xy);
    col = saturate(col);
    float a = saturate(max(ballA, max(col.r, max(col.g, col.b))));

    if (U.opaque > 0.5f) {
        float3 bg = float3(0.014f, 0.014f, 0.024f);
        return float4(saturate(col * U.fade + bg * (1.0f - a)), 1.0f);
    }
    return float4(col, a) * U.fade;
}

// -------------------------------------------------------- particle pass -----
struct PVSOut {
    float4 pos   : SV_Position;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

PVSOut VSParticles(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    float2 corners[6] = {
        float2(-1, -1), float2(1, -1), float2(-1, 1),
        float2(-1,  1), float2(1, -1), float2( 1, 1)
    };
    Particle p = gParticles[iid];
    float2 c = corners[vid];
    PVSOut o;
    o.pos = float4(p.pos + c * p.extent, 0.0f, 1.0f);
    o.uv = c;
    o.color = p.color;
    return o;
}

float4 PSParticles(PVSOut input) : SV_Target {
    float d = length(input.uv);
    if (d > 1.0f) discard;
    float core = exp(-d * d * 18.0f);
    float glow = exp(-d * d * 4.5f) * 0.55f;
    float a = saturate((core + glow) * input.color.a *
                       (1.0f - smoothstep(0.80f, 1.0f, d)));
    return float4(input.color.rgb * a, a);
}
