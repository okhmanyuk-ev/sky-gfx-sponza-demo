#include <iostream>
#include <unordered_map>
#include <tiny_gltf.h>
#include <imgui.h>
#include <skygfx/utils.h>
#include "../lib/skygfx/examples/utils/utils.h"
#include "../lib/skygfx/examples/utils/imgui_helper.h"
#include <imgui_impl_glfw.h>

static double cursor_saved_pos_x = 0.0;
static double cursor_saved_pos_y = 0.0;
static bool cursor_is_interacting = false;

bool IsImguiInteracting()
{
	return !(!ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByPopup)
		&& !ImGui::IsAnyItemActive());
}

void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT)
	{
		if (action == GLFW_PRESS && !cursor_is_interacting)
		{
			if (IsImguiInteracting())
				return;

			cursor_is_interacting = true;
			glfwGetCursorPos(window, &cursor_saved_pos_x, &cursor_saved_pos_y);
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
		}
		else if (action == GLFW_RELEASE && cursor_is_interacting)
		{
			cursor_is_interacting = false;
			glfwSetCursorPos(window, cursor_saved_pos_x, cursor_saved_pos_y);
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}
}

struct Material
{
	std::shared_ptr<skygfx::Texture> color_texture;
	std::shared_ptr<skygfx::Texture> normal_texture;
	std::shared_ptr<skygfx::Texture> metallic_roughness_texture;
	glm::vec4 color;
};

struct RenderBuffer
{
	// TODO: remove unique_ptr and use move semantics for mesh
	std::unordered_map<std::shared_ptr<Material>, std::vector<std::pair<std::unique_ptr<skygfx::utils::Mesh>, skygfx::utils::DrawCommand>>> meshes;
};

