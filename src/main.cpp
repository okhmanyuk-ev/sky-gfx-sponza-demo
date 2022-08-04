#include <iostream>
#include <unordered_map>
#include <skygfx/skygfx.h>
#include "../lib/sky-gfx/examples/utils/utils.h"
#include <tiny_gltf.h>
#include <imgui.h>
#include "imgui_helper.h"
#include "forward_rendering.h"

using Vertex = skygfx::Vertex::PositionTextureNormal;

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

static std::function<void(uint32_t, uint32_t)> resize_func = nullptr;

void WindowSizeCallback(GLFWwindow* window, int width, int height)
{
	resize_func((uint32_t)width, (uint32_t)height);
}

struct TextureBundle
{
	std::shared_ptr<skygfx::Texture> color_texture;
	std::shared_ptr<skygfx::Texture> normal_texture;
};

struct RenderBuffer
{
	struct Batch
	{
		skygfx::Topology topology;
		std::vector<Vertex> vertices;
		skygfx::Buffer index_buffer;
		uint32_t index_count = 0;
		uint32_t index_offset = 0;
	};

	std::unordered_map<std::shared_ptr<TextureBundle>, std::vector<Batch>> batches;
};

RenderBuffer BuildRenderBuffer(const tinygltf::Model& model)
{
	// https://github.com/syoyo/tinygltf/blob/master/examples/glview/glview.cc
	// https://github.com/syoyo/tinygltf/blob/master/examples/basic/main.cpp

	RenderBuffer result;

	const auto& scene = model.scenes.at(0);

	std::unordered_map<int, std::shared_ptr<skygfx::Texture>> textures_cache;

	auto get_or_create_texture = [&](int index) {
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

			skygfx::Buffer index_buf;
			index_buf.size = index_buffer_view.byteLength;
			index_buf.stride = IndexStride.at(index_buffer_accessor.componentType);
			index_buf.data = (void*)((size_t)index_buffer.data.data() + index_buffer_view.byteOffset);
			
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

			RenderBuffer::Batch batch;
			batch.topology = topology;
			batch.index_buffer = index_buf;

			for (int i = 0; i < positions_buffer_accessor.count; i++)
			{
				Vertex vertex;

				vertex.pos = positions_ptr[i];
				vertex.normal = normal_ptr[i];
				vertex.texcoord = texcoord_ptr[i];

				batch.vertices.push_back(vertex);
			}

			batch.index_count = (uint32_t)index_count;
			batch.index_offset = (uint32_t)index_offset;

			const auto& material = model.materials.at(primitive.material);
			const auto& baseColorTexture = material.pbrMetallicRoughness.baseColorTexture;

			if (baseColorTexture.index == -1)
				continue;

			auto texture_bundle = std::make_shared<TextureBundle>();
			texture_bundle->color_texture = get_or_create_texture(baseColorTexture.index);
			texture_bundle->normal_texture = get_or_create_texture(material.normalTexture.index);

			result.batches[texture_bundle].push_back(batch);
		}
		// TODO: dont forget to draw childrens of node
	}

	return result;
}

struct Camera
{
	glm::vec3 position = { 0.0f, 0.0f, 0.0f };
	float yaw = 0.0f;
	float pitch = 0.0f;
};

std::tuple<glm::mat4, glm::mat4> UpdateCamera(GLFWwindow* window, Camera& camera, uint32_t width, uint32_t height)
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

		constexpr auto limit = glm::pi<float>() / 2.0f - 0.01f;

		camera.pitch = fmaxf(-limit, camera.pitch);
		camera.pitch = fminf(+limit, camera.pitch);

		auto pi = glm::pi<float>();

		while (camera.yaw > pi)
			camera.yaw -= pi * 2.0f;

		while (camera.yaw < -pi)
			camera.yaw += pi * 2.0f;

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
	auto up = glm::normalize(glm::cross(right, front));

	if (glm::length(direction) > 0.0f)
	{
		camera.position += front * direction.y;
		camera.position += right * direction.x;
	}

	auto view = glm::lookAtRH(camera.position, camera.position + front, up);
	auto projection = glm::perspectiveFov(fov, (float)width, (float)height, near_plane, far_plane);

	return { view, projection };
}

