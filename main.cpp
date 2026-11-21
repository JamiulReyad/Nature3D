#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdarg>
// Compatibility for GLFW precompiled with MinGW-w64 on standard MinGW runtime
extern "C" int __ms_vsnprintf(char *s, size_t n, const char *format, va_list arg) {
    return vsnprintf(s, n, format, arg);
}
#endif

#ifndef GLEW_STATIC
#define GLEW_STATIC
#endif
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const int WIDTH = 1280;
const int HEIGHT = 720;

// ─── Global State ───────────────────────────────────────────────────────────
float camAngle = 0.85f;
float camPitch = 0.28f;
float camDist = 48.0f;
glm::vec3 camTarget(0.0f, 0.0f, 0.0f);

bool isMousePressed = false;
bool isRightMousePressed = false;
double lastMouseX = 0.0;
double lastMouseY = 0.0;

float timeOfDay = 0.35f;       // ~08:30 AM start
float timeSpeed = 60.0f;       // seconds of game time per real second
bool timePaused = false;
float manualFog = 0.0f;
float manualWind = 0.35f;

// Feature toggles
bool showPlane = true;
glm::vec3 planePos(0.0f, 25.0f, -25.0f);
float planeYaw = 1.57f;

bool showAurora = true;
bool showBirds = true;
bool showFireflies = true;

enum WeatherType { CLEAR = 0, CLOUDY = 1, RAIN = 2, STORM = 3, FOG = 4, SNOW = 5 };
WeatherType currentWeather = CLEAR;

const char* weatherNames[] = { "Clear Sky", "Cloudy", "Rain", "Thunderstorm", "Dense Fog", "Snowfall" };

struct WeatherParams {
    float clouds;
    float fog;
    float rain;
    float snow;
};

WeatherParams weatherTarget = {0.05f, 0.05f, 0.0f, 0.0f};
WeatherParams weatherCurrent = weatherTarget;

// Lightning state
float lightningIntensity = 0.0f;
float lightningTimer = 4.0f;

void updateWindowTitle(GLFWwindow* window, int fps);

void setWeather(WeatherType type, GLFWwindow* window = nullptr) {
    currentWeather = type;
    switch (type) {
        case CLEAR:
            weatherTarget = {0.05f, 0.05f, 0.0f, 0.0f};
            std::cout << "\n>>> [WEATHER CHANGED] -> 1: CLEAR SKY (Sunny & Warm)" << std::endl;
            break;
        case CLOUDY:
            weatherTarget = {0.88f, 0.20f, 0.0f, 0.0f};
            std::cout << "\n>>> [WEATHER CHANGED] -> 2: CLOUDY (Thick Overcast Clouds)" << std::endl;
            break;
        case RAIN:
            weatherTarget = {0.96f, 0.40f, 1.0f, 0.0f};
            std::cout << "\n>>> [WEATHER CHANGED] -> 3: RAIN (Falling Raindrops & Wet Ground)" << std::endl;
            break;
        case STORM:
            weatherTarget = {1.00f, 0.55f, 1.0f, 0.0f};
            lightningTimer = 0.4f;
            std::cout << "\n>>> [WEATHER CHANGED] -> 4: THUNDERSTORM (Dark Sky, High Wind, Rain & Lightning)" << std::endl;
            break;
        case FOG:
            weatherTarget = {0.60f, 1.00f, 0.0f, 0.0f};
            std::cout << "\n>>> [WEATHER CHANGED] -> 5: DENSE FOG (Atmospheric Mist Shrouding Mountains)" << std::endl;
            break;
        case SNOW:
            weatherTarget = {0.85f, 0.30f, 0.0f, 1.0f};
            std::cout << "\n>>> [WEATHER CHANGED] -> 6: SNOWFALL (Swirling Snowflakes & Snow-Capped Ground)" << std::endl;
            break;
    }
    if (window) {
        updateWindowTitle(window, 60);
    }
}

// ─── Shader Compilation Helpers ─────────────────────────────────────────────
GLuint compileShader(const char* source, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetShaderInfoLog(shader, sizeof(infoLog), NULL, infoLog);
        std::cerr << "Shader compilation error: " << infoLog << std::endl;
    }
    return shader;
}

GLuint createProgram(const char* vertexSrc, const char* fragmentSrc) {
    GLuint vs = compileShader(vertexSrc, GL_VERTEX_SHADER);
    GLuint fs = compileShader(fragmentSrc, GL_FRAGMENT_SHADER);

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetProgramInfoLog(program, sizeof(infoLog), NULL, infoLog);
        std::cerr << "Program linking error: " << infoLog << std::endl;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

// ─── Terrain Height Function (Synchronized on CPU & GPU) ───────────────────
float terrainHeight(float x, float z) {
    float h = (sin(x * 0.15f) + cos(z * 0.15f)) * 0.7f + sin(x * 0.04f) * cos(z * 0.04f) * 2.2f;
    // Lake oval basin centered at (20, 0), radii (13, 28)
    float dx = (x - 20.0f) / 13.0f;
    float dz = z / 28.0f;
    float r2 = dx * dx + dz * dz;
    if (r2 < 1.0f) {
        float lakeDep = (1.0f - r2) * (1.0f - r2) * 3.6f;
        h -= lakeDep;
    }
    return h;
}

// ─── Shader Sources ────────────────────────────────────────────────────────

// 1. Terrain Shaders
const char* terrainVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNorm;
layout (location = 2) in vec2 aUV;

uniform mat4 uMVP;
uniform mat4 uModel;

out vec3 vWorldPos;
out vec3 vNorm;
out vec2 vUV;

void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    vNorm = mat3(uModel) * aNorm;
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* terrainFS = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vNorm;
in vec2 vUV;

uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmbient;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform vec3 uCamPos;
uniform float uSnow;
uniform float uRain;
uniform float uLightning;

out vec4 FragColor;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    vec3 N = normalize(vNorm);
    float h = vWorldPos.y;

    vec3 sand  = vec3(0.55, 0.48, 0.35);
    vec3 dirt  = vec3(0.38, 0.28, 0.18);
    vec3 grass = vec3(0.24, 0.46, 0.14);
    vec3 rock  = vec3(0.42, 0.40, 0.37);
    vec3 snow  = vec3(0.95, 0.96, 0.99);

    float nv = noise(vUV * 12.0);

    // Height based biome blending
    vec3 col = mix(sand, grass, smoothstep(-0.8, -0.1, h));
    col = mix(col, dirt, smoothstep(0.4, 1.8, h));
    col = mix(col, rock, smoothstep(2.0, 4.2, h) + (1.0 - max(N.y, 0.0)) * 0.4);
    col = mix(col, snow, smoothstep(4.8, 7.5, h));
    col *= 0.85 + 0.3 * nv;

    // Wetness darkens ground during rain
    col *= 1.0 - uRain * 0.32;

    // Snow dynamically blankets the entire landscape
    float snowThreshold = smoothstep(-0.4, 0.6, N.y + h * 0.1);
    col = mix(col, snow, clamp(uSnow * 1.15 * snowThreshold, 0.0, 0.98));

    // Sun Lighting
    vec3 L = normalize(uSunDir);
    float diff = max(dot(N, L), 0.0);
    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 24.0) * (0.06 + uRain * 0.45);

    // Moonlight
    vec3 moonDir = normalize(-uSunDir);
    float moonDiff = max(dot(N, moonDir), 0.0) * max(-uSunDir.y, 0.0) * 0.25;
    vec3 moonCol = vec3(0.45, 0.55, 0.75);

    vec3 lit = (uAmbient + vec3(uLightning * 0.9)) * col +
               diff * uSunColor * col +
               spec * uSunColor +
               moonDiff * moonCol * col;

    // Atmospheric Fog
    float dist = length(vWorldPos - uCamPos);
    float fog = 1.0 - exp(-uFogDensity * dist * 0.016);
    fog = clamp(fog, 0.0, 1.0);

    FragColor = vec4(mix(lit, uFogColor, fog), 1.0);
}
)";

// 2. Water Shaders
const char* waterVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform float uTime;

out vec3 vWorldPos;
out vec3 vNorm;
out vec2 vUV;

void main() {
    vec3 pos = aPos;

    float w1 = sin(pos.x * 0.9 + uTime * 2.2) * cos(pos.z * 0.7 + uTime * 1.7) * 0.07;
    float w2 = sin(pos.x * 1.8 - uTime * 3.1 + pos.z * 1.3) * 0.035;
    float w3 = cos(pos.x * 0.4 + pos.z * 0.5 + uTime * 1.0) * 0.05;
    pos.y += w1 + w2 + w3;

    float dw_dx = 0.9 * cos(pos.x * 0.9 + uTime * 2.2) * cos(pos.z * 0.7 + uTime * 1.7) * 0.07 +
                  1.8 * cos(pos.x * 1.8 - uTime * 3.1 + pos.z * 1.3) * 0.035;
    float dw_dz = -0.7 * sin(pos.x * 0.9 + uTime * 2.2) * sin(pos.z * 0.7 + uTime * 1.7) * 0.07 +
                  1.3 * cos(pos.x * 1.8 - uTime * 3.1 + pos.z * 1.3) * 0.035;

    vec3 normal = normalize(vec3(-dw_dx, 1.0, -dw_dz));

    vec4 wp = uModel * vec4(pos, 1.0);
    vWorldPos = wp.xyz;
    vNorm = normal;
    vUV = aUV;
    gl_Position = uMVP * vec4(pos, 1.0);
}
)";

const char* waterFS = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vNorm;
in vec2 vUV;

uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmbient;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform vec3 uCamPos;
uniform float uLightning;
uniform float uTime;
uniform float uSnow;

out vec4 FragColor;