RenderBuffer BuildRenderBuffer(const tinygltf::Model& model)
{
	// https://github.com/syoyo/tinygltf/blob/master/examples/glview/glview.cc
	// https://github.com/syoyo/tinygltf/blob/master/examples/basic/main.cpp

	RenderBuffer result;

	const auto& scene = model.scenes.at(0);

	std::unordered_map<int, std::shared_ptr<skygfx::Texture>> textures_cache;

	auto get_or_create_texture = [&](int index) -> std::shared_ptr<skygfx::Texture> {
		if (index == -1)
			return nullptr;
			
		if (!textures_cache.contains(index))
		{
			const auto& texture = model.textures.at(index);
			const auto& image = model.images.at(texture.source);
			textures_cache[index] = std::make_shared<skygfx::Texture>((uint32_t)image.width, (uint32_t)image.height, 4, (void*)image.image.data(), true);
		}

		return textures_cache.at(index);
	};

	for (auto node_index : scene.nodes)
	{
		const auto& node = model.nodes.at(node_index);

		auto mesh_index = node.mesh;

		const auto& mesh = model.meshes.at(mesh_index);

		for (const auto& primitive : mesh.primitives)
		{
			static const std::unordered_map<int, skygfx::Topology> ModesMap = {
				{ TINYGLTF_MODE_POINTS, skygfx::Topology::PointList },
				{ TINYGLTF_MODE_LINE, skygfx::Topology::LineList },
			//	{ TINYGLTF_MODE_LINE_LOOP, skygfx::Topology:: },
				{ TINYGLTF_MODE_LINE_STRIP, skygfx::Topology::LineStrip },
				{ TINYGLTF_MODE_TRIANGLES, skygfx::Topology::TriangleList },
				{ TINYGLTF_MODE_TRIANGLE_STRIP, skygfx::Topology::TriangleStrip },
			//	{ TINYGLTF_MODE_TRIANGLE_FAN, skygfx::Topology:: } 
			};

			auto topology = ModesMap.at(primitive.mode);

			const static std::unordered_map<int, int> IndexStride = {
				{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT, 2 },
				{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT, 4 },
			};

			/* buffer_view.target is:
				TINYGLTF_TARGET_ARRAY_BUFFER,
				TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER
			*/

			const auto& index_buffer_accessor = model.accessors.at(primitive.indices);
			const auto& index_buffer_view = model.bufferViews.at(index_buffer_accessor.bufferView);
			const auto& index_buffer = model.buffers.at(index_buffer_view.buffer);

			auto index_buf_size = index_buffer_view.byteLength;
			auto index_buf_stride = IndexStride.at(index_buffer_accessor.componentType);
			auto index_buf_data = (void*)((size_t)index_buffer.data.data() + index_buffer_view.byteOffset);
			
			auto index_count = index_buffer_accessor.count;
			auto index_offset = index_buffer_accessor.byteOffset / 2;

			const auto& positions_buffer_accessor = model.accessors.at(primitive.attributes.at("POSITION"));
			const auto& positions_buffer_view = model.bufferViews.at(positions_buffer_accessor.bufferView);
			const auto& positions_buffer = model.buffers.at(positions_buffer_view.buffer);

			const auto& normal_buffer_accessor = model.accessors.at(primitive.attributes.at("NORMAL"));
			const auto& normal_buffer_view = model.bufferViews.at(normal_buffer_accessor.bufferView);
			const auto& normal_buffer = model.buffers.at(normal_buffer_view.buffer);

			const auto& texcoord_buffer_accessor = model.accessors.at(primitive.attributes.at("TEXCOORD_0"));
			const auto& texcoord_buffer_view = model.bufferViews.at(texcoord_buffer_accessor.bufferView);
			const auto& texcoord_buffer = model.buffers.at(texcoord_buffer_view.buffer);

			auto positions_ptr = (glm::vec3*)(((size_t)positions_buffer.data.data()) + positions_buffer_view.byteOffset);
			auto normal_ptr = (glm::vec3*)(((size_t)normal_buffer.data.data()) + normal_buffer_view.byteOffset);
			auto texcoord_ptr = (glm::vec2*)(((size_t)texcoord_buffer.data.data()) + texcoord_buffer_view.byteOffset);

			auto indices = skygfx::utils::Mesh::Indices();

			for (int i = 0; i < index_buffer_accessor.count; i++)
			{
				uint32_t index;

				if (index_buf_stride == 2)
					index = static_cast<uint32_t>(((uint16_t*)index_buf_data)[i]);
				else
					index = ((uint32_t*)index_buf_data)[i];

				indices.push_back(index);
			}

			skygfx::utils::Mesh::Vertices vertices;

			for (int i = 0; i < positions_buffer_accessor.count; i++)
			{
				skygfx::utils::Mesh::Vertex vertex;

				vertex.pos = positions_ptr[i];
				vertex.normal = normal_ptr[i];
				vertex.texcoord = texcoord_ptr[i];
				vertex.color = { 1.0f, 1.0f, 1.0f, 1.0f };

				vertices.push_back(vertex);
			}

			auto mesh = std::make_unique<skygfx::utils::Mesh>();
			mesh->setTopology(topology);
			mesh->setIndices(indices);
			mesh->setVertices(vertices);

			auto draw_command = skygfx::utils::DrawIndexedVerticesCommand{
				.index_count = (uint32_t)index_count,
				.index_offset = (uint32_t)index_offset
			};

			const auto& material = model.materials.at(primitive.material);
			const auto& baseColorTexture = material.pbrMetallicRoughness.baseColorTexture;
			const auto& metallicRoughnessTexture = material.pbrMetallicRoughness.metallicRoughnessTexture;
			const auto& baseColorFactor = material.pbrMetallicRoughness.baseColorFactor;
			const auto& occlusionTexture = material.occlusionTexture;

			auto _material = std::make_shared<Material>();
			_material->color_texture = get_or_create_texture(baseColorTexture.index);
			_material->normal_texture = get_or_create_texture(material.normalTexture.index);
			_material->metallic_roughness_texture = get_or_create_texture(metallicRoughnessTexture.index);
			_material->color = {
				baseColorFactor.at(0),
				baseColorFactor.at(1),
				baseColorFactor.at(2),
				baseColorFactor.at(3)
			};

			result.meshes[_material].push_back({ std::move(mesh), draw_command });
		}
		// TODO: dont forget to draw childrens of node
	}

	return result;
}

