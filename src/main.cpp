#include <iostream>

#include <skygfx/skygfx.h>
#include "../lib/sky-gfx/examples/utils/utils.h"
#include <tiny_gltf.h>
#include <magic_enum.hpp>

enum class Bindings {
	COLOR_TEXTURE_BINDING,
	NORMAL_TEXTURE_BINDING,
	MATRICES_UBO_BINDING,
	LIGHT_UBO_BINDING
};

static std::string vertex_shader_code = R"(
#version 450 core

layout(location = POSITION_LOCATION) in vec3 aPosition;
layout(location = NORMAL_LOCATION) in vec3 aNormal;
layout(location = TEXCOORD_LOCATION) in vec2 aTexCoord;

layout(location = 0) out struct { vec3 Position; vec3 Normal; vec2 TexCoord; } Out;
out gl_PerVertex { vec4 gl_Position; };

layout(binding = MATRICES_UBO_BINDING) uniform _ubo
{
	mat4 projection;
	mat4 view;
	mat4 model;
} ubo;

void main()
{
	Out.Position = vec3(ubo.model * vec4(aPosition, 1.0));
	Out.Normal = vec3(ubo.model * vec4(aNormal, 1.0));
	Out.TexCoord = aTexCoord;
#ifdef FLIP_TEXCOORD_Y
	Out.TexCoord.y = 1.0 - Out.TexCoord.y;
#endif
	gl_Position = ubo.projection * ubo.view * ubo.model * vec4(aPosition, 1.0);
})";

static std::string fragment_shader_code = R"(
#version 450 core

layout(binding = LIGHT_UBO_BINDING) uniform _light
{
	vec3 direction;
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
	vec3 eye_position;
	float shininess;
} light;

layout(location = 0) out vec4 result;
layout(location = 0) in struct { vec3 Position; vec3 Normal; vec2 TexCoord; } In;

layout(binding = COLOR_TEXTURE_BINDING) uniform sampler2D sColorTexture;
layout(binding = NORMAL_TEXTURE_BINDING) uniform sampler2D sNormalTexture;

void main() 
{ 
	result = texture(sColorTexture, In.TexCoord);

	vec3 normal = (In.Normal * vec3(texture(sNormalTexture, In.TexCoord)));

	vec3 view_dir = normalize(light.eye_position - In.Position);
	vec3 light_dir = normalize(light.direction);

	float diff = max(dot(normal, -light_dir), 0.0);
	vec3 reflect_dir = reflect(light_dir, normal);
	float spec = pow(max(dot(view_dir, reflect_dir), 0.0), light.shininess);

	vec3 intensity = light.ambient + (light.diffuse * diff) + (light.specular * spec);

	result *= vec4(intensity, 1.0);
})";

using Vertex = skygfx::Vertex::PositionTextureNormal;

static struct alignas(16) UniformBuffer
{
	glm::mat4 projection = glm::mat4(1.0f);
	glm::mat4 view = glm::mat4(1.0f);
	glm::mat4 model = glm::mat4(1.0f);
} ubo;

static struct alignas(16) Light
{
	alignas(16) glm::vec3 direction;
	alignas(16) glm::vec3 ambient;
	alignas(16) glm::vec3 diffuse;
	alignas(16) glm::vec3 specular;
	alignas(16) glm::vec3 eye_position;
	float shininess;
} light;

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

static std::function<void(uint32_t, uint32_t)> resize_func = nullptr;

void WindowSizeCallback(GLFWwindow* window, int width, int height)
{
	resize_func((uint32_t)width, (uint32_t)height);
}

template<class T>
std::vector<std::string> MakeBindingDefines()
{
	static_assert(std::is_enum<T>());

	std::vector<std::string> result;
	for (auto enum_field : magic_enum::enum_values<T>())
	{
		auto name = magic_enum::enum_name(enum_field);
		auto value = std::to_string(static_cast<int>(enum_field));
		auto combined = std::string(name) + " " + value;
		result.push_back(combined);
	}
	return result;
}

template<class T>
uint32_t GetBinding(T value)
{
	static_assert(std::is_enum<T>());
	return static_cast<int>(value);
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
			/*static const std::unordered_map<int, skygfx::Topology> ModesMap = {
				{ TINYGLTF_MODE_POINTS, skygfx::Topology::PointList },
				{ TINYGLTF_MODE_LINE, skygfx::Topology::LineList },
			//	{ TINYGLTF_MODE_LINE_LOOP, skygfx::Topology:: },
				{ TINYGLTF_MODE_LINE_STRIP, skygfx::Topology::LineStrip },
				{ TINYGLTF_MODE_TRIANGLES, skygfx::Topology::TriangleList },
				{ TINYGLTF_MODE_TRIANGLE_STRIP, skygfx::Topology::TriangleStrip },
			//	{ TINYGLTF_MODE_TRIANGLE_FAN, skygfx::Topology:: } 
			};

			auto topology = ModesMap.at(primitive.mode);
			device.setTopology(topology);*/

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

void DrawRenderBuffer(const RenderBuffer& render_buffer, skygfx::Device& device)
{
	device.setTopology(skygfx::Topology::TriangleList);
	device.setDepthMode(skygfx::DepthMode{ skygfx::ComparisonFunc::Less });
	device.setCullMode(skygfx::CullMode::Front);
	device.setTextureAddressMode(skygfx::TextureAddress::Wrap);

	for (const auto& [texture_bundle, batches] : render_buffer.batches)
	{
		device.setTexture(GetBinding(Bindings::COLOR_TEXTURE_BINDING), *texture_bundle->color_texture);
		device.setTexture(GetBinding(Bindings::NORMAL_TEXTURE_BINDING), *texture_bundle->normal_texture);

		for (const auto& batch : batches)
		{
			device.setIndexBuffer(batch.index_buffer);
			device.setVertexBuffer(batch.vertices);
			device.drawIndexed(batch.index_count, batch.index_offset);
		}
	}
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

	auto device = skygfx::Device(backend_type, native_window, width, height);
	auto shader = skygfx::Shader(Vertex::Layout, vertex_shader_code, fragment_shader_code, MakeBindingDefines<Bindings>());

	resize_func = [&](uint32_t _width, uint32_t _height) {
		width = _width;
		height = _height;
		device.resize(_width, _height);
	};

	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	std::string err;
	std::string warn;
	auto path = "assets/sponza/sponza.glb";

	bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);

	Camera camera;

	auto render_buffer = BuildRenderBuffer(model);

	light.ambient = { 0.25f, 0.25f, 0.25f };
	light.diffuse = { 1.0f, 1.0f, 1.0f };
	light.specular = { 1.0f, 1.0f, 1.0f };
	light.shininess = 32.0f;

	while (!glfwWindowShouldClose(window))
	{
		std::tie(ubo.view, ubo.projection) = UpdateCamera(window, camera, width, height);

		auto time = (float)glfwGetTime() / 2.0f;

		light.direction.x = glm::cos(time);
		light.direction.y = 0.5f;
		light.direction.z = glm::sin(time);

		light.eye_position = camera.position;

		device.clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
		device.setShader(shader);
		device.setUniformBuffer(GetBinding(Bindings::MATRICES_UBO_BINDING), ubo);
		device.setUniformBuffer(GetBinding(Bindings::LIGHT_UBO_BINDING), light);
		
		DrawRenderBuffer(render_buffer, device);

		device.present();

		glfwPollEvents();
	}

	glfwTerminate();

	return 0;
}