void main() {
    vec3 N = normalize(vNorm);
    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 L = normalize(uSunDir);

    float NdotV = max(dot(N, V), 0.0);
    float fresnel = 0.15 + 0.85 * pow(1.0 - NdotV, 4.0);

    vec3 deepWater    = vec3(0.02, 0.20, 0.42);
    vec3 shallowWater = vec3(0.08, 0.52, 0.65);
    vec3 frozenIce    = vec3(0.70, 0.82, 0.92);

    vec3 baseCol = mix(deepWater, shallowWater, clamp(vNorm.x * 0.5 + 0.5, 0.0, 1.0));
    baseCol = mix(baseCol, frozenIce, uSnow * 0.65);

    vec3 H = normalize(L + V);
    float specSun = pow(max(dot(N, H), 0.0), 64.0) * max(uSunDir.y, 0.0);

    vec3 moonDir = normalize(-uSunDir);
    vec3 Hmoon = normalize(moonDir + V);
    float specMoon = pow(max(dot(N, Hmoon), 0.0), 48.0) * max(-uSunDir.y, 0.0);
    vec3 moonColor = vec3(0.65, 0.75, 0.95);

    vec3 sunSpecHighlight = specSun * uSunColor * 1.8;
    vec3 moonSpecHighlight = specMoon * moonColor * 0.65;

    vec3 lit = (uAmbient + vec3(uLightning * 0.9)) * baseCol +
               baseCol * max(dot(N, L), 0.0) * uSunColor * 0.4 +
               sunSpecHighlight +
               moonSpecHighlight;

    float dist = length(vWorldPos - uCamPos);
    float fog = 1.0 - exp(-uFogDensity * dist * 0.016);
    fog = clamp(fog, 0.0, 1.0);

    float alpha = clamp(0.80 + fresnel * 0.18, 0.75, 0.96);
    FragColor = vec4(mix(lit, uFogColor, fog), alpha);
}
)";

// 3. Tree Shaders
const char* treeVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNorm;
layout (location = 2) in float aTreeID;

uniform mat4 uVP;
uniform float uTime;
uniform float uWind;

out vec3 vNorm;
out vec3 vWorldPos;
out float vHeight;
out float vTreeID;

void main() {
    float phase = aTreeID * 6.283 + uTime * 2.5;
    float swayAmp = uWind * 0.06 * max(aPos.y - 0.5, 0.0);
    vec3 p = aPos;
    p.x += sin(phase) * swayAmp;
    p.z += cos(phase * 0.7) * swayAmp * 0.6;

    vNorm = aNorm;
    vWorldPos = p;
    vHeight = aPos.y;
    vTreeID = aTreeID;
    gl_Position = uVP * vec4(p, 1.0);
}
)";

const char* treeFS = R"(
#version 330 core
in vec3 vNorm;
in vec3 vWorldPos;
in float vHeight;
in float vTreeID;

uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmbient;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform vec3 uCamPos;
uniform float uSnow;
uniform float uLightning;

out vec4 FragColor;

float hash(float n) {
    return fract(sin(n * 13.23) * 43758.5453);
}

void main() {
    vec3 N = normalize(vNorm);
    float t = hash(vTreeID);

    vec3 pineGreen = vec3(0.08 + t * 0.06, 0.26 + t * 0.12, 0.07 + t * 0.05);
    vec3 bark = vec3(0.24, 0.16, 0.10);

    float isBark = step(vHeight, 1.3);
    vec3 col = mix(pineGreen, bark, isBark);

    if (isBark < 0.5) {
        col = mix(col, vec3(0.95, 0.96, 1.0), uSnow * smoothstep(0.1, 0.8, N.y));
    }

    vec3 L = normalize(uSunDir);
    float diff = max(dot(N, L), 0.0) * 0.85;

    vec3 moonDir = normalize(-uSunDir);
    float moonDiff = max(dot(N, moonDir), 0.0) * max(-uSunDir.y, 0.0) * 0.22;
    vec3 moonCol = vec3(0.45, 0.55, 0.75);

    vec3 lit = (uAmbient + vec3(uLightning * 0.9)) * col +
               diff * uSunColor * col +
               moonDiff * moonCol * col;

    float dist = length(vWorldPos - uCamPos);
    float fog = 1.0 - exp(-uFogDensity * dist * 0.018);

    FragColor = vec4(mix(lit, uFogColor, clamp(fog, 0.0, 1.0)), 1.0);
}
)";

// 4. Sky Shaders (Day/Night, Dynamic FBM Clouds, Sun, Crescent Moon, Stars, Lightning)
const char* skyVS = R"(
#version 330 core
layout (location = 0) in vec2 aPos;

out vec2 vUV;

void main() {
    vUV = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.9999, 1.0);
}
)";

const char* skyFS = R"(
#version 330 core
in vec2 vUV;

uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uTime;
uniform float uClouds;
uniform float uLightning;
uniform float uRain;

out vec4 FragColor;

float hash(vec2 p) {
    vec3 p3  = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    v += 0.5000 * noise(p); p *= 2.02;
    v += 0.2500 * noise(p); p *= 2.03;
    v += 0.1250 * noise(p); p *= 2.01;
    v += 0.0625 * noise(p);
    return v;
}

void main() {
    float elev = max(vUV.y, 0.001);
    vec3 sky = mix(uHorizon, uZenith, pow(elev, 0.55));

    float nightFactor = smoothstep(-0.04, -0.22, uSunDir.y);

    // 1. Stars at Night
    float starSeed = hash(floor(vUV * 500.0));
    float skyMask = smoothstep(0.12, 0.35, elev);
    float starIntensity = step(0.993, starSeed) * nightFactor * skyMask * (1.0 - uClouds * 0.9);
    sky += vec3(starIntensity * 2.8);

    // 2. Crescent Moon with craters & ethereal halo
    vec3 moonDir = -uSunDir;
    float aspect = 1280.0 / 720.0;
    vec2 uvCorr = vUV;
    uvCorr.x *= aspect;
    vec2 moonPos = vec2(0.5 + moonDir.x * 0.38, 0.22 + moonDir.y * 0.55);
    moonPos.x *= aspect;

    float moonDist = length(uvCorr - moonPos);
    float mainCircle = smoothstep(0.032, 0.030, moonDist);
    float shadowDist = length(uvCorr - (moonPos + vec2(0.011, 0.007)));
    float shadowCircle = smoothstep(0.032, 0.030, shadowDist);
    float crescent = clamp(mainCircle - shadowCircle, 0.0, 1.0) * nightFactor;
    float craters = hash(floor(uvCorr * 160.0)) * 0.3 + 0.7;

    vec3 moonCol = vec3(0.85, 0.90, 1.0);
    float moonGlow = exp(-moonDist * 22.0) * 0.40 * nightFactor;
    sky = mix(sky, moonCol * craters, crescent);
    sky += moonCol * moonGlow;

    // 3. Glowing Sun
    vec2 sunUV = vUV - vec2(0.5 + uSunDir.x * 0.38, 0.22 + uSunDir.y * 0.55);
    float sunDist = length(sunUV);
    float sunDisc = smoothstep(0.024, 0.020, sunDist) * max(uSunDir.y, 0.0);
    float sunGlow = exp(-sunDist * 8.0) * 0.85 * max(uSunDir.y, 0.0);
    sky = mix(sky, vec3(1.0, 0.98, 0.90) * 1.5, sunDisc);
    sky += uSunColor * sunGlow;

    // 4. Procedural Drifting Clouds (FBM)
    if (uClouds > 0.05 && elev > 0.02) {
        vec2 cloudCoord = (vUV - 0.5) / max(elev, 0.06) * 1.6;
        cloudCoord += vec2(uTime * 0.03, uTime * 0.01);
        float cNoise = fbm(cloudCoord * 2.5);
        float cloudCoverage = smoothstep(0.85 - uClouds * 0.75, 1.30 - uClouds * 0.65, cNoise);

        vec3 dayCloudColor   = vec3(0.92, 0.93, 0.96);
        vec3 stormCloudColor = vec3(0.12, 0.13, 0.18);
        vec3 nightCloudColor = vec3(0.03, 0.03, 0.07);

        vec3 cloudBase = mix(dayCloudColor, nightCloudColor, nightFactor);
        cloudBase = mix(cloudBase, stormCloudColor, clamp(uRain * 1.2, 0.0, 1.0));

        // Sun rim lighting on clouds
        cloudBase += uSunColor * exp(-sunDist * 3.5) * 0.5 * max(uSunDir.y, 0.0);

        // Lightning illumination on clouds
        cloudBase += vec3(uLightning * 1.2);

        sky = mix(sky, cloudBase, clamp(cloudCoverage * 1.3, 0.0, 0.98));
    }

    // 5. Lightning flash in the atmosphere
    sky += vec3(uLightning * 0.75);

    FragColor = vec4(sky, 1.0);
}
)";

// 5. Particle Shaders (Rain / Snow)
const char* particleVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aLife;
layout (location = 2) in float aType;

uniform mat4 uVP;

out float vLife;
out float vType;

void main() {
    vLife = aLife;
    vType = aType;
    gl_Position = uVP * vec4(aPos, 1.0);
    gl_PointSize = mix(6.0, 11.0, aType) * (vLife * 0.4 + 0.6);
}
)";

const char* particleFS = R"(
#version 330 core
in float vLife;
in float vType;

out vec4 FragColor;

void main() {
    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float d = dot(uv, uv);
    if (d > 1.0) discard;

    float alpha = vLife * (1.0 - d * 0.4);
    vec3 rainColor = vec3(0.75, 0.88, 1.0);
    vec3 snowColor = vec3(1.0, 1.0, 1.0);
    vec3 c = mix(rainColor, snowColor, vType);

    FragColor = vec4(c, alpha * 0.95);
}
)";

// 6. Airplane Shaders
const char* planeVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 uMVP;
uniform mat4 uModel;

out vec3 vPos;
out vec3 vWorldPos;

