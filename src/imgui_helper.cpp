#include "imgui_helper.h"

static const std::string imgui_vertex_shader_code = R"(
#version 450 core

layout(location = POSITION_LOCATION) in vec3 aPosition;
layout(location = COLOR_LOCATION) in vec4 aColor;
layout(location = TEXCOORD_LOCATION) in vec2 aTexCoord;

layout(binding = 1) uniform _matrices
{
	mat4 projection;
	mat4 view;
	mat4 model;
} matrices;

layout(location = 0) out struct {
	vec4 color;
	vec2 tex_coord;
} Out;

out gl_PerVertex { vec4 gl_Position; };

void main()
{
	Out.tex_coord = aTexCoord;
	Out.color = aColor;
#ifdef FLIP_TEXCOORD_Y
	Out.tex_coord.y = 1.0 - Out.tex_coord.y;
#endif
	gl_Position = matrices.projection * matrices.view * matrices.model * vec4(aPosition, 1.0);
})";

static const std::string imgui_fragment_shader_code = R"(
#version 450 core

layout(location = 0) in struct {
	vec4 color;
	vec2 tex_coord;
} In;

layout(location = 0) out vec4 result;

layout(binding = 0) uniform sampler2D sColorTexture;

void main()
{ 
	result = texture(sColorTexture, In.tex_coord) * In.color;
})";

ImguiHelper::ImguiHelper(GLFWwindow* window)
{
	ImGui::CreateContext();
	ImGui::StyleColorsClassic();

	ImGui_ImplGlfw_InitForOpenGL(window, true);

	auto& io = ImGui::GetIO();

	io.IniFilename = NULL;
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

	uint8_t* data;
	int32_t width;
	int32_t height;

	io.Fonts->GetTexDataAsRGBA32(&data, &width, &height);
	mFontTexture = std::make_shared<skygfx::Texture>(width, height, 4, data);
	io.Fonts->TexID = mFontTexture.get();

	const skygfx::Vertex::Layout vertex_layout = { sizeof(ImDrawVert), {
		{ skygfx::Vertex::Location::Position, skygfx::Vertex::Attribute::Format::Float2, offsetof(ImDrawVert, pos) },
		{ skygfx::Vertex::Location::Color, skygfx::Vertex::Attribute::Format::Byte3, offsetof(ImDrawVert, col) },
		{ skygfx::Vertex::Location::TexCoord, skygfx::Vertex::Attribute::Format::Float2, offsetof(ImDrawVert, uv) } }
	};

	mShader = std::make_shared<skygfx::Shader>(vertex_layout, imgui_vertex_shader_code, imgui_fragment_shader_code);
}

ImguiHelper::~ImguiHelper()
{
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}

void ImguiHelper::draw()
{
	ImGui::Render();

	skygfx::SetTopology(skygfx::Topology::TriangleList);
	skygfx::SetSampler(skygfx::Sampler::Nearest);
	skygfx::SetShader(*mShader);
	skygfx::SetBlendMode(skygfx::BlendStates::NonPremultiplied);
	skygfx::SetDepthMode(std::nullopt);
	skygfx::SetCullMode(skygfx::CullMode::None);

	struct alignas(16) ImguiMatrices
	{
		glm::mat4 projection = glm::mat4(1.0f);
		glm::mat4 view = glm::mat4(1.0f);
		glm::mat4 model = glm::mat4(1.0f);
	};

	ImguiMatrices matrices;

	auto width = (float)skygfx::GetBackbufferWidth();
	auto height = (float)skygfx::GetBackbufferHeight();

	auto display_scale = ImGui::GetIO().DisplayFramebufferScale;
	
	width /= display_scale.x;
	height /= display_scale.y;

	matrices.projection = glm::orthoLH(0.0f, width, height, 0.0f, -1.0f, 1.0f);
	matrices.view = glm::lookAtLH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));

	skygfx::SetDynamicUniformBuffer(1, matrices);

	auto draw_data = ImGui::GetDrawData();

	for (int i = 0; i < draw_data->CmdListsCount; i++)
	{
		draw_data->ScaleClipRects(display_scale);

		const auto cmds = draw_data->CmdLists[i];

		skygfx::SetDynamicVertexBuffer(cmds->VtxBuffer.Data, static_cast<size_t>(cmds->VtxBuffer.size()));
		skygfx::SetDynamicIndexBuffer(cmds->IdxBuffer.Data, static_cast<size_t>(cmds->IdxBuffer.size()));

		int index_offset = 0;

		for (auto& cmd : cmds->CmdBuffer)
		{
			if (cmd.UserCallback)
			{
				cmd.UserCallback(cmds, &cmd);
			}
			else
			{
				skygfx::SetScissor(skygfx::Scissor{
					.position = { cmd.ClipRect.x, cmd.ClipRect.y },
					.size = { cmd.ClipRect.z - cmd.ClipRect.x, cmd.ClipRect.w - cmd.ClipRect.y }
				});
				skygfx::SetTexture(0, *(skygfx::Texture*)cmd.TextureId);
				skygfx::DrawIndexed(cmd.ElemCount, index_offset);
			}
			index_offset += cmd.ElemCount;
		}
	}

	skygfx::SetScissor(std::nullopt);
}

void ImguiHelper::newFrame()
{
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}
