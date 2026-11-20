#pragma once

namespace omniframe {

namespace shaders {

// Shader para Linhas, Wireframes e Grid
inline constexpr const char* LINE_VERTEX_SHADER = R"glsl(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;

uniform mat4 uMVP;
uniform vec4 uClipPlane;

out vec4 vColor;

void main() {
    vColor = aColor;
    vec4 worldPos = vec4(aPos, 1.0);
    gl_ClipDistance[0] = dot(worldPos, uClipPlane);
    gl_Position = uMVP * worldPos;
}
)glsl";

inline constexpr const char* LINE_FRAGMENT_SHADER = R"glsl(#version 330 core
in vec4 vColor;
out vec4 FragColor;

void main() {
    FragColor = vColor;
}
)glsl";

// Shader para Malhas Sólidas 3D com Iluminação Blinn-Phong (Apoios, Cones de Cargas, Esferas)
inline constexpr const char* MESH_VERTEX_SHADER = R"glsl(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec4 aColor;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;
uniform vec4 uClipPlane;

out vec3 vNormal;
out vec3 vFragPos;
out vec4 vColor;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vFragPos = vec3(worldPos);
    vNormal = normalize(uNormalMatrix * aNormal);
    vColor = aColor;
    gl_ClipDistance[0] = dot(worldPos, uClipPlane);
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)glsl";

inline constexpr const char* MESH_FRAGMENT_SHADER = R"glsl(#version 330 core
in vec3 vNormal;
in vec3 vFragPos;
in vec4 vColor;

uniform vec3 uLightDir;
uniform vec3 uViewPos;

out vec4 FragColor;

void main() {
    // Iluminação Ambiente
    float ambientStrength = 0.35;
    vec3 ambient = ambientStrength * vec3(1.0);

    // Iluminação Difusa
    vec3 norm = normalize(vNormal);
    vec3 lightDir = normalize(uLightDir);
    // Two-sided illumination for thin 2D shell/plate meshes and arbitrary winding
    float diff = abs(dot(norm, lightDir));
    vec3 diffuse = diff * vec3(0.85);

    // Iluminação Especular (Blinn-Phong)
    float specularStrength = 0.3;
    vec3 viewDir = normalize(uViewPos - vFragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), 32.0);
    vec3 specular = specularStrength * spec * vec3(1.0);

    vec3 result = (ambient + diffuse + specular) * vColor.rgb;
    FragColor = vec4(result, vColor.a);
}
)glsl";

// Shader para Preenchimento Translúcido dos Diagramas (GL_TRIANGLES com Alpha Blending)
inline constexpr const char* DIAGRAM_FILL_VERTEX_SHADER = R"glsl(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;

uniform mat4 uMVP;
uniform vec4 uClipPlane;

out vec4 vColor;

void main() {
    vColor = aColor;
    vec4 worldPos = vec4(aPos, 1.0);
    gl_ClipDistance[0] = dot(worldPos, uClipPlane);
    gl_Position = uMVP * worldPos;
}
)glsl";

inline constexpr const char* DIAGRAM_FILL_FRAGMENT_SHADER = R"glsl(#version 330 core
in vec4 vColor;
out vec4 FragColor;

void main() {
    FragColor = vColor;
}
)glsl";

// Shader Analítico de Linhas Anti-Aliasing Subpixel (Screen-Space Quad Expansion + SDF)
inline constexpr const char* AA_LINE_VERTEX_SHADER = R"glsl(#version 330 core
layout (location = 0) in vec3 aPosA;
layout (location = 1) in vec3 aPosB;
layout (location = 2) in vec4 aColorA;
layout (location = 3) in vec4 aColorB;
layout (location = 4) in vec2 aCorner; // x in {0.0, 1.0}, y in {-1.0, +1.0}

uniform mat4 uMVP;
uniform vec2 uResolution;
uniform float uStrokePx;
uniform float uFeatherPx;
uniform vec4 uClipPlane;

flat out vec2 vPxA;
flat out vec2 vPxB;
flat out vec4 vColorA;
flat out vec4 vColorB;
flat out float vEffStrokePx;
flat out float vAlphaAtten;