void main() {
    vPos = aPos;
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* planeFS = R"(
#version 330 core
in vec3 vPos;
in vec3 vWorldPos;
out vec4 fragColor;

uniform float uTime;
uniform bool uIsNight;
uniform float uFogDensity;
uniform vec3 uFogColor;
uniform vec3 uCamPos;
uniform float uLightning;

void main() {
    vec3 colorA = vec3(0.96, 0.96, 0.98);
    vec3 colorB = vec3(0.12, 0.28, 0.55);

    float brightness = uIsNight ? 0.25 : 1.0;
    colorA *= brightness;
    colorB *= brightness;

    float mixer = clamp(vPos.z * 0.4 + 0.5, 0.0, 1.0);
    vec3 finalColor = mix(colorA, colorB, mixer);

    if (abs(vPos.x) > 1.2) {
        finalColor = mix(finalColor, vec3(0.75, 0.15, 0.15) * brightness, 0.45);
    }

    if (vPos.z > 0.8 && vPos.z < 2.2 && vPos.y > 0.2) {
        finalColor = uIsNight ? vec3(0.10, 0.15, 0.25) : vec3(0.15, 0.35, 0.55);
    }

    if (uIsNight) {
        if (vPos.x < -5.5) {
            finalColor = vec3(1.0, 0.05, 0.05) * 2.0;
        } else if (vPos.x > 5.5) {
            finalColor = vec3(0.05, 1.0, 0.1) * 2.0;
        }
        float strobe = step(0.85, fract(uTime * 1.5));
        if (abs(vPos.x) < 0.2 && vPos.y > 1.8 && strobe > 0.5) {
            finalColor = vec3(1.5, 1.5, 1.5);
        }
    }

    finalColor += vec3(uLightning * 0.8);

    float dist = length(vWorldPos - uCamPos);
    float fog = 1.0 - exp(-uFogDensity * dist * 0.016);
    fog = clamp(fog, 0.0, 1.0);

    fragColor = vec4(mix(finalColor, uFogColor, fog), 1.0);
}
)";

// 7. Aurora Borealis Shaders
const char* auroraVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTex;
uniform mat4 uMVP;
uniform float uTime;
uniform float uLayerOffset;
out vec2 vTex;
out float vRay;
void main() {
    vec3 pos = aPos;
    float strength = aTex.y;
    float curve = sin(pos.x * 0.005 + uLayerOffset) * 40.0;
    pos.z += curve;
    vRay = 0.5 + 0.5 * sin(pos.x * 3.0 + uTime * 0.5 + uLayerOffset);
    pos.z += sin(pos.x * 0.04 + uTime * 0.8) * 8.0 * strength;
    pos.x += cos(uTime * 0.4 + uLayerOffset) * 5.0 * strength;
    vTex = aTex;
    gl_Position = uMVP * vec4(pos, 1.0);
}
)";

const char* auroraFS = R"(
#version 330 core
in vec2 vTex;
in float vRay;
out vec4 FragColor;
uniform float uAlpha;
void main() {
    vec3 green = vec3(0.05, 1.0, 0.45);
    vec3 purple = vec3(0.55, 0.15, 0.90);
    vec3 finalColor = mix(green, purple, vTex.y * 0.8);
    float verticalShape = smoothstep(0.0, 0.12, vTex.y) * exp(-vTex.y * 3.2);
    float rayEffect = smoothstep(0.4, 0.9, vRay);
    FragColor = vec4(finalColor, uAlpha * verticalShape * (0.5 + 0.5 * rayEffect));
}
)";

// 8. Firefly Shaders
const char* fireflyVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uVP;
uniform float uSize;
void main() {
    gl_Position = uVP * vec4(aPos, 1.0);
    gl_PointSize = uSize;
}
)";

const char* fireflyFS = R"(
#version 330 core
out vec4 FragColor;
uniform float uTime;
uniform vec3 uAmbient;
void main() {
    vec2 coord = gl_PointCoord * 2.0 - 1.0;
    float d = dot(coord, coord);
    if (d > 1.0) discard;
    float pulse = 0.6 + 0.4 * sin(uTime * 4.5);
    vec3 spiritColor = vec3(0.85, 1.0, 0.3);
    float alpha = (1.0 - d) * pulse;
    FragColor = vec4(spiritColor, alpha * 0.95);
}
)";

// 9. Bird Shaders
const char* birdVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* birdFS = R"(
#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main() {
    FragColor = vec4(uColor, 1.0);
}
)";

// ─── Geometry Builders ──────────────────────────────────────────────────────

struct BasicMesh {
    GLuint VAO, VBO;
    int count;
};

BasicMesh createPlaneMesh() {
    std::vector<float> verts;

    auto tri = [&](float x0,float y0,float z0,
                   float x1,float y1,float z1,
                   float x2,float y2,float z2) {
        verts.insert(verts.end(), {x0,y0,z0, x1,y1,z1, x2,y2,z2});
    };
    auto quad = [&](float x0,float y0,float z0,
                    float x1,float y1,float z1,
                    float x2,float y2,float z2,
                    float x3,float y3,float z3) {
        verts.insert(verts.end(), {x0,y0,z0, x1,y1,z1, x2,y2,z2});
        verts.insert(verts.end(), {x0,y0,z0, x2,y2,z2, x3,y3,z3});
    };

    const int FSIDES = 8;
    struct Ring { float z, r, yo; };
    Ring rings[] = {
        { 3.2f, 0.05f, 0.0f },  // nose tip
        { 2.5f, 0.28f, 0.05f }, // nose
        { 1.8f, 0.40f, 0.08f }, // cockpit front
        { 1.0f, 0.42f, 0.06f }, // cockpit rear
        { 0.0f, 0.40f, 0.0f  }, // mid / wing root
        {-1.0f, 0.37f, 0.0f  }, // aft
        {-2.0f, 0.28f, 0.0f  }, // tail taper
        {-3.0f, 0.15f, 0.0f  }, // tail
        {-3.8f, 0.04f, 0.0f  }  // tail tip
    };
    const int NR = 9;

    auto ringPt = [&](Ring& ring, int s) -> glm::vec3 {
        float a = (s / (float)FSIDES) * 2.0f * (float)M_PI;
        return glm::vec3(cos(a) * ring.r, sin(a) * ring.r + ring.yo, ring.z);
    };

    for (int r = 0; r < NR - 1; r++) {
        for (int s = 0; s < FSIDES; s++) {
            int s1 = (s + 1) % FSIDES;
            glm::vec3 a = ringPt(rings[r],   s);
            glm::vec3 b = ringPt(rings[r],   s1);
            glm::vec3 c = ringPt(rings[r+1], s1);
            glm::vec3 d = ringPt(rings[r+1], s);
            quad(a.x,a.y,a.z, b.x,b.y,b.z, c.x,c.y,c.z, d.x,d.y,d.z);
        }
    }

    {
        glm::vec3 tip(0.0f, rings[0].yo, rings[0].z);
        for (int s = 0; s < FSIDES; s++) {
            int s1 = (s + 1) % FSIDES;
            glm::vec3 a = ringPt(rings[1], s);
            glm::vec3 b = ringPt(rings[1], s1);
            tri(tip.x,tip.y,tip.z, a.x,a.y,a.z, b.x,b.y,b.z);
        }
    }

    {
        glm::vec3 tip(0.0f, rings[NR-1].yo, rings[NR-1].z);
        for (int s = 0; s < FSIDES; s++) {
            int s1 = (s + 1) % FSIDES;
            glm::vec3 a = ringPt(rings[NR-2], s);
            glm::vec3 b = ringPt(rings[NR-2], s1);
            tri(tip.x,tip.y,tip.z, b.x,b.y,b.z, a.x,a.y,a.z);
        }
    }

    for (int s = 0; s < 3; s++) {
        float a0 = (s / (float)FSIDES) * 2.0f * (float)M_PI;
        float a1 = ((s+1) / (float)FSIDES) * 2.0f * (float)M_PI;
        quad(
            cos(a0)*0.38f, sin(a0)*0.38f+0.25f, 1.8f,
            cos(a1)*0.38f, sin(a1)*0.38f+0.25f, 1.8f,
            cos(a1)*0.38f, sin(a1)*0.38f+0.25f, 0.8f,
            cos(a0)*0.38f, sin(a0)*0.38f+0.25f, 0.8f
        );
    }
    for (int s = FSIDES-3; s < FSIDES; s++) {
        float a0 = (s / (float)FSIDES) * 2.0f * (float)M_PI;
        float a1 = ((s+1) / (float)FSIDES) * 2.0f * (float)M_PI;
        quad(
            cos(a0)*0.38f, sin(a0)*0.38f+0.25f, 1.8f,
            cos(a1)*0.38f, sin(a1)*0.38f+0.25f, 1.8f,
            cos(a1)*0.38f, sin(a1)*0.38f+0.25f, 0.8f,
            cos(a0)*0.38f, sin(a0)*0.38f+0.25f, 0.8f
        );
    }

    float wt = 0.08f;
    quad( 0.4f, wt, 1.2f,   6.5f, wt*0.3f,-0.8f,
          6.5f, wt*0.3f,-2.0f,  0.4f, wt,-0.5f);
    quad( 0.4f,-wt, 1.2f,   0.4f,-wt,-0.5f,
          6.5f,-wt*0.3f,-2.0f,  6.5f,-wt*0.3f,-0.8f);
    quad( 0.4f, wt, 1.2f,   0.4f,-wt, 1.2f,
          6.5f,-wt*0.3f,-0.8f,  6.5f, wt*0.3f,-0.8f);
    quad( 0.4f, wt,-0.5f,   6.5f, wt*0.3f,-2.0f,
          6.5f,-wt*0.3f,-2.0f,  0.4f,-wt,-0.5f);
    quad( 6.5f, wt*0.3f,-0.8f, 6.5f,-wt*0.3f,-0.8f,
          6.5f,-wt*0.3f,-2.0f, 6.5f, wt*0.3f,-2.0f);

    quad(-0.4f, wt, 1.2f,  -0.4f, wt,-0.5f,
         -6.5f, wt*0.3f,-2.0f, -6.5f, wt*0.3f,-0.8f);
    quad(-0.4f,-wt, 1.2f,  -6.5f,-wt*0.3f,-0.8f,
         -6.5f,-wt*0.3f,-2.0f,  -0.4f,-wt,-0.5f);
    quad(-0.4f, wt, 1.2f,  -6.5f, wt*0.3f,-0.8f,
         -6.5f,-wt*0.3f,-0.8f,  -0.4f,-wt, 1.2f);
    quad(-0.4f, wt,-0.5f,  -0.4f,-wt,-0.5f,
         -6.5f,-wt*0.3f,-2.0f, -6.5f, wt*0.3f,-2.0f);
    quad(-6.5f, wt*0.3f,-0.8f, -6.5f, wt*0.3f,-2.0f,
         -6.5f,-wt*0.3f,-2.0f, -6.5f,-wt*0.3f,-0.8f);

    auto addNacelle = [&](float cx, float cz) {
        const int NS = 6;
        float nr = 0.18f, nl = 1.4f;
        for (int s = 0; s < NS; s++) {
            float a0 = (s/(float)NS)*2.0f*(float)M_PI;
            float a1 = ((s+1)/(float)NS)*2.0f*(float)M_PI;
            float x0=cos(a0)*nr, y0=sin(a0)*nr;
            float x1=cos(a1)*nr, y1=sin(a1)*nr;
            quad(cx+x0, -0.25f+y0, cz+0.6f,
                 cx+x1, -0.25f+y1, cz+0.6f,
                 cx+x1, -0.25f+y1, cz+0.6f-nl,
                 cx+x0, -0.25f+y0, cz+0.6f-nl);
        }
        glm::vec3 intakeTip(cx, -0.25f, cz+0.9f);
        for (int s = 0; s < NS; s++) {
            float a0 = (s/(float)NS)*2.0f*(float)M_PI;
            float a1 = ((s+1)/(float)NS)*2.0f*(float)M_PI;
            tri(intakeTip.x, intakeTip.y, intakeTip.z,
                cx+cos(a0)*nr, -0.25f+sin(a0)*nr, cz+0.6f,
                cx+cos(a1)*nr, -0.25f+sin(a1)*nr, cz+0.6f);
        }
        glm::vec3 exhaustTip(cx, -0.25f, cz+0.6f-nl-0.2f);
        for (int s = 0; s < NS; s++) {
            float a0 = (s/(float)NS)*2.0f*(float)M_PI;
            float a1 = ((s+1)/(float)NS)*2.0f*(float)M_PI;
            tri(exhaustTip.x, exhaustTip.y, exhaustTip.z,
                cx+cos(a1)*nr, -0.25f+sin(a1)*nr, cz+0.6f-nl,
                cx+cos(a0)*nr, -0.25f+sin(a0)*nr, cz+0.6f-nl);
        }
    };

    addNacelle( 2.8f,  0.2f);
    addNacelle(-2.8f,  0.2f);

    quad( 0.08f, 0.40f,-2.2f,   0.08f, 2.0f,-3.2f,
         -0.08f, 2.0f,-3.2f,   -0.08f, 0.40f,-2.2f);
    tri( 0.08f, 2.0f,-3.2f,  0.0f, 2.2f,-3.0f, -0.08f, 2.0f,-3.2f);
    quad( 0.08f, 0.40f,-2.2f, -0.08f, 0.40f,-2.2f,
         -0.08f, 2.0f,-3.2f,   0.08f, 2.0f,-3.2f);

    float hs = 0.06f;
    quad( 0.4f, hs,-2.5f,   2.8f, hs,-3.2f,   2.8f,-hs,-3.2f,   0.4f,-hs,-2.5f);
    tri( 2.8f, hs,-3.2f,  2.8f,-hs,-3.2f,  3.0f, 0.0f,-3.4f);
    quad(-0.4f, hs,-2.5f,  -0.4f,-hs,-2.5f,  -2.8f,-hs,-3.2f,  -2.8f, hs,-3.2f);
    tri(-2.8f, hs,-3.2f, -3.0f, 0.0f,-3.4f, -2.8f,-hs,-3.2f);

    BasicMesh mesh;
    mesh.count = (int)(verts.size() / 3);
    glGenVertexArrays(1, &mesh.VAO);
    glBindVertexArray(mesh.VAO);
    glGenBuffers(1, &mesh.VBO);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    return mesh;
}

