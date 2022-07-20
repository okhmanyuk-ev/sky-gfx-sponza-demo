#include <iostream>

#include <skygfx/skygfx.h>
#include "../lib/sky-gfx/examples/utils/utils.h"
#include <tiny_gltf.h>

static std::string vertex_shader_code = R"(
#version 450 core

layout(location = POSITION_LOCATION) in vec3 aPosition;

layout(location = 0) out struct { vec4 Color; } Out;
out gl_PerVertex { vec4 gl_Position; };

layout(binding = 1) uniform _ubo
{
	mat4 projection;
	mat4 view;
	mat4 model;
} ubo;

void main()
{
	Out.Color = normalize(vec4(aPosition, 1.0));
	gl_Position = ubo.projection * ubo.view * ubo.model * vec4(aPosition, 1.0);
})";

static std::string fragment_shader_code = R"(
#version 450 core

layout(location = 0) out vec4 result;
layout(location = 0) in struct { vec4 Color; } In;

void main() 
{ 
	result = In.Color;
})";

using Vertex = skygfx::Vertex::Position;

static struct alignas(16) UniformBuffer
{
	glm::mat4 projection = glm::mat4(1.0f);
	glm::mat4 view = glm::mat4(1.0f);
	glm::mat4 model = glm::mat4(1.0f);
} ubo;

static double cursor_saved_pos_x = 0.0;
static double cursor_saved_pos_y = 0.0;
static bool cursor_is_interacting = false;

void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT)
	{
		if (action == GLFW_PRESS)
		{
			cursor_is_interacting = true;
			glfwGetCursorPos(window, &cursor_saved_pos_x, &cursor_saved_pos_y);
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
		}
		else if (action == GLFW_RELEASE)
		{
			cursor_is_interacting = false;
			glfwSetCursorPos(window, cursor_saved_pos_x, cursor_saved_pos_y);
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}
}

int main()
{
	//auto backend_type = utils::ChooseBackendTypeViaConsole();
	auto backend_type = skygfx::BackendType::D3D11;

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	uint32_t width = 800;
	uint32_t height = 600;

	auto window = glfwCreateWindow(width, height, "sponza demo", NULL, NULL);

	int count = 0;
	auto monitors = glfwGetMonitors(&count);

	auto video_mode = glfwGetVideoMode(monitors[0]);

	auto window_pos_x = (video_mode->width / 2) - (width / 2);
	auto window_pos_y = (video_mode->height / 2) - (height / 2);

	glfwSetWindowPos(window, window_pos_x, window_pos_y);
	glfwMakeContextCurrent(window);

	glfwSetMouseButtonCallback(window, MouseButtonCallback);

	auto native_window = utils::GetNativeWindow(window);

	auto device = skygfx::Device(backend_type, native_window, width, height);
	auto shader = skygfx::Shader(Vertex::Layout, vertex_shader_code, fragment_shader_code);

	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	std::string err;
	std::string warn;
	auto path = "assets/sponza/sponza.glb";

	bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);

	auto yaw = 0.0f;
	auto pitch = 0.0f;
	glm::vec3 position = { 0.0f, 0.0f, 0.0f };

	while (!glfwWindowShouldClose(window))
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

			yaw += glm::radians(static_cast<float>(dx));
			pitch -= glm::radians(static_cast<float>(dy));

			constexpr auto limit = glm::pi<float>() / 2.0f - 0.01f;

			pitch = fmaxf(-limit, pitch);
			pitch = fminf(+limit, pitch);

			auto pi = glm::pi<float>();

			while (yaw > pi)
				yaw -= pi * 2.0f;

			while (yaw < -pi)
				yaw += pi * 2.0f;

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
			speed /= 3.0f;

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

		auto sin_yaw = glm::sin(yaw);
		auto sin_pitch = glm::sin(pitch);

		auto cos_yaw = glm::cos(yaw);
		auto cos_pitch = glm::cos(pitch);

		const float fov = 70.0f;
		const float near_plane = 1.0f;
		const float far_plane = 8192.0f;
		const glm::vec3 world_up = { 0.0f, 1.0f, 0.0f };

		auto front = glm::normalize(glm::vec3(cos_yaw * cos_pitch, sin_pitch, sin_yaw * cos_pitch));
		auto right = glm::normalize(glm::cross(front, world_up));
		auto up = glm::normalize(glm::cross(right, front));

		if (glm::length(direction) > 0.0f)
		{
			position += front * direction.y;
			position += right * direction.x;
		}

		ubo.view = glm::lookAtRH(position, position + front, up);
		ubo.projection = glm::perspectiveFov(fov, (float)width, (float)height, near_plane, far_plane);

		device.clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
		device.setTopology(skygfx::Topology::TriangleList);
		device.setShader(shader);
		device.setUniformBuffer(1, ubo);
		device.setDepthMode(skygfx::DepthMode{ skygfx::ComparisonFunc::Less });

		// https://github.com/syoyo/tinygltf/blob/master/examples/glview/glview.cc
		// https://github.com/syoyo/tinygltf/blob/master/examples/basic/main.cpp

		const auto& scene = model.scenes.at(0);

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

				device.setTopology(topology);


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
				device.setIndexBuffer(index_buf);

				auto index_count = index_buffer_accessor.count;
				auto index_offset = index_buffer_accessor.byteOffset / 2;

				const auto& positions_buffer_accessor = model.accessors.at(primitive.attributes.at("POSITION"));
				const auto& positions_buffer_view = model.bufferViews.at(positions_buffer_accessor.bufferView);
				const auto& positions_buffer = model.buffers.at(positions_buffer_view.buffer);
				
				auto stride = positions_buffer_accessor.ByteStride(positions_buffer_view);
				
				skygfx::Buffer vertex_buffer;
				vertex_buffer.size = positions_buffer_view.byteLength;
				vertex_buffer.stride = stride;
				vertex_buffer.data = (void*)(((size_t)positions_buffer.data.data()) + positions_buffer_view.byteOffset);
				device.setVertexBuffer(vertex_buffer);

				//device.setTopology(skygfx::Topology::LineList);
				device.drawIndexed(index_count, index_offset);
			}
			// TODO: dont forget to draw childrens of node
		}


		device.present();

		glfwPollEvents();
	}

	glfwTerminate();
	return 0;
}