#include "PmFx.h"

#include <libultraship/libultraship.h>
#include <algorithm>
#include <cmath>

#if defined(ENABLE_OPENGL) || defined(__ANDROID__)
#if defined(__ANDROID__) || defined(USE_OPENGLES)
#include <GLES3/gl3.h>
#define PM_GLES 1
#elif defined(__APPLE__) || defined(_WIN32)
#include <GL/glew.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL_opengl.h>
#endif
#define PM_HAS_GL 1
#endif

namespace Pm {
namespace Fx {

#ifdef PM_HAS_GL

struct Target {
    GLuint fbo = 0;
    GLuint tex = 0;
    int w = 0, h = 0;
};

// Downsample chain: full -> 1/2 -> 1/4 -> 1/8. The blur runs at 1/8 between the last target and a scratch one.
static Target sDown[3];
static Target sScratch;
static Target sPage[kPageSlots];
static GLuint sProgram = 0;
static GLuint sVao = 0;
static GLuint sVbo = 0;
static GLint sUniTex = -1, sUniStep = -1, sUniSigma = -1;
static bool sProgramFailed = false;
static float sSigmaPx = 0.0f;
// The framebuffer ImGui was rendering to when a page capture started.
static GLint sScreenFbo = 0;

static void EnsureTarget(Target& t, int w, int h) {
    w = std::max(1, w);
    h = std::max(1, h);
    if (t.tex != 0 && t.w == w && t.h == h) {
        return;
    }
    GLint prevTex = 0, prevFbo = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    if (t.tex == 0) {
        glGenTextures(1, &t.tex);
        glGenFramebuffers(1, &t.fbo);
    }
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    // Start cleared so a texture that is shown before its first capture is not garbage.
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    t.w = w;
    t.h = h;
}

static void FramebufferSize(int* w, int* h) {
    ImGuiIO& io = ImGui::GetIO();
    *w = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
    *h = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
}

static GLuint Compile(GLenum type, const char* source) {
#ifdef PM_GLES
    const char* header = "#version 300 es\nprecision highp float;\n";
#else
    const char* header = "#version 120\n";
#endif
    const char* parts[2] = { header, source };
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 2, parts, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = "";
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        SPDLOG_ERROR("Pause menu: blur shader failed to compile: {}", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool EnsureProgram() {
    if (sProgram != 0) {
        return true;
    }
    if (sProgramFailed) {
        return false;
    }
#ifdef PM_GLES
    const char* vs = "in vec2 aPos;\nout vec2 vUv;\n"
                     "void main() { vUv = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    const char* fs = "uniform sampler2D uTex;\nuniform vec2 uStep;\nuniform float uSigma;\n"
                     "in vec2 vUv;\nout vec4 oColor;\n"
                     "void main() {\n"
                     "  vec3 acc = vec3(0.0); float sum = 0.0;\n"
                     "  for (int i = -8; i <= 8; i++) {\n"
                     "    float w = exp(-0.5 * float(i * i) / (uSigma * uSigma));\n"
                     "    acc += texture(uTex, vUv + uStep * float(i)).rgb * w; sum += w;\n"
                     "  }\n"
                     "  oColor = vec4(acc / sum, 1.0);\n"
                     "}\n";
#else
    const char* vs = "attribute vec2 aPos;\nvarying vec2 vUv;\n"
                     "void main() { vUv = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    const char* fs = "uniform sampler2D uTex;\nuniform vec2 uStep;\nuniform float uSigma;\n"
                     "varying vec2 vUv;\n"
                     "void main() {\n"
                     "  vec3 acc = vec3(0.0); float sum = 0.0;\n"
                     "  for (int i = -8; i <= 8; i++) {\n"
                     "    float w = exp(-0.5 * float(i * i) / (uSigma * uSigma));\n"
                     "    acc += texture2D(uTex, vUv + uStep * float(i)).rgb * w; sum += w;\n"
                     "  }\n"
                     "  gl_FragColor = vec4(acc / sum, 1.0);\n"
                     "}\n";
#endif
    GLuint v = Compile(GL_VERTEX_SHADER, vs);
    GLuint f = Compile(GL_FRAGMENT_SHADER, fs);
    if (v == 0 || f == 0) {
        sProgramFailed = true;
        return false;
    }
    sProgram = glCreateProgram();
    glAttachShader(sProgram, v);
    glAttachShader(sProgram, f);
    glBindAttribLocation(sProgram, 0, "aPos");
    glLinkProgram(sProgram);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(sProgram, GL_LINK_STATUS, &ok);
    if (!ok) {
        SPDLOG_ERROR("Pause menu: blur shader failed to link");
        glDeleteProgram(sProgram);
        sProgram = 0;
        sProgramFailed = true;
        return false;
    }
    sUniTex = glGetUniformLocation(sProgram, "uTex");
    sUniStep = glGetUniformLocation(sProgram, "uStep");
    sUniSigma = glGetUniformLocation(sProgram, "uSigma");

    // One triangle that covers the screen.
    static const GLfloat tri[6] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };
    GLint prevVao = 0, prevBuf = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuf);
    glGenVertexArrays(1, &sVao);
    glBindVertexArray(sVao);
    glGenBuffers(1, &sVbo);
    glBindBuffer(GL_ARRAY_BUFFER, sVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevBuf);
    return true;
}

static void BlurPass(const Target& src, const Target& dst, float stepX, float stepY, float sigma) {
    glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
    glViewport(0, 0, dst.w, dst.h);
    glBindTexture(GL_TEXTURE_2D, src.tex);
    glUniform2f(sUniStep, stepX, stepY);
    glUniform1f(sUniSigma, sigma);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void BackdropCallback(const ImDrawList*, const ImDrawCmd*) {
    int fbw = 0, fbh = 0;
    FramebufferSize(&fbw, &fbh);
    if (fbw <= 0 || fbh <= 0 || sDown[2].tex == 0) {
        return;
    }
    GLint screen = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &screen);
    glDisable(GL_SCISSOR_TEST);

    // Halve three times with linear blits: a 2x2 box filter each time, so nothing aliases.
    GLuint srcFbo = (GLuint)screen;
    int sw = fbw, sh = fbh;
    for (int i = 0; i < 3; i++) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sDown[i].fbo);
        glBlitFramebuffer(0, 0, sw, sh, 0, 0, sDown[i].w, sDown[i].h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        srcFbo = sDown[i].fbo;
        sw = sDown[i].w;
        sh = sDown[i].h;
    }