BasicMesh createBirdMesh() {
    float verts[] = {
        0.0f, 0.0f, 0.0f,  -0.6f, 0.25f, -0.3f,
        0.0f, 0.0f, 0.0f,   0.6f, 0.25f, -0.3f
    };
    BasicMesh m;
    m.count = 4;
    glGenVertexArrays(1, &m.VAO);
    glGenBuffers(1, &m.VBO);
    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    return m;
}

struct FireflySystem {
    static const int COUNT = 120;
    std::vector<glm::vec3> positions;
    GLuint VAO, VBO;
    void init() {
        positions.resize(COUNT);
        for (int i = 0; i < COUNT; i++) resetFirefly(i);
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, COUNT * sizeof(glm::vec3), positions.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }
    void resetFirefly(int i) {
        float x = ((float)rand() / RAND_MAX - 0.5f) * 75.0f;
        float z = ((float)rand() / RAND_MAX - 0.5f) * 75.0f;
        float ground = terrainHeight(x, z);
        positions[i] = glm::vec3(x, ground + 0.8f + ((float)rand() / RAND_MAX) * 3.5f, z);
    }
    void update(float dt) {
        for (int i = 0; i < COUNT; i++) {
            positions[i].x += (((float)rand() / RAND_MAX) - 0.5f) * dt * 2.2f;
            positions[i].y += (((float)rand() / RAND_MAX) - 0.5f) * dt * 1.5f;
            positions[i].z += (((float)rand() / RAND_MAX) - 0.5f) * dt * 2.2f;
            float ground = terrainHeight(positions[i].x, positions[i].z);
            if (positions[i].y < ground + 0.5f) positions[i].y = ground + 0.6f;
            if (positions[i].y > ground + 6.0f) positions[i].y = ground + 5.5f;
        }
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, COUNT * sizeof(glm::vec3), positions.data());
    }
};

struct Terrain {
    GLuint VAO, VBO_pos, VBO_norm, VBO_uv, EBO;
    int indexCount;
};

void generateTerrain(Terrain& terrain) {
    const float TSIZE = 84.0f;
    const float TSTEP = 1.4f;
    const int tcols = (int)(TSIZE / TSTEP) + 1;
    const int trows = (int)(TSIZE / TSTEP) + 1;

    std::vector<float> vertices;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<unsigned int> indices;

    for (int iz = 0; iz < trows; iz++) {
        for (int ix = 0; ix < tcols; ix++) {
            float x = (ix / (float)(tcols - 1) - 0.5f) * TSIZE;
            float z = (iz / (float)(trows - 1) - 0.5f) * TSIZE;
            float y = terrainHeight(x, z);

            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);

            uvs.push_back(ix / (float)tcols);
            uvs.push_back(iz / (float)trows);

            const float eps = 0.3f;
            float hL = terrainHeight(x - eps, z);
            float hR = terrainHeight(x + eps, z);
            float hD = terrainHeight(x, z - eps);
            float hU = terrainHeight(x, z + eps);

            glm::vec3 n = glm::normalize(glm::vec3(hL - hR, 2.0f * eps, hD - hU));
            normals.push_back(n.x);
            normals.push_back(n.y);
            normals.push_back(n.z);
        }
    }

    for (int iz = 0; iz < trows - 1; iz++) {
        for (int ix = 0; ix < tcols - 1; ix++) {
            unsigned int a = iz * tcols + ix;
            indices.push_back(a);
            indices.push_back(a + 1);
            indices.push_back(a + tcols);
            indices.push_back(a + 1);
            indices.push_back(a + tcols + 1);
            indices.push_back(a + tcols);
        }
    }

    terrain.indexCount = indices.size();

    glGenVertexArrays(1, &terrain.VAO);
    glGenBuffers(1, &terrain.VBO_pos);
    glGenBuffers(1, &terrain.VBO_norm);
    glGenBuffers(1, &terrain.VBO_uv);
    glGenBuffers(1, &terrain.EBO);

    glBindVertexArray(terrain.VAO);

    glBindBuffer(GL_ARRAY_BUFFER, terrain.VBO_pos);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, terrain.VBO_norm);
    glBufferData(GL_ARRAY_BUFFER, normals.size() * sizeof(float), normals.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, terrain.VBO_uv);
    glBufferData(GL_ARRAY_BUFFER, uvs.size() * sizeof(float), uvs.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, terrain.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
}

// ─── Subdivided Water Grid Mesh ─────────────────────────────────────────────
struct WaterMesh {
    GLuint VAO, VBO_pos, VBO_uv, EBO;
    int indexCount;
};

void generateWaterMesh(WaterMesh& water) {
    const float minX = 7.0f, maxX = 33.0f;
    const float minZ = -28.0f, maxZ = 28.0f;
    const int cols = 50;
    const int rows = 50;
    const float waterY = -1.0f;

    std::vector<float> verts;
    std::vector<float> uvs;
    std::vector<unsigned int> indices;

    for (int r = 0; r <= rows; r++) {
        float fz = r / (float)rows;
        float z = minZ + fz * (maxZ - minZ);
        for (int c = 0; c <= cols; c++) {
            float fx = c / (float)cols;
            float x = minX + fx * (maxX - minX);

            verts.push_back(x);
            verts.push_back(waterY);
            verts.push_back(z);

            uvs.push_back(fx);
            uvs.push_back(fz);
        }
    }

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            unsigned int a = r * (cols + 1) + c;
            unsigned int b = a + 1;
            unsigned int d = (r + 1) * (cols + 1) + c;
            unsigned int e = d + 1;

            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(d);

            indices.push_back(b);
            indices.push_back(e);
            indices.push_back(d);
        }
    }

    water.indexCount = indices.size();

    glGenVertexArrays(1, &water.VAO);
    glGenBuffers(1, &water.VBO_pos);
    glGenBuffers(1, &water.VBO_uv);
    glGenBuffers(1, &water.EBO);

    glBindVertexArray(water.VAO);

    glBindBuffer(GL_ARRAY_BUFFER, water.VBO_pos);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, water.VBO_uv);
    glBufferData(GL_ARRAY_BUFFER, uvs.size() * sizeof(float), uvs.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, water.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
}

// ─── Forest / Tree Generation (Denser Forest) ───────────────────────────────
struct Forest {
    GLuint VAO, VBO_pos, VBO_norm, VBO_id;
    int vertexCount;
};