void main() {
    vec4 worldA = vec4(aPosA, 1.0);
    vec4 worldB = vec4(aPosB, 1.0);
    vec4 clipA = uMVP * worldA;
    vec4 clipB = uMVP * worldB;

    const float W_NEAR = 0.001;
    if (clipA.w < W_NEAR && clipB.w < W_NEAR) {
        gl_ClipDistance[0] = 1.0;
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        vPxA = vec2(0.0);
        vPxB = vec2(0.0);
        vColorA = vec4(0.0);
        vColorB = vec4(0.0);
        vEffStrokePx = 1.0;
        vAlphaAtten = 0.0;
        return;
    }

    vec4 colA = aColorA;
    vec4 colB = aColorB;
    if (clipA.w < W_NEAR) {
        float t = clamp((W_NEAR - clipA.w) / max(clipB.w - clipA.w, 1e-6), 0.0, 1.0);
        clipA = mix(clipA, clipB, t);
        worldA = mix(worldA, worldB, t);
        colA = mix(colA, colB, t);
    } else if (clipB.w < W_NEAR) {
        float t = clamp((W_NEAR - clipB.w) / max(clipA.w - clipB.w, 1e-6), 0.0, 1.0);
        clipB = mix(clipB, clipA, t);
        worldB = mix(worldB, worldA, t);
        colB = mix(colB, colA, t);
    }

    vec2 pxA = (clipA.xy / clipA.w * 0.5 + 0.5) * uResolution;
    vec2 pxB = (clipB.xy / clipB.w * 0.5 + 0.5) * uResolution;

    vec2 delta = pxB - pxA;
    float segLen = length(delta);
    vec2 dir = (segLen > 1e-4) ? (delta / segLen) : vec2(1.0, 0.0);
    vec2 perp = vec2(-dir.y, dir.x);

    float effStroke = max(uStrokePx, 1.0);
    float alphaAtten = clamp(uStrokePx / effStroke, 0.25, 1.0);
    float pad = 0.5 * effStroke + max(uFeatherPx, 0.75) + 0.75;
    float alongSign = (aCorner.x < 0.5) ? -1.0 : 1.0;

    vec4 baseClip = mix(clipA, clipB, aCorner.x);
    vec2 offsetPx = dir * (alongSign * pad) + perp * (aCorner.y * pad);

    vec4 baseWorld = mix(worldA, worldB, aCorner.x);
    gl_ClipDistance[0] = dot(baseWorld, uClipPlane);
    gl_Position = baseClip;
    gl_Position.xy += (offsetPx * 2.0 / max(uResolution, vec2(1.0))) * baseClip.w;

    vPxA = pxA;
    vPxB = pxB;
    vColorA = colA;
    vColorB = colB;
    vEffStrokePx = effStroke;
    vAlphaAtten = alphaAtten;
}
)glsl";

inline constexpr const char* AA_LINE_FRAGMENT_SHADER = R"glsl(#version 330 core
flat in vec2 vPxA;
flat in vec2 vPxB;
flat in vec4 vColorA;
flat in vec4 vColorB;
flat in float vEffStrokePx;
flat in float vAlphaAtten;

uniform float uFeatherPx;
uniform int uPassMode; // 0 = completo, 1 = nucleo com depth write, 2 = halo AA sem depth write

out vec4 FragColor;

void main() {
    if (vAlphaAtten <= 0.0) {
        discard;
    }

    vec2 pa = gl_FragCoord.xy - vPxA;
    vec2 ba = vPxB - vPxA;
    float baDot = dot(ba, ba);
    float h = (baDot > 1e-5) ? clamp(dot(pa, ba) / baDot, 0.0, 1.0) : 0.0;
    float dist = length(pa - ba * h);

    float radius = 0.5 * vEffStrokePx;
    float halfFeather = 0.5 * max(uFeatherPx, 0.65);
    float sd = dist - radius;
    float coverage = 1.0 - smoothstep(-halfFeather, halfFeather, sd);

    vec4 col = mix(vColorA, vColorB, h);
    float baseAlpha = col.a * vAlphaAtten;

    if (uPassMode == 1) {
        if (coverage < 0.88) {
            discard;
        }
        FragColor = vec4(col.rgb, baseAlpha);
    } else if (uPassMode == 2) {
        if (coverage >= 0.88 || coverage <= 0.01) {
            discard;
        }
        FragColor = vec4(col.rgb, baseAlpha * coverage);
    } else {
        if (coverage <= 0.01) {
            discard;
        }
        FragColor = vec4(col.rgb, baseAlpha * coverage);
    }
}
)glsl";

