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
		{ skygfx::Vertex::Attribute::Type::Position, skygfx::Vertex::Attribute::Format::R32G32F, offsetof(ImDrawVert, pos) },
		{ skygfx::Vertex::Attribute::Type::Color, skygfx::Vertex::Attribute::Format::R8G8B8A8UN, offsetof(ImDrawVert, col) },
		{ skygfx::Vertex::Attribute::Type::TexCoord, skygfx::Vertex::Attribute::Format::R32G32F, offsetof(ImDrawVert, uv) } }
	};

	mShader = std::make_shared<skygfx::Shader>(vertex_layout, imgui_vertex_shader_code, imgui_fragment_shader_code);
}

ImguiHelper::~ImguiHelper()
{
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}

void ImguiHelper::draw(skygfx::Device& device)
{
	ImGui::Render();

	device.setTopology(skygfx::Topology::TriangleList);
	device.setSampler(skygfx::Sampler::Nearest);
	device.setShader(*mShader);
	device.setBlendMode(skygfx::BlendStates::NonPremultiplied);
	device.setDepthMode(std::nullopt);
	device.setCullMode(skygfx::CullMode::None);

	struct alignas(16) ImguiMatrices
	{
		glm::mat4 projection = glm::mat4(1.0f);
		glm::mat4 view = glm::mat4(1.0f);
		glm::mat4 model = glm::mat4(1.0f);
	};

	ImguiMatrices matrices;

	auto width = device.getBackbufferWidth();
	auto height = device.getBackbufferHeight();

	matrices.projection = glm::orthoLH(0.0f, (float)width, (float)height, 0.0f, -1.0f, 1.0f);
	matrices.view = glm::lookAtLH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));

	device.setDynamicUniformBuffer(1, matrices);

	auto draw_data = ImGui::GetDrawData();

	for (int i = 0; i < draw_data->CmdListsCount; i++)
	{
		const auto cmds = draw_data->CmdLists[i];

		device.setDynamicVertexBuffer(cmds->VtxBuffer.Data, static_cast<size_t>(cmds->VtxBuffer.size()));
		device.setDynamicIndexBuffer(cmds->IdxBuffer.Data, static_cast<size_t>(cmds->IdxBuffer.size()));

		int index_offset = 0;

		for (auto& cmd : cmds->CmdBuffer)
		{
			if (cmd.UserCallback)
			{
				cmd.UserCallback(cmds, &cmd);
			}
			else
			{
				device.setTexture(0, *(skygfx::Texture*)cmd.TextureId);
				device.setScissor(skygfx::Scissor{ {cmd.ClipRect.x, cmd.ClipRect.y }, { cmd.ClipRect.z - cmd.ClipRect.x, cmd.ClipRect.w - cmd.ClipRect.y } });
				device.drawIndexed(cmd.ElemCount, index_offset);
			}
			index_offset += cmd.ElemCount;
		}
	}

	device.setScissor(std::nullopt);
}

void ImguiHelper::newFrame()
{
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}