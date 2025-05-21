#pragma once

#if defined(__USE_OPENGL__)

#include "Includes.h"
#include "Renderer.h"
#include "Color.h"
#include "Vector2.h"

class OpenGLRenderer : public Renderer {
public:
    void Initialize(HWND hwnd, HINSTANCE hInstance) override { /* OpenGL setup */ }
    void RenderFrame() override { /* OpenGL draw calls */ }
    void Cleanup() override { /* Cleanup OpenGL resources */ }

    void DrawRectangle(const Vector2& position, const Vector2& size, const MyColor& color, bool is2D) override { /* OpenGL logic */ }
    void DrawText(const std::string& text, const Vector2& position, const MyColor& color) override { /* OpenGL text rendering */ }
    void DrawTexture(const std::string& textureId, const Vector2& position, const Vector2& size, const MyColor& tintColor, bool is2D) override { /* OpenGL texture rendering */
    }
};


#endif //#if defined(__USE_OPENGL__)