void DrawGeometry(skygfx::Device& device, const RenderBuffer& render_buffer,
	uint32_t color_texture_binding, uint32_t normal_texture_binding)
{
	for (const auto& [texture_bundle, batches] : render_buffer.batches)
	{
		device.setTexture(color_texture_binding, *texture_bundle->color_texture);
		device.setTexture(normal_texture_binding, *texture_bundle->normal_texture);

		for (const auto& batch : batches)
		{
			device.setTopology(batch.topology);
			device.setIndexBuffer(batch.index_buffer);
			device.setVertexBuffer(batch.vertices);
			device.drawIndexed(batch.index_count, batch.index_offset);
		}
	}
}

void DrawGui(Camera& camera)
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

	uint32_t width = 800;
	uint32_t height = 600;

	auto window = glfwCreateWindow(width, height, "sponza", NULL, NULL);

	int count = 0;
	auto monitors = glfwGetMonitors(&count);

	auto video_mode = glfwGetVideoMode(monitors[0]);

	auto window_pos_x = (video_mode->width / 2) - (width / 2);
	auto window_pos_y = (video_mode->height / 2) - (height / 2);

	glfwSetWindowPos(window, window_pos_x, window_pos_y);
	glfwMakeContextCurrent(window);

	glfwSetMouseButtonCallback(window, MouseButtonCallback);
	glfwSetWindowSizeCallback(window, WindowSizeCallback);

	auto native_window = utils::GetNativeWindow(window);

	auto device = std::make_shared<skygfx::Device>(backend_type, native_window, width, height);
	
	resize_func = [&](uint32_t _width, uint32_t _height) {
		width = _width;
		height = _height;
		device->resize(_width, _height);
	};

	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	std::string err;
	std::string warn;
	auto path = "assets/sponza/sponza.glb";

	bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);

	Camera camera;

	auto render_buffer = BuildRenderBuffer(model);

	auto matrices = ForwardRendering::Matrices();

	auto directional_light = ForwardRendering::DirectionalLight();
	directional_light.ambient = { 0.125f, 0.125f, 0.125f };
	directional_light.diffuse = { 0.125f, 0.125f, 0.125f };
	directional_light.specular = { 1.0f, 1.0f, 1.0f };
	directional_light.shininess = 16.0f;
	directional_light.direction = { 0.5f, -1.0f, 0.5f };

	auto point_light = ForwardRendering::PointLight();
	point_light.ambient = { 0.0625f, 0.0625f, 0.0625f };
	point_light.diffuse = { 0.5f, 0.5f, 0.5f };
	point_light.specular = { 1.0f, 1.0f, 1.0f };
	point_light.shininess = 32.0f;
	point_light.constant_attenuation = 0.0f;
	point_light.linear_attenuation = 0.00128f;
	point_light.quadratic_attenuation = 0.0f;
	point_light.position = { 0.0f, 256.0f, -36.0f };

	//const auto point_light_start_x = -1200.0f;
	//const auto point_light_end_x = 1200.0f;

	auto imgui = ImguiHelper(window);
	auto forward_rendering = ForwardRendering(device, Vertex::Layout);

	auto draw_geometry_func = [&render_buffer](auto& device, auto color_texture_binding, auto normal_texture_binding) {
		DrawGeometry(device, render_buffer, color_texture_binding, normal_texture_binding);
	};

	while (!glfwWindowShouldClose(window))
	{
		imgui.newFrame();

		std::tie(matrices.view, matrices.projection) = UpdateCamera(window, camera, width, height);

		matrices.eye_position = camera.position;

		auto time = (float)glfwGetTime();

		point_light.position.x = glm::cos(time / 4.0f) * 1200.0f;
		
		forward_rendering.Draw(draw_geometry_func, matrices, directional_light, { point_light });

		DrawGui(camera);

		imgui.draw(*device);

		device->present();

		glfwPollEvents();
	}

	glfwTerminate();

	return 0;
}