void UpdateCamera(GLFWwindow* window, skygfx::utils::PerspectiveCamera& camera)
{
	if (cursor_is_interacting)
	{
		double x = 0.0;
		double y = 0.0;

		glfwGetCursorPos(window, &x, &y);

		auto dx = x - cursor_saved_pos_x;
		auto dy = y - cursor_saved_pos_y;
		
		const auto sensitivity = 0.25f;
	
		dx *= sensitivity;
		dy *= sensitivity;

		camera.yaw += glm::radians(static_cast<float>(dx));
		camera.pitch -= glm::radians(static_cast<float>(dy));

		glfwSetCursorPos(window, cursor_saved_pos_x, cursor_saved_pos_y);
	}

	static auto before = glfwGetTime();
	auto now = glfwGetTime();
	auto dtime = now - before;
	before = now;

	auto speed = (float)dtime * 500.0f;

	if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
		speed *= 3.0f;

	if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
		speed /= 6.0f;

	glm::vec2 direction = { 0.0f, 0.0f };

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		direction.y = 1.0f;

	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		direction.y = -1.0f;

	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		direction.x = -1.0f;

	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		direction.x = 1.0f;

	if (glm::length(direction) > 0.0f)
	{
		direction = glm::normalize(direction);
		direction *= speed;
	}

	auto angles_speed = dtime * 100.0f;
	
	if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
		camera.yaw += glm::radians(static_cast<float>(angles_speed));
	
	if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
		camera.yaw -= glm::radians(static_cast<float>(angles_speed));
	
	if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
		camera.pitch += glm::radians(static_cast<float>(angles_speed));
	
	if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
		camera.pitch -= glm::radians(static_cast<float>(angles_speed));
		
	constexpr auto limit = glm::pi<float>() / 2.0f - 0.01f;

	camera.pitch = fmaxf(-limit, camera.pitch);
	camera.pitch = fminf(+limit, camera.pitch);

	auto pi = glm::pi<float>();

	while (camera.yaw > pi)
		camera.yaw -= pi * 2.0f;

	while (camera.yaw < -pi)
		camera.yaw += pi * 2.0f;

	auto sin_yaw = glm::sin(camera.yaw);
	auto sin_pitch = glm::sin(camera.pitch);

	auto cos_yaw = glm::cos(camera.yaw);
	auto cos_pitch = glm::cos(camera.pitch);

	const float fov = 70.0f;
	const float near_plane = 1.0f;
	const float far_plane = 8192.0f;
	const glm::vec3 world_up = { 0.0f, 1.0f, 0.0f };

	auto front = glm::normalize(glm::vec3(cos_yaw * cos_pitch, sin_pitch, sin_yaw * cos_pitch));
	auto right = glm::normalize(glm::cross(front, world_up));
	//auto up = glm::normalize(glm::cross(right, front));

	if (glm::length(direction) > 0.0f)
	{
		camera.position += front * direction.y;
		camera.position += right * direction.x;
	}
}

void DrawGui(skygfx::utils::PerspectiveCamera& camera)
{
	const int ImGuiWindowFlags_Overlay = ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoBringToFrontOnFocus;

	ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_Overlay & ~ImGuiWindowFlags_NoInputs);
	ImGui::SetWindowPos(ImVec2(10.0f, 10.0f));

	static int fps = 0;
	static int frame_count = 0;
	static double before = 0.0;

	double now = glfwGetTime();
	frame_count++;

	if (now - before >= 1.0)
	{
		fps = frame_count;
		frame_count = 0;
		before = now;
	}

	ImGui::Text("FPS: %d", fps);
	ImGui::Separator();
	ImGui::SliderAngle("Pitch##1", &camera.pitch, -89.0f, 89.0f);
	ImGui::SliderAngle("Yaw##1", &camera.yaw, -180.0f, 180.0f);
	ImGui::DragFloat3("Position##1", (float*)&camera.position);

	ImGui::End();
}