void addCylinder(std::vector<float>& verts, std::vector<float>& norms, std::vector<float>& ids,
                 float x, float y, float z, float r, float h, int segs, float treeID) {
    for (int i = 0; i < segs; i++) {
        float a0 = (i / (float)segs) * 2.0f * (float)M_PI;
        float a1 = ((i + 1) / (float)segs) * 2.0f * (float)M_PI;
        float x0 = cos(a0) * r, z0 = sin(a0) * r;
        float x1 = cos(a1) * r, z1 = sin(a1) * r;

        verts.push_back(x + x0); verts.push_back(y);     verts.push_back(z + z0);
        verts.push_back(x + x1); verts.push_back(y);     verts.push_back(z + z1);
        verts.push_back(x + x0); verts.push_back(y + h); verts.push_back(z + z0);

        norms.push_back(x0 / r); norms.push_back(0); norms.push_back(z0 / r);
        norms.push_back(x1 / r); norms.push_back(0); norms.push_back(z1 / r);
        norms.push_back(x0 / r); norms.push_back(0); norms.push_back(z0 / r);

        ids.push_back(treeID); ids.push_back(treeID); ids.push_back(treeID);

        verts.push_back(x + x1); verts.push_back(y);     verts.push_back(z + z1);
        verts.push_back(x + x1); verts.push_back(y + h); verts.push_back(z + z1);
        verts.push_back(x + x0); verts.push_back(y + h); verts.push_back(z + z0);

        norms.push_back(x1 / r); norms.push_back(0); norms.push_back(z1 / r);
        norms.push_back(x1 / r); norms.push_back(0); norms.push_back(z1 / r);
        norms.push_back(x0 / r); norms.push_back(0); norms.push_back(z0 / r);

        ids.push_back(treeID); ids.push_back(treeID); ids.push_back(treeID);
    }
}

void addCone(std::vector<float>& verts, std::vector<float>& norms, std::vector<float>& ids,
             float x, float y, float z, float r, float h, int segs, float treeID) {
    for (int i = 0; i < segs; i++) {
        float a0 = (i / (float)segs) * 2.0f * (float)M_PI;
        float a1 = ((i + 1) / (float)segs) * 2.0f * (float)M_PI;
        float x0 = cos(a0) * r, z0 = sin(a0) * r;
        float x1 = cos(a1) * r, z1 = sin(a1) * r;

        verts.push_back(x);      verts.push_back(y + h); verts.push_back(z);
        verts.push_back(x + x0); verts.push_back(y);     verts.push_back(z + z0);
        verts.push_back(x + x1); verts.push_back(y);     verts.push_back(z + z1);

        glm::vec3 n = glm::normalize(glm::vec3((x0 + x1) * 0.5f, r / h, (z0 + z1) * 0.5f));
        norms.push_back(n.x); norms.push_back(n.y); norms.push_back(n.z);
        norms.push_back(n.x); norms.push_back(n.y); norms.push_back(n.z);
        norms.push_back(n.x); norms.push_back(n.y); norms.push_back(n.z);

        ids.push_back(treeID); ids.push_back(treeID); ids.push_back(treeID);
    }
}

void generateForest(Forest& forest) {
    // Dense lush forest: 360 pine trees!
    const int NTREES = 360;
    std::vector<float> verts;
    std::vector<float> norms;
    std::vector<float> ids;

    for (int i = 0; i < NTREES; i++) {
        float seedX = (float)i * 13.17f + 7.3f;
        float seedZ = (float)i * 23.41f + 11.2f;
        float hx = std::fmod(std::abs(sin(seedX) * 43758.5453f), 1.0f);
        float hz = std::fmod(std::abs(sin(seedZ) * 43758.5453f), 1.0f);

        float tx = (hx - 0.5f) * 78.0f;
        float tz = (hz - 0.5f) * 78.0f;

        // Skip trees inside lake basin or too close to shoreline
        float ldx = (tx - 20.0f) / 13.0f;
        float ldz = tz / 28.0f;
        if (ldx * ldx + ldz * ldz < 1.15f) {
            continue;
        }

        float ty = terrainHeight(tx, tz);
        if (ty < -0.8f || ty > 6.5f) {
            continue;
        }

        float scale = 0.80f + std::fmod(std::abs(sin(seedX * 1.5f)), 1.0f) * 0.70f;
        float bh = 1.3f * scale;
        float br = 0.16f * scale;

        addCylinder(verts, norms, ids, tx, ty, tz, br, bh, 6, (float)i);

        int layers = 3;
        for (int L = 0; L < layers; L++) {
            float lr = (1.2f - L * 0.28f) * scale;
            float lh = (1.7f + L * 0.4f) * scale;
            float ly = bh + L * 0.75f * scale;
            addCone(verts, norms, ids, tx, ty + ly, tz, lr, lh, 8, (float)i);
        }
    }

    forest.vertexCount = verts.size() / 3;

    glGenVertexArrays(1, &forest.VAO);
    glGenBuffers(1, &forest.VBO_pos);
    glGenBuffers(1, &forest.VBO_norm);
    glGenBuffers(1, &forest.VBO_id);

    glBindVertexArray(forest.VAO);

    glBindBuffer(GL_ARRAY_BUFFER, forest.VBO_pos);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, forest.VBO_norm);
    glBufferData(GL_ARRAY_BUFFER, norms.size() * sizeof(float), norms.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, forest.VBO_id);
    glBufferData(GL_ARRAY_BUFFER, ids.size() * sizeof(float), ids.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

// ─── Weather Particle System (Rain & Snow) ──────────────────────────────────
struct ParticleSystem {
    static const int NPART = 2600;
    std::vector<glm::vec3> positions;
    std::vector<float> lifetimes;
    std::vector<float> types;
    GLuint VAO, VBO_pos, VBO_life, VBO_type;

    void init(float wx, float wz) {
        positions.resize(NPART);
        lifetimes.resize(NPART);
        types.resize(NPART);

        for (int i = 0; i < NPART; i++) {
            resetParticle(i, wx, wz, 0.0f);
        }

        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO_pos);
        glGenBuffers(1, &VBO_life);
        glGenBuffers(1, &VBO_type);

        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO_pos);
        glBufferData(GL_ARRAY_BUFFER, NPART * sizeof(glm::vec3), NULL, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, VBO_life);
        glBufferData(GL_ARRAY_BUFFER, NPART * sizeof(float), NULL, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);

        glBindBuffer(GL_ARRAY_BUFFER, VBO_type);
        glBufferData(GL_ARRAY_BUFFER, NPART * sizeof(float), NULL, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
        glEnableVertexAttribArray(2);

        glBindVertexArray(0);
    }

    void resetParticle(int i, float wx, float wz, float baseHeight) {
        const float spread = 45.0f;
        positions[i].x = ((float)rand() / RAND_MAX - 0.5f) * spread + wx;
        positions[i].y = baseHeight + 8.0f + ((float)rand() / RAND_MAX) * 18.0f;
        positions[i].z = ((float)rand() / RAND_MAX - 0.5f) * spread + wz;
        lifetimes[i] = 0.6f + ((float)rand() / RAND_MAX) * 0.4f;
    }

    void update(float dt, const WeatherParams& weather, float wind, const glm::vec3& camPos) {
        bool isRaining = weather.rain > 0.03f;
        bool isSnowing = weather.snow > 0.03f;

        if (!isRaining && !isSnowing) {
            for (int i = 0; i < NPART; i++) {
                lifetimes[i] = glm::max(0.0f, lifetimes[i] - dt * 3.0f);
            }
            return;
        }

        float fallSpeed = isSnowing ? 3.5f : (18.0f + weather.rain * 10.0f);
        float windX = wind * (isSnowing ? 4.5f : 9.0f);
        float pType = isSnowing ? 1.0f : 0.0f;

        for (int i = 0; i < NPART; i++) {
            types[i] = pType;
            positions[i].y -= dt * fallSpeed;
            positions[i].x += dt * windX;

            if (isSnowing) {
                positions[i].z += sin((float)i * 0.5f + positions[i].y * 0.7f) * dt * 1.8f;
            }

            lifetimes[i] -= dt * (isSnowing ? 0.22f : 0.50f);

            float ground = terrainHeight(positions[i].x, positions[i].z);
            if (positions[i].x > 7.0f && positions[i].x < 33.0f && std::abs(positions[i].z) < 28.0f) {
                ground = glm::max(ground, -1.0f);
            }

            if (lifetimes[i] <= 0.0f || positions[i].y < ground) {
                resetParticle(i, camPos.x, camPos.z, camPos.y);
            }
        }
    }

    void updateGPU() {
        glBindBuffer(GL_ARRAY_BUFFER, VBO_pos);
        glBufferSubData(GL_ARRAY_BUFFER, 0, NPART * sizeof(glm::vec3), positions.data());

        glBindBuffer(GL_ARRAY_BUFFER, VBO_life);
        glBufferSubData(GL_ARRAY_BUFFER, 0, NPART * sizeof(float), lifetimes.data());

        glBindBuffer(GL_ARRAY_BUFFER, VBO_type);
        glBufferSubData(GL_ARRAY_BUFFER, 0, NPART * sizeof(float), types.data());
    }

    void render(GLuint program, const glm::mat4& VP) {
        glUseProgram(program);
        glBindVertexArray(VAO);

        glUniformMatrix4fv(glGetUniformLocation(program, "uVP"), 1, GL_FALSE, glm::value_ptr(VP));

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        glDrawArrays(GL_POINTS, 0, NPART);
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(0);
    }
};

// ─── Sky & Sun Colors ───────────────────────────────────────────────────────
struct SkyColors {
    glm::vec3 zenith;
    glm::vec3 horizon;
    glm::vec3 sunColor;
    glm::vec3 ambient;
};

