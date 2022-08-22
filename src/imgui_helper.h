#pragma once

#include <skygfx/skygfx.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include "imgui_impl_glfw.h"

class ImguiHelper : skygfx::noncopyable
{
public:
	ImguiHelper(GLFWwindow* window);
	~ImguiHelper();

	// TODO: pass device as const ref
	void draw(skygfx::Device& device);
	void newFrame();

private:
	std::shared_ptr<skygfx::Texture> mFontTexture = nullptr;
	std::shared_ptr<skygfx::Shader> mShader = nullptr;
};
