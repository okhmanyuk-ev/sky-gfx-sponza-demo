#include "imgui_helper.h"
#include <skygfx/extended.h>

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
}

ImguiHelper::~ImguiHelper()
{
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}

void ImguiHelper::draw()
{
	ImGui::Render();

	skygfx::SetSampler(skygfx::Sampler::Nearest);
	skygfx::SetBlendMode(skygfx::BlendStates::NonPremultiplied);
	skygfx::SetDepthMode(std::nullopt);
	skygfx::SetCullMode(skygfx::CullMode::None);

	auto display_scale = ImGui::GetIO().DisplayFramebufferScale;

	auto camera = skygfx::extended::OrthogonalCamera{};
	auto model = glm::scale(glm::mat4(1.0f), { display_scale.x, display_scale.y, 1.0f });

	auto draw_data = ImGui::GetDrawData();
	draw_data->ScaleClipRects(display_scale);

	for (int i = 0; i < draw_data->CmdListsCount; i++)
	{
		const auto cmds = draw_data->CmdLists[i];

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

				auto vertices = skygfx::extended::Mesh::Vertices();
				
				for (uint32_t i = 0; i < cmd.ElemCount; i++)
				{
					auto index = cmds->IdxBuffer[i + index_offset];
					const auto& vertex = cmds->VtxBuffer[index];

					auto color = (uint8_t*)&vertex.col;

					vertices.push_back(skygfx::extended::Mesh::Vertex{
						.pos = { vertex.pos.x, vertex.pos.y, 0.0f },
						.color = {
							color[0] / 255.0f,
							color[1] / 255.0f,
							color[2] / 255.0f,
							color[3] / 255.0f
						},
						.texcoord = { vertex.uv.x, vertex.uv.y }
					});
				}

				auto mesh = skygfx::extended::Mesh();
				mesh.setVertices(vertices);
				
				auto material = skygfx::extended::Material{
					.color_texture = (skygfx::Texture*)cmd.TextureId
				};
				
				skygfx::extended::DrawMesh(mesh, camera, model, material);
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