SkyColors getSkyColors(float tod) {
    float hour = tod * 24.0f;
    SkyColors sc;

    auto lerp3 = [](const glm::vec3& a, const glm::vec3& b, float t) {
        return glm::mix(a, b, t);
    };

    if (hour < 5.0f || hour > 21.5f) { // Night
        sc.zenith = glm::vec3(0.008f, 0.010f, 0.025f);
        sc.horizon = glm::vec3(0.015f, 0.020f, 0.045f);
        sc.sunColor = glm::vec3(0.05f, 0.08f, 0.15f);
        sc.ambient = glm::vec3(0.025f, 0.030f, 0.055f);
    } else if (hour < 7.0f) { // Dawn / Sunrise
        float t = (hour - 5.0f) / 2.0f;
        sc.zenith = lerp3(glm::vec3(0.015f, 0.02f, 0.06f), glm::vec3(0.30f, 0.22f, 0.50f), t);
        sc.horizon = lerp3(glm::vec3(0.04f, 0.04f, 0.12f), glm::vec3(0.92f, 0.48f, 0.18f), t);
        sc.sunColor = lerp3(glm::vec3(0.2f, 0.15f, 0.2f), glm::vec3(1.0f, 0.65f, 0.25f), t);
        sc.ambient = lerp3(glm::vec3(0.03f, 0.03f, 0.08f), glm::vec3(0.28f, 0.20f, 0.18f), t);
    } else if (hour < 10.5f) { // Morning
        float t = (hour - 7.0f) / 3.5f;
        sc.zenith = lerp3(glm::vec3(0.30f, 0.22f, 0.50f), glm::vec3(0.20f, 0.42f, 0.75f), t);
        sc.horizon = lerp3(glm::vec3(0.92f, 0.48f, 0.18f), glm::vec3(0.58f, 0.70f, 0.85f), t);
        sc.sunColor = lerp3(glm::vec3(1.0f, 0.65f, 0.25f), glm::vec3(1.0f, 0.95f, 0.82f), t);
        sc.ambient = lerp3(glm::vec3(0.28f, 0.20f, 0.18f), glm::vec3(0.42f, 0.42f, 0.42f), t);
    } else if (hour < 16.5f) { // Midday
        sc.zenith = glm::vec3(0.20f, 0.42f, 0.75f);
        sc.horizon = glm::vec3(0.58f, 0.70f, 0.85f);
        sc.sunColor = glm::vec3(1.0f, 0.98f, 0.90f);
        sc.ambient = glm::vec3(0.44f, 0.44f, 0.44f);
    } else if (hour < 19.5f) { // Sunset / Golden hour
        float t = (hour - 16.5f) / 3.0f;
        sc.zenith = lerp3(glm::vec3(0.20f, 0.42f, 0.75f), glm::vec3(0.18f, 0.12f, 0.32f), t);
        sc.horizon = lerp3(glm::vec3(0.58f, 0.70f, 0.85f), glm::vec3(0.95f, 0.42f, 0.12f), t);
        sc.sunColor = lerp3(glm::vec3(1.0f, 0.98f, 0.90f), glm::vec3(1.0f, 0.50f, 0.15f), t);
        sc.ambient = lerp3(glm::vec3(0.44f, 0.44f, 0.44f), glm::vec3(0.24f, 0.15f, 0.10f), t);
    } else { // Twilight
        float t = (hour - 19.5f) / 2.0f;
        sc.zenith = lerp3(glm::vec3(0.18f, 0.12f, 0.32f), glm::vec3(0.008f, 0.010f, 0.025f), t);
        sc.horizon = lerp3(glm::vec3(0.95f, 0.42f, 0.12f), glm::vec3(0.015f, 0.020f, 0.045f), t);
        sc.sunColor = lerp3(glm::vec3(1.0f, 0.50f, 0.15f), glm::vec3(0.05f, 0.08f, 0.15f), t);
        sc.ambient = lerp3(glm::vec3(0.24f, 0.15f, 0.10f), glm::vec3(0.025f, 0.030f, 0.055f), t);
    }

    return sc;
}

glm::vec3 getSunDirection(float tod) {
    float angle = (tod - 0.25f) * 2.0f * (float)M_PI; // Sunrise at 0.25 (tod = 6:00)
    float elevation = sin(angle);
    float horizontal = cos(angle);
    return glm::normalize(glm::vec3(horizontal, elevation, 0.22f));
}

void updateWindowTitle(GLFWwindow* window, int fps) {
    int hours = (int)(timeOfDay * 24.0f);
    int minutes = (int)((timeOfDay * 24.0f - hours) * 60.0f);

    char titleBuf[128];
    snprintf(titleBuf, sizeof(titleBuf),
             "3D Nature | [%02d:%02d] | %s | %d FPS %s",
             hours, minutes, weatherNames[currentWeather], fps,
             timePaused ? "(PAUSED)" : "");
    glfwSetWindowTitle(window, titleBuf);
}

// ─── Input Callbacks ───────────────────────────────────────────────────────
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        isMousePressed = (action == GLFW_PRESS);
        if (isMousePressed) {
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        isRightMousePressed = (action == GLFW_PRESS);
        if (isRightMousePressed) {
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
        }
    }
}

void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    double dx = xpos - lastMouseX;
    double dy = ypos - lastMouseY;

    if (isMousePressed) {
        camAngle += (float)dx * 0.005f;
        camPitch = glm::clamp(camPitch + (float)dy * 0.003f, 0.04f, 1.35f);
    } else if (isRightMousePressed) {
        glm::vec3 forward = glm::normalize(glm::vec3(-sin(camAngle), 0.0f, -cos(camAngle)));
        glm::vec3 right = glm::normalize(glm::vec3(cos(camAngle), 0.0f, -sin(camAngle)));
        camTarget += right * (float)(-dx * 0.05f) + forward * (float)(dy * 0.05f);
    }

    lastMouseX = xpos;
    lastMouseY = ypos;
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    camDist = glm::clamp(camDist - (float)yoffset * 2.2f, 8.0f, 95.0f);
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        switch (key) {
            case GLFW_KEY_1:
            case GLFW_KEY_KP_1:
                setWeather(CLEAR, window);
                break;

            case GLFW_KEY_2:
            case GLFW_KEY_KP_2:
                setWeather(CLOUDY, window);
                break;

            case GLFW_KEY_3:
            case GLFW_KEY_KP_3:
                setWeather(RAIN, window);
                break;

            case GLFW_KEY_4:
            case GLFW_KEY_KP_4:
                setWeather(STORM, window);
                break;

            case GLFW_KEY_5:
            case GLFW_KEY_KP_5:
                setWeather(FOG, window);
                break;

            case GLFW_KEY_6:
            case GLFW_KEY_KP_6:
                setWeather(SNOW, window);
                break;

            case GLFW_KEY_P:
                if (action == GLFW_PRESS) {
                    showPlane = !showPlane;
                    std::cout << (showPlane ? ">>> Airplane: VISIBLE" : ">>> Airplane: HIDDEN") << std::endl;
                }
                break;

            case GLFW_KEY_A:
                if (action == GLFW_PRESS) {
                    showAurora = !showAurora;
                    std::cout << (showAurora ? ">>> Aurora Borealis: ON" : ">>> Aurora Borealis: OFF") << std::endl;
                }
                break;

            case GLFW_KEY_B:
                if (action == GLFW_PRESS) {
                    showBirds = !showBirds;
                    std::cout << (showBirds ? ">>> Birds: VISIBLE" : ">>> Birds: HIDDEN") << std::endl;
                }
                break;

            case GLFW_KEY_F:
                if (action == GLFW_PRESS) {
                    showFireflies = !showFireflies;
                    std::cout << (showFireflies ? ">>> Fireflies: ON" : ">>> Fireflies: OFF") << std::endl;
                }
                break;

            case GLFW_KEY_SPACE:
                if (action == GLFW_PRESS) {
                    timePaused = !timePaused;
                    std::cout << (timePaused ? ">>> Time PAUSED" : ">>> Time RESUMED") << std::endl;
                    updateWindowTitle(window, 60);
                }
                break;
            case GLFW_KEY_UP:
                timeSpeed = glm::min(timeSpeed + 30.0f, 600.0f);
                std::cout << ">>> Time speed: " << timeSpeed << "x" << std::endl;
                break;
            case GLFW_KEY_DOWN:
                timeSpeed = glm::max(timeSpeed - 30.0f, 0.0f);
                std::cout << ">>> Time speed: " << timeSpeed << "x" << std::endl;
                break;
            case GLFW_KEY_RIGHT_BRACKET:
                timeOfDay = std::fmod(timeOfDay + 0.02f, 1.0f);
                break;
            case GLFW_KEY_LEFT_BRACKET:
                timeOfDay = std::fmod(timeOfDay - 0.02f + 1.0f, 1.0f);
                break;
            case GLFW_KEY_R:
                camAngle = 0.85f;
                camPitch = 0.28f;
                camDist = 48.0f;
                camTarget = glm::vec3(0.0f);
                std::cout << ">>> Camera reset to default view." << std::endl;
                break;
            case GLFW_KEY_W:
                camTarget.z -= 1.0f;
                break;
            case GLFW_KEY_S:
                camTarget.z += 1.0f;
                break;
            case GLFW_KEY_D:
                camTarget.x += 1.0f;
                break;
            case GLFW_KEY_ESCAPE:
                glfwSetWindowShouldClose(window, GLFW_TRUE);
                break;
        }
    }
}

void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    if (width > 0 && height > 0) {
        glViewport(0, 0, width, height);
    }
}

