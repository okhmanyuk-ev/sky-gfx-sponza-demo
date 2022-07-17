#include <iostream>

#include <skygfx/skygfx.h>
#include "../lib/sky-gfx/examples/utils/utils.h"
#include <tiny_gltf.h>

static std::string vertex_shader_code = R"(
#version 450 core

layout(location = POSITION_LOCATION) in vec3 aPosition;
layout(location = COLOR_LOCATION) in vec4 aColor;

layout(location = 0) out struct { vec4 Color; } Out;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
	Out.Color = aColor;
	gl_Position = vec4(aPosition, 1.0);
})";

static std::string fragment_shader_code = R"(
#version 450 core

layout(location = 0) out vec4 result;
layout(location = 0) in struct { vec4 Color; } In;

void main() 
{ 
	result = In.Color;
})";

using Vertex = skygfx::Vertex::PositionColor;

static std::vector<Vertex> vertices = {
	{ {  0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
	{ { -0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
	{ {  0.0f,  0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
};

static std::vector<uint32_t> indices = { 0, 1, 2 };

int main()
{
	auto backend_type = utils::ChooseBackendTypeViaConsole();

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

	auto native_window = utils::GetNativeWindow(window);

	auto device = skygfx::Device(backend_type, native_window, width, height);
	auto shader = skygfx::Shader(Vertex::Layout, vertex_shader_code, fragment_shader_code);

	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	std::string err;
	std::string warn;
	auto path = "assets/sponza/sponza.glb";

	bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);

	while (!glfwWindowShouldClose(window))
	{
		device.clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
		device.setTopology(skygfx::Topology::TriangleList);
		device.setShader(shader);
		device.setVertexBuffer(vertices);
		device.setIndexBuffer(indices);
		device.drawIndexed(static_cast<uint32_t>(indices.size()));

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

				const auto& accessor = model.accessors.at(primitive.indices);

				auto index_count = accessor.count;
				auto index_type = accessor.componentType;
				auto index_offset = 0;



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