    if (EnsureProgram()) {
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_STENCIL_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(sProgram);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(sUniTex, 0);
        glBindVertexArray(sVao);
        // Sigma in texels of the 1/8 target. The loop covers +-8 texels, so keep sigma under ~3.2 and
        // widen the tap spacing instead when a bigger blur is asked for.
        float sigma = std::max(0.5f, sSigmaPx / 8.0f);
        float spacing = 1.0f;
        if (sigma > 3.2f) {
            spacing = sigma / 3.2f;
            sigma = 3.2f;
        }
        const Target& t = sDown[2];
        BlurPass(t, sScratch, spacing / (float)t.w, 0.0f, sigma);
        BlurPass(sScratch, t, 0.0f, spacing / (float)t.h, sigma);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)screen);
    glEnable(GL_SCISSOR_TEST);
}

static void BeginPageCallback(const ImDrawList*, const ImDrawCmd* cmd) {
    int slot = (int)(intptr_t)cmd->UserCallbackData;
    if (slot < 0 || slot >= kPageSlots || sPage[slot].fbo == 0) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, sPage[slot].fbo);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
}

// First capture of a run: remember where ImGui was drawing so EndPageCallback can go back to it.
static void BeginFirstPageCallback(const ImDrawList* list, const ImDrawCmd* cmd) {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &sScreenFbo);
    BeginPageCallback(list, cmd);
}

static void EndPageCallback(const ImDrawList*, const ImDrawCmd*) {
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)sScreenFbo);
    // Page textures hold premultiplied colour.
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

bool Available() {
    auto window = Ship::Context::GetInstance()->GetWindow();
    return window != nullptr && window->GetWindowBackend() == Ship::WindowBackend::FAST3D_SDL_OPENGL;
}

void QueueBackdrop(ImDrawList* drawList, float sigmaPx) {
    if (!Available()) {
        return;
    }
    int fbw = 0, fbh = 0;
    FramebufferSize(&fbw, &fbh);
    if (fbw <= 0 || fbh <= 0) {
        return;
    }
    EnsureTarget(sDown[0], fbw / 2, fbh / 2);
    EnsureTarget(sDown[1], fbw / 4, fbh / 4);
    EnsureTarget(sDown[2], fbw / 8, fbh / 8);
    EnsureTarget(sScratch, fbw / 8, fbh / 8);
    sSigmaPx = sigmaPx;
    drawList->AddCallback(BackdropCallback, nullptr);
    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

ImTextureID BackdropTexture() {
    return (ImTextureID)(intptr_t)sDown[2].tex;
}

static bool sCaptureOpen = false;

void BeginPageCapture(ImDrawList* drawList, int slot) {
    if (!Available()) {
        return;
    }
    int fbw = 0, fbh = 0;
    FramebufferSize(&fbw, &fbh);
    EnsureTarget(sPage[slot], fbw, fbh);
    drawList->AddCallback(sCaptureOpen ? BeginPageCallback : BeginFirstPageCallback, (void*)(intptr_t)slot);
    sCaptureOpen = true;
}

void EndPageCapture(ImDrawList* drawList) {
    if (!Available()) {
        return;
    }
    drawList->AddCallback(EndPageCallback, nullptr);
    sCaptureOpen = false;
}

void EndPageCompose(ImDrawList* drawList) {
    if (!Available()) {
        return;
    }
    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

ImTextureID PageTexture(int slot) {
    return (ImTextureID)(intptr_t)sPage[slot].tex;
}

static void Release(Target& t) {
    if (t.fbo != 0) {
        glDeleteFramebuffers(1, &t.fbo);
    }
    if (t.tex != 0) {
        glDeleteTextures(1, &t.tex);
    }
    t = Target();
}

void Shutdown() {
    for (auto& t : sDown) {
        Release(t);
    }
    Release(sScratch);
    for (auto& t : sPage) {
        Release(t);
    }
    if (sProgram != 0) {
        glDeleteProgram(sProgram);
        sProgram = 0;
    }
    if (sVbo != 0) {
        glDeleteBuffers(1, &sVbo);
        sVbo = 0;
    }
    if (sVao != 0) {
        glDeleteVertexArrays(1, &sVao);
        sVao = 0;
    }
}

#else // !PM_HAS_GL

bool Available() {
    return false;
}
void QueueBackdrop(ImDrawList*, float) {
}
ImTextureID BackdropTexture() {
    return (ImTextureID)0;
}
void BeginPageCapture(ImDrawList*, int) {
}
void EndPageCapture(ImDrawList*) {
}
void EndPageCompose(ImDrawList*) {
}
ImTextureID PageTexture(int) {
    return (ImTextureID)0;
}
void Shutdown() {
}

#endif

} // namespace Fx
} // namespace Pm