// ─── Main Function ──────────────────────────────────────────────────────────
int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "3D Nature Simulation", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetKeyCallback(window, keyCallback);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }

    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "\n=============================================" << std::endl;
    std::cout << "       3D DYNAMIC NATURE SIMULATION          " << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << " Controls:" << std::endl;
    std::cout << "  Left Mouse Drag  : Orbit Camera" << std::endl;
    std::cout << "  Right Mouse Drag : Pan Camera Target" << std::endl;
    std::cout << "  Mouse Scroll     : Zoom In / Out" << std::endl;
    std::cout << "  P                : Toggle Airplane (On/Off)" << std::endl;
    std::cout << "  A                : Toggle Aurora Borealis (Night)" << std::endl;
    std::cout << "  B                : Toggle Birds Flocks" << std::endl;
    std::cout << "  F                : Toggle Forest Fireflies (Night)" << std::endl;
    std::cout << "  Keys 1 - 6       : Change Weather (Top Row or Keypad):" << std::endl;
    std::cout << "                     [1] Clear Sky" << std::endl;
    std::cout << "                     [2] Cloudy" << std::endl;
    std::cout << "                     [3] Rain" << std::endl;
    std::cout << "                     [4] Thunderstorm" << std::endl;
    std::cout << "                     [5] Dense Fog" << std::endl;
    std::cout << "                     [6] Snowfall" << std::endl;
    std::cout << "  Spacebar         : Pause / Resume Day-Night Cycle" << std::endl;
    std::cout << "  [ / ]            : Manually Adjust Time of Day" << std::endl;
    std::cout << "  Up / Down Arrow  : Accelerate / Slow Time Progression" << std::endl;
    std::cout << "  R                : Reset Camera" << std::endl;
    std::cout << "  ESC              : Exit" << std::endl;
    std::cout << "=============================================\n" << std::endl;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_PROGRAM_POINT_SIZE);

    // Compile Shaders
    GLuint progTerrain  = createProgram(terrainVS, terrainFS);
    GLuint progWater    = createProgram(waterVS, waterFS);
    GLuint progTree     = createProgram(treeVS, treeFS);
    GLuint progSky      = createProgram(skyVS, skyFS);
    GLuint progParticle = createProgram(particleVS, particleFS);
    GLuint progPlane    = createProgram(planeVS, planeFS);
    GLuint progAurora   = createProgram(auroraVS, auroraFS);
    GLuint progFirefly  = createProgram(fireflyVS, fireflyFS);
    GLuint progBird     = createProgram(birdVS, birdFS);

    // Build Geometry
    Terrain terrain;
    generateTerrain(terrain);

    WaterMesh water;
    generateWaterMesh(water);

    Forest forest;
    generateForest(forest);

    ParticleSystem particles;
    particles.init(0.0f, 0.0f);

    BasicMesh planeMesh = createPlaneMesh();
    BasicMesh birdMesh  = createBirdMesh();

    FireflySystem fireflies;
    fireflies.init();

    // Fullscreen Sky Quad
    float skyVerts[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f
    };
    GLuint skyVAO, skyVBO;
    glGenVertexArrays(1, &skyVAO);
    glGenBuffers(1, &skyVBO);
    glBindVertexArray(skyVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyVerts), skyVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Aurora Quad Geometry
    float auroraVerts[] = {
        -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
         0.5f,  0.5f, 0.0f,  1.0f, 1.0f,
        -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
         0.5f,  0.5f, 0.0f,  1.0f, 1.0f,
        -0.5f,  0.5f, 0.0f,  0.0f, 1.0f
    };
    GLuint auroraVAO, auroraVBO;
    glGenVertexArrays(1, &auroraVAO);
    glGenBuffers(1, &auroraVBO);
    glBindVertexArray(auroraVAO);
    glBindBuffer(GL_ARRAY_BUFFER, auroraVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(auroraVerts), auroraVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    double lastTime = glfwGetTime();
    double fpsTimer = lastTime;
    int frameCount = 0;
    int currentFPS = 60;

    // Main Render Loop
    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        float dt = (float)(currentTime - lastTime);
        lastTime = currentTime;
        dt = glm::min(dt, 0.05f);

        // FPS and Title Bar Updates
        frameCount++;
        if (currentTime - fpsTimer >= 0.25) {
            currentFPS = (int)(frameCount / (currentTime - fpsTimer));
            frameCount = 0;
            fpsTimer = currentTime;
            updateWindowTitle(window, currentFPS);
        }

        // Time of Day Update
        if (!timePaused) {
            timeOfDay = std::fmod(timeOfDay + dt * timeSpeed / 86400.0f, 1.0f);
        }

        bool isNight = (timeOfDay < 0.22f || timeOfDay > 0.82f);

        // Fast & Responsive Weather Parameter Smoothing
        float lerpSpeed = glm::min(dt * 3.5f, 1.0f);
        weatherCurrent.clouds += (weatherTarget.clouds - weatherCurrent.clouds) * lerpSpeed;
        weatherCurrent.fog    += (weatherTarget.fog    - weatherCurrent.fog)    * lerpSpeed;
        weatherCurrent.rain   += (weatherTarget.rain   - weatherCurrent.rain)   * lerpSpeed;
        weatherCurrent.snow   += (weatherTarget.snow   - weatherCurrent.snow)   * lerpSpeed;

        // Storm Lightning Logic
        if (currentWeather == STORM) {
            lightningTimer -= dt;
            if (lightningTimer <= 0.0f) {
                lightningIntensity = 1.0f;
                lightningTimer = 3.0f + ((float)rand() / RAND_MAX) * 4.5f;
            }
        }
        if (lightningIntensity > 0.0f) {
            lightningIntensity = glm::max(0.0f, lightningIntensity - dt * 4.5f);
        }

        // Update Plane movement
        if (showPlane) {
            float planeSpeed = 22.0f;
            float leftBound = -140.0f;
            float rightBound = 140.0f;
            float range = rightBound - leftBound;

            planePos.x = leftBound + std::fmod((float)currentTime * planeSpeed, range);
            planePos.y = 26.0f + (float)sin((float)currentTime * 0.7f) * 1.5f;
            planePos.z = -28.0f + (float)cos((float)currentTime * 0.35f) * 6.0f;
            planeYaw = 1.57f;
        }

        // Update Fireflies
        if (showFireflies && isNight) {
            fireflies.update(dt);
        }

        // Camera Calculation
        float cx = camTarget.x + camDist * cos(camAngle) * cos(camPitch);
        float cy = camTarget.y + camDist * sin(camPitch) + 3.0f;
        float cz = camTarget.z + camDist * sin(camAngle) * cos(camPitch);
        glm::vec3 camPos(cx, cy, cz);

        int winW, winH;
        glfwGetFramebufferSize(window, &winW, &winH);
        float aspect = (winH > 0) ? ((float)winW / (float)winH) : (16.0f / 9.0f);

        glm::mat4 proj = glm::perspective(glm::radians(58.0f), aspect, 0.15f, 400.0f);
        glm::mat4 view = glm::lookAt(camPos, camTarget, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 VP = proj * view;
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 MVP = VP * model;

        // Lighting & Atmosphere
        glm::vec3 sunDir = getSunDirection(timeOfDay);
        SkyColors sc = getSkyColors(timeOfDay);

        float overcastDim = glm::clamp(weatherCurrent.clouds * 0.75f + weatherCurrent.rain * 0.25f, 0.0f, 0.85f);
        sc.sunColor *= (1.0f - overcastDim);
        sc.ambient = glm::mix(sc.ambient, glm::vec3(0.20f, 0.22f, 0.25f), overcastDim);

        if (weatherCurrent.fog > 0.05f) {
            sc.horizon = glm::mix(sc.horizon, glm::vec3(0.68f, 0.70f, 0.74f), weatherCurrent.fog * 0.9f);
            sc.zenith  = glm::mix(sc.zenith,  glm::vec3(0.52f, 0.56f, 0.62f), weatherCurrent.fog * 0.8f);
        }
        if (weatherCurrent.snow > 0.05f) {
            sc.horizon = glm::mix(sc.horizon, glm::vec3(0.75f, 0.78f, 0.85f), weatherCurrent.snow * 0.7f);
            sc.zenith  = glm::mix(sc.zenith,  glm::vec3(0.55f, 0.62f, 0.75f), weatherCurrent.snow * 0.5f);
        }
        if (currentWeather == STORM) {
            sc.horizon = glm::mix(sc.horizon, glm::vec3(0.12f, 0.13f, 0.16f), 0.85f);
            sc.zenith  = glm::mix(sc.zenith,  glm::vec3(0.08f, 0.09f, 0.12f), 0.85f);
        }

        float fogDensity = weatherCurrent.fog * 1.6f + manualFog * 0.2f;
        glm::vec3 fogColor = glm::mix(sc.horizon, sc.zenith, 0.25f);
        if (currentWeather == STORM) {
            fogColor = glm::mix(fogColor, glm::vec3(0.14f, 0.15f, 0.19f), 0.75f);
        }
        fogColor += glm::vec3(lightningIntensity * 0.6f);

        // Update Weather Particles
        particles.update(dt, weatherCurrent, manualWind + weatherCurrent.rain * 0.5f, camPos);
        if (weatherCurrent.rain > 0.02f || weatherCurrent.snow > 0.02f) {
            particles.updateGPU();
        }

        // ─── Rendering ──────────────────────────────────────────────────────
        glClearColor(fogColor.r, fogColor.g, fogColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 1. Sky Quad
        glDepthMask(GL_FALSE);
        glUseProgram(progSky);
        glBindVertexArray(skyVAO);

        glUniform3fv(glGetUniformLocation(progSky, "uZenith"), 1, glm::value_ptr(sc.zenith));
        glUniform3fv(glGetUniformLocation(progSky, "uHorizon"), 1, glm::value_ptr(sc.horizon));
        glUniform3fv(glGetUniformLocation(progSky, "uSunDir"), 1, glm::value_ptr(sunDir));
        glUniform3fv(glGetUniformLocation(progSky, "uSunColor"), 1, glm::value_ptr(sc.sunColor));
        glUniform1f(glGetUniformLocation(progSky, "uTime"), (float)currentTime);
        glUniform1f(glGetUniformLocation(progSky, "uClouds"), weatherCurrent.clouds);
        glUniform1f(glGetUniformLocation(progSky, "uLightning"), lightningIntensity);
        glUniform1f(glGetUniformLocation(progSky, "uRain"), weatherCurrent.rain);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        // 2. Aurora Borealis (Night Sky Curtains)
        if (showAurora && isNight && weatherCurrent.clouds < 0.8f) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glUseProgram(progAurora);
            glUniform1f(glGetUniformLocation(progAurora, "uTime"), (float)currentTime);
            glBindVertexArray(auroraVAO);

            for (int i = 0; i < 3; i++) {
                glUniform1f(glGetUniformLocation(progAurora, "uLayerOffset"), (float)i * 15.0f);
                glUniform1f(glGetUniformLocation(progAurora, "uAlpha"), 0.45f * (1.0f - weatherCurrent.clouds * 0.8f));
                glm::mat4 aModel = glm::mat4(1.0f);
                float ax = -80.0f + (float)(i * 25.0f);
                float ay = 45.0f + (float)(i * 8.0f);
                float az = -120.0f + (float)(i * 30.0f);
                aModel = glm::translate(aModel, glm::vec3(ax, ay, az));
                aModel = glm::rotate(aModel, glm::radians((float)(i * 12.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
                aModel = glm::scale(aModel, glm::vec3(350.0f, 60.0f, 1.0f));
                glm::mat4 aMVP = VP * aModel;
                glUniformMatrix4fv(glGetUniformLocation(progAurora, "uMVP"), 1, GL_FALSE, glm::value_ptr(aMVP));
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        glDepthMask(GL_TRUE);

        // 3. Terrain
        glUseProgram(progTerrain);
        glBindVertexArray(terrain.VAO);

        glUniformMatrix4fv(glGetUniformLocation(progTerrain, "uMVP"), 1, GL_FALSE, glm::value_ptr(MVP));
        glUniformMatrix4fv(glGetUniformLocation(progTerrain, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform3fv(glGetUniformLocation(progTerrain, "uSunDir"), 1, glm::value_ptr(sunDir));
        glUniform3fv(glGetUniformLocation(progTerrain, "uSunColor"), 1, glm::value_ptr(sc.sunColor));
        glUniform3fv(glGetUniformLocation(progTerrain, "uAmbient"), 1, glm::value_ptr(sc.ambient));
        glUniform3fv(glGetUniformLocation(progTerrain, "uFogColor"), 1, glm::value_ptr(fogColor));
        glUniform1f(glGetUniformLocation(progTerrain, "uFogDensity"), fogDensity);
        glUniform3fv(glGetUniformLocation(progTerrain, "uCamPos"), 1, glm::value_ptr(camPos));
        glUniform1f(glGetUniformLocation(progTerrain, "uSnow"), weatherCurrent.snow);
        glUniform1f(glGetUniformLocation(progTerrain, "uRain"), weatherCurrent.rain);
        glUniform1f(glGetUniformLocation(progTerrain, "uLightning"), lightningIntensity);

        glDrawElements(GL_TRIANGLES, terrain.indexCount, GL_UNSIGNED_INT, 0);

        // 4. Pine Forest (Denser)
        glUseProgram(progTree);
        glBindVertexArray(forest.VAO);

        glUniformMatrix4fv(glGetUniformLocation(progTree, "uVP"), 1, GL_FALSE, glm::value_ptr(VP));
        glUniform3fv(glGetUniformLocation(progTree, "uSunDir"), 1, glm::value_ptr(sunDir));
        glUniform3fv(glGetUniformLocation(progTree, "uSunColor"), 1, glm::value_ptr(sc.sunColor));
        glUniform3fv(glGetUniformLocation(progTree, "uAmbient"), 1, glm::value_ptr(sc.ambient));
        glUniform3fv(glGetUniformLocation(progTree, "uFogColor"), 1, glm::value_ptr(fogColor));
        glUniform1f(glGetUniformLocation(progTree, "uFogDensity"), fogDensity);
        glUniform3fv(glGetUniformLocation(progTree, "uCamPos"), 1, glm::value_ptr(camPos));
        glUniform1f(glGetUniformLocation(progTree, "uTime"), (float)currentTime);
        glUniform1f(glGetUniformLocation(progTree, "uWind"), manualWind + weatherCurrent.rain * 0.5f);
        glUniform1f(glGetUniformLocation(progTree, "uSnow"), weatherCurrent.snow);
        glUniform1f(glGetUniformLocation(progTree, "uLightning"), lightningIntensity);

        glDrawArrays(GL_TRIANGLES, 0, forest.vertexCount);

        // 5. Water Surface
        glUseProgram(progWater);
        glBindVertexArray(water.VAO);

        glUniformMatrix4fv(glGetUniformLocation(progWater, "uMVP"), 1, GL_FALSE, glm::value_ptr(MVP));
        glUniformMatrix4fv(glGetUniformLocation(progWater, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform3fv(glGetUniformLocation(progWater, "uSunDir"), 1, glm::value_ptr(sunDir));
        glUniform3fv(glGetUniformLocation(progWater, "uSunColor"), 1, glm::value_ptr(sc.sunColor));
        glUniform3fv(glGetUniformLocation(progWater, "uAmbient"), 1, glm::value_ptr(sc.ambient));
        glUniform3fv(glGetUniformLocation(progWater, "uFogColor"), 1, glm::value_ptr(fogColor));
        glUniform1f(glGetUniformLocation(progWater, "uFogDensity"), fogDensity);
        glUniform3fv(glGetUniformLocation(progWater, "uCamPos"), 1, glm::value_ptr(camPos));
        glUniform1f(glGetUniformLocation(progWater, "uLightning"), lightningIntensity);
        glUniform1f(glGetUniformLocation(progWater, "uTime"), (float)currentTime);
        glUniform1f(glGetUniformLocation(progWater, "uSnow"), weatherCurrent.snow);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawElements(GL_TRIANGLES, water.indexCount, GL_UNSIGNED_INT, 0);

        // 6. Airplane
        if (showPlane) {
            glUseProgram(progPlane);
            glBindVertexArray(planeMesh.VAO);

            glUniform1f(glGetUniformLocation(progPlane, "uTime"), (float)currentTime);
            glUniform1i(glGetUniformLocation(progPlane, "uIsNight"), isNight ? 1 : 0);
            glUniform1f(glGetUniformLocation(progPlane, "uFogDensity"), fogDensity);
            glUniform3fv(glGetUniformLocation(progPlane, "uFogColor"), 1, glm::value_ptr(fogColor));
            glUniform3fv(glGetUniformLocation(progPlane, "uCamPos"), 1, glm::value_ptr(camPos));
            glUniform1f(glGetUniformLocation(progPlane, "uLightning"), lightningIntensity);

            glm::mat4 pModel = glm::mat4(1.0f);
            pModel = glm::translate(pModel, planePos);
            pModel = glm::rotate(pModel, planeYaw, glm::vec3(0.0f, 1.0f, 0.0f));
            pModel = glm::rotate(pModel, (float)(0.04f * sin((float)currentTime * 1.8f)), glm::vec3(1.0f, 0.0f, 0.0f));
            pModel = glm::rotate(pModel, (float)(0.05f * cos((float)currentTime * 1.4f)), glm::vec3(0.0f, 0.0f, 1.0f));
            pModel = glm::scale(pModel, glm::vec3(1.7f));

            glm::mat4 planeMVP = VP * pModel;
            glUniformMatrix4fv(glGetUniformLocation(progPlane, "uMVP"), 1, GL_FALSE, glm::value_ptr(planeMVP));
            glUniformMatrix4fv(glGetUniformLocation(progPlane, "uModel"), 1, GL_FALSE, glm::value_ptr(pModel));

            glDrawArrays(GL_TRIANGLES, 0, planeMesh.count);
            glBindVertexArray(0);
        }

        // 7. Flying Birds (Flocks in Sky)
        if (showBirds && !isNight && weatherCurrent.fog < 0.8f) {
            glUseProgram(progBird);
            glBindVertexArray(birdMesh.VAO);
            glLineWidth(2.5f);
            glUniform3f(glGetUniformLocation(progBird, "uColor"), 0.12f, 0.14f, 0.18f);

            for (int i = 0; i < 35; i++) {
                float bSpeed = 7.0f + std::fmod((float)i * 1.4f, 8.0f);
                float rangeX = 240.0f;
                float startX = 120.0f;
                float bX = startX - std::fmod(((float)currentTime * 2.0f + (float)i * 14.0f) * bSpeed, rangeX);
                float bY = 16.0f + std::fmod((float)i * 4.5f, 22.0f) + (float)sin((float)currentTime * 1.8f + (float)i) * 1.2f;
                float bZ = -15.0f - std::fmod((float)i * 11.0f, 55.0f);

                glm::mat4 bModel = glm::mat4(1.0f);
                bModel = glm::translate(bModel, glm::vec3(bX, bY, bZ));
                bModel = glm::rotate(bModel, 1.57f, glm::vec3(0.0f, 1.0f, 0.0f));
                float flapFactor = (float)sin((float)currentTime * 14.0f + ((float)i * 0.7f));
                float wingScaleY = 0.6f + (flapFactor * 0.45f);
                bModel = glm::scale(bModel, glm::vec3(1.8f, wingScaleY, 1.0f));

                glm::mat4 bMVP = VP * bModel;
                glUniformMatrix4fv(glGetUniformLocation(progBird, "uMVP"), 1, GL_FALSE, glm::value_ptr(bMVP));
                glDrawArrays(GL_LINES, 0, birdMesh.count);
            }
            glLineWidth(1.0f);
            glBindVertexArray(0);
        }

        // 8. Fireflies (Glowing around the forest at night)
        if (showFireflies && isNight) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glDepthMask(GL_FALSE);
            glUseProgram(progFirefly);

            glUniformMatrix4fv(glGetUniformLocation(progFirefly, "uVP"), 1, GL_FALSE, glm::value_ptr(VP));
            glUniform1f(glGetUniformLocation(progFirefly, "uTime"), (float)currentTime);
            glUniform1f(glGetUniformLocation(progFirefly, "uSize"), 7.5f);
            glUniform3fv(glGetUniformLocation(progFirefly, "uAmbient"), 1, glm::value_ptr(sc.ambient));

            glBindVertexArray(fireflies.VAO);
            glDrawArrays(GL_POINTS, 0, FireflySystem::COUNT);
            glDepthMask(GL_TRUE);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBindVertexArray(0);
        }

        // 9. Rain & Snow Particles
        if (weatherCurrent.rain > 0.03f || weatherCurrent.snow > 0.03f) {
            particles.render(progParticle, VP);
        }

        glBindVertexArray(0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup Resources
    glDeleteProgram(progTerrain);
    glDeleteProgram(progWater);
    glDeleteProgram(progTree);
    glDeleteProgram(progSky);
    glDeleteProgram(progParticle);
    glDeleteProgram(progPlane);
    glDeleteProgram(progAurora);
    glDeleteProgram(progFirefly);
    glDeleteProgram(progBird);

    glDeleteVertexArrays(1, &terrain.VAO);
    glDeleteVertexArrays(1, &water.VAO);
    glDeleteVertexArrays(1, &forest.VAO);
    glDeleteVertexArrays(1, &particles.VAO);
    glDeleteVertexArrays(1, &skyVAO);
    glDeleteVertexArrays(1, &auroraVAO);
    glDeleteBuffers(1, &auroraVBO);
    glDeleteVertexArrays(1, &fireflies.VAO);
    glDeleteBuffers(1, &fireflies.VBO);
    glDeleteVertexArrays(1, &birdMesh.VAO);
    glDeleteBuffers(1, &birdMesh.VBO);
    glDeleteVertexArrays(1, &planeMesh.VAO);
    glDeleteBuffers(1, &planeMesh.VBO);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