// Shader Fullscreen para Resolve SSAA de Alta Definição (Catmull-Rom 16-tap / Bicúbico + CAS)
inline constexpr const char* RESOLVE_VERTEX_SHADER = R"glsl(#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;

out vec2 vUV;

void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)glsl";

inline constexpr const char* RESOLVE_FRAGMENT_SHADER = R"glsl(#version 330 core
in vec2 vUV;

uniform sampler2D uSceneTex;
uniform vec2 uTexSize;
uniform int uFilterMode; // 0 = Catmull-Rom + Sharpen, 1 = Bicubico suave, 2 = Bilinear
uniform float uSharpen;

out vec4 FragColor;

float catmullRomWeight(float x) {
    float ax = abs(x);
    if (ax < 1.0) {
        return 1.5 * ax * ax * ax - 2.5 * ax * ax + 1.0;
    } else if (ax < 2.0) {
        return -0.5 * ax * ax * ax + 2.5 * ax * ax - 4.0 * ax + 2.0;
    }
    return 0.0;
}

float bsplineWeight(float x) {
    float ax = abs(x);
    if (ax < 1.0) {
        return (2.0 / 3.0) - ax * ax + 0.5 * ax * ax * ax;
    } else if (ax < 2.0) {
        float t = 2.0 - ax;
        return (1.0 / 6.0) * t * t * t;
    }
    return 0.0;
}

vec3 sampleCubic4x4(vec2 uv, vec2 texSize, bool sharpKernel) {
    vec2 pos = uv * texSize - 0.5;
    vec2 base = floor(pos);
    vec2 f = pos - base;
    vec2 invSize = 1.0 / max(texSize, vec2(1.0));
    vec2 halfTexel = 0.5 * invSize;

    vec3 accum = vec3(0.0);
    float totalW = 0.0;

    for (int j = -1; j <= 2; ++j) {
        float wy = sharpKernel ? catmullRomWeight(float(j) - f.y) : bsplineWeight(float(j) - f.y);
        for (int i = -1; i <= 2; ++i) {
            float wx = sharpKernel ? catmullRomWeight(float(i) - f.x) : bsplineWeight(float(i) - f.x);
            float w = wx * wy;
            vec2 sampleUV = clamp((base + vec2(float(i), float(j)) + 0.5) * invSize, halfTexel, 1.0 - halfTexel);
            accum += texture(uSceneTex, sampleUV).rgb * w;
            totalW += w;
        }
    }

    vec3 res = (abs(totalW) > 1e-5) ? (accum / totalW) : texture(uSceneTex, uv).rgb;
    return clamp(res, 0.0, 1.0);
}

void main() {
    vec3 col;
    if (uFilterMode == 2) {
        col = texture(uSceneTex, vUV).rgb;
    } else {
        bool sharpKernel = (uFilterMode == 0);
        col = sampleCubic4x4(vUV, uTexSize, sharpKernel);
    }

    if (uSharpen > 0.001 && uFilterMode != 2) {
        vec2 stepUV = 1.0 / max(uTexSize, vec2(1.0));
        vec3 n = texture(uSceneTex, vUV + vec2(0.0,  stepUV.y)).rgb;
        vec3 s = texture(uSceneTex, vUV + vec2(0.0, -stepUV.y)).rgb;
        vec3 e = texture(uSceneTex, vUV + vec2( stepUV.x, 0.0)).rgb;
        vec3 w = texture(uSceneTex, vUV + vec2(-stepUV.x, 0.0)).rgb;

        vec3 mn = min(min(n, s), min(e, w));
        vec3 mx = max(max(n, s), max(e, w));
        vec3 avg = 0.25 * (n + s + e + w);
        vec3 diff = col - avg;
        col = clamp(col + diff * uSharpen, max(mn - 0.03, vec3(0.0)), min(mx + 0.03, vec3(1.0)));
    }

    FragColor = vec4(col, 1.0);
}
)glsl";

} // namespace shaders

} // namespace omniframe