int main()
{
	auto backend_type = utils::ChooseBackendTypeViaConsole();

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "Sponza");

	skygfx::Initialize(native_window, width, height, backend_type);

	glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width, int height) {
		skygfx::Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	});
	
	glfwSetMouseButtonCallback(window, MouseButtonCallback);

	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	std::string err;
	std::string warn;
	auto path = "assets/sponza/sponza.glb";

	bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);

	auto camera = skygfx::utils::PerspectiveCamera();

	auto render_buffer = BuildRenderBuffer(model);

	auto directional_light = skygfx::utils::effects::DirectionalLight();
	directional_light.ambient = { 0.125f, 0.125f, 0.125f };
	directional_light.diffuse = { 0.125f, 0.125f, 0.125f };
	directional_light.specular = { 1.0f, 1.0f, 1.0f };
	directional_light.shininess = 16.0f;
	directional_light.direction = { 0.5f, -1.0f, 0.5f };

	auto base_light = skygfx::utils::effects::PointLight();
	base_light.shininess = 32.0f;
	base_light.constant_attenuation = 0.0f;
	base_light.linear_attenuation = 0.00128f;
	base_light.quadratic_attenuation = 0.0f;
	
	auto red_light = base_light;
	red_light.ambient = { 0.0625f, 0.0f, 0.0f };
	red_light.diffuse = { 0.5f, 0.0f, 0.0f };
	red_light.specular = { 1.0f, 0.0f, 0.0f };

	auto green_light = base_light;
	green_light.ambient = { 0.0f, 0.0625f, 0.0f };
	green_light.diffuse = { 0.0f, 0.5f, 0.0f };
	green_light.specular = { 0.0f, 1.0f, 0.0f };

	auto blue_light = base_light;
	blue_light.ambient = { 0.0f, 0.0f, 0.0625f };
	blue_light.diffuse = { 0.0f, 0.0f, 0.5f };
	blue_light.specular = { 0.0f, 0.0f, 1.0f };

	auto lightblue_light = base_light;
	lightblue_light.ambient = { 0.0f, 0.0625f, 0.0625f };
	lightblue_light.diffuse = { 0.0f, 0.5f, 0.5f };
	lightblue_light.specular = { 0.0f, 1.0f, 1.0f };

	struct MovingLight
	{
		skygfx::utils::effects::PointLight light;
		glm::vec3 begin;
		glm::vec3 end;
		float multiplier = 1.0f;
	};

	std::vector<MovingLight> moving_lights = {
		// first floor
		{ red_light, { 1200.0f, 256.0f, -36.0f }, { -1200.0f, 256.0f, -36.0f }, 4.0f },
		{ green_light, { 1200.0f, 256.0f, -36.0f }, { -1200.0f, 256.0f, -36.0f }, 3.0f },
		{ blue_light, { 1200.0f, 256.0f, -36.0f }, { -1200.0f, 256.0f, -36.0f }, 2.0f },
		
		// second floor
		{ green_light, { 1100.0f, 550.0f, 400.0f }, { 1100.0f, 550.0f, -400.0f }, 1.0f },
		{ red_light, { -1200.0f, 550.0f, -400.0f }, { -1200.0f, 550.0f, 400.0f }, 2.0f },
		{ blue_light, { 1100.0f, 550.0f, 400.0f }, { -1200.0f, 550.0f, 400.0f }, 3.0f },
		{ lightblue_light, { 1100.0f, 550.0f, -400.0f }, { -1200.0f, 550.0f, -400.0f }, 4.0f }
	};

	auto imgui = ImguiHelper();

	ImGui_ImplGlfw_InitForOpenGL(window, true);

	skygfx::utils::Commands draw_cmds;

	for (const auto& [_material, meshes] : render_buffer.meshes)
	{
		skygfx::utils::SetColor(draw_cmds, _material->color);
		skygfx::utils::SetColorTexture(draw_cmds, _material->color_texture.get());
		skygfx::utils::SetNormalTexture(draw_cmds, _material->normal_texture.get());
				
		for (const auto& [mesh, draw_command]: meshes)
		{
			skygfx::utils::SetMesh(draw_cmds, mesh.get());
			skygfx::utils::Draw(draw_cmds, draw_command);
		}
	}

	skygfx::utils::passes::Bloom bloom_pass;

	std::optional<skygfx::RenderTarget> src_target;
	std::optional<skygfx::RenderTarget> dst_target;

	auto ensureTargetSize = [window = window](auto& target) {
		int win_width;
		int win_height;
		glfwGetFramebufferSize(window, &win_width, &win_height);

		if (!target.has_value() || target.value().getWidth() != win_width || target.value().getHeight() != win_height)
			target.emplace(win_width, win_height);
	};

	while (!glfwWindowShouldClose(window))
	{
		ensureTargetSize(src_target);
		ensureTargetSize(dst_target);

		ImGui_ImplGlfw_NewFrame();

		ImGui::NewFrame();

		DrawGui(camera);

		UpdateCamera(window, camera);

		auto time = (float)glfwGetTime();

		std::vector<skygfx::utils::effects::PointLight> point_lights;

		for (auto& moving_light : moving_lights)
		{
			moving_light.light.position = glm::lerp(moving_light.begin, moving_light.end, (glm::sin(time / moving_light.multiplier) + 1.0f) * 0.5f);
			point_lights.push_back(moving_light.light);
		}

		skygfx::SetRenderTarget(src_target.value());
		skygfx::Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });

		skygfx::SetDepthMode(skygfx::DepthMode{ skygfx::ComparisonFunc::LessEqual });
		skygfx::SetCullMode(skygfx::CullMode::Front);
		skygfx::SetSampler(skygfx::Sampler::Linear);
		skygfx::SetTextureAddress(skygfx::TextureAddress::Wrap);

		skygfx::utils::Commands cmds;
		skygfx::utils::SetCamera(cmds, camera);

		auto draw_meshes = [&](const auto& light){
			skygfx::utils::SetEffect(cmds, light);
			skygfx::utils::InsertSubcommands(cmds, &draw_cmds);
		};

		skygfx::utils::Callback(cmds, [] {
			skygfx::SetBlendMode(skygfx::BlendStates::Opaque);
		});
		
		draw_meshes(directional_light);

		skygfx::utils::Callback(cmds, [] {
			skygfx::SetBlendMode(skygfx::BlendStates::Additive);
		});

		for (const auto& point_light : point_lights)
		{
			draw_meshes(point_light);
		}

		skygfx::utils::ExecuteCommands(cmds);

		skygfx::SetBlendMode(skygfx::BlendStates::NonPremultiplied);
		skygfx::SetDepthMode(std::nullopt);
		skygfx::SetCullMode(skygfx::CullMode::None);

		skygfx::SetRenderTarget(dst_target.value());
		skygfx::Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });

		bloom_pass.execute(src_target.value(), dst_target.value());

		skygfx::SetRenderTarget(std::nullopt);

		skygfx::utils::ExecuteCommands({
			skygfx::utils::commands::SetColorTexture{ &dst_target.value() },
			skygfx::utils::commands::Draw{}
		});

		imgui.draw();

		skygfx::Present();

		glfwPollEvents();
	}
	
	ImGui_ImplGlfw_Shutdown();
	
	skygfx::Finalize();

	glfwTerminate();

	return 0;
}
