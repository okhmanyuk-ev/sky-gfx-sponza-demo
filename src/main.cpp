#include <iostream>

#include <skygfx/skygfx.h>
#include "../lib/sky-gfx/examples/utils/utils.h"
#include <tiny_gltf.h>
#include <magic_enum.hpp>
#include <imgui.h>
#include "imgui_impl_glfw.h"

enum class DirectionalLightBinding : uint32_t {
	COLOR_TEXTURE_BINDING,
	NORMAL_TEXTURE_BINDING,
	MATRICES_UNIFORM_BINDING,
	DIRECTIONAL_LIGHT_UNIFORM_BINDING
};

enum class PointLightBinding : uint32_t {
	COLOR_TEXTURE_BINDING,
	NORMAL_TEXTURE_BINDING,
	MATRICES_UNIFORM_BINDING,
	POINT_LIGHT_UNIFORM_BINDING
};

static const std::string common_vertex_shader_code = R"(
#version 450 core

layout(location = POSITION_LOCATION) in vec3 aPosition;
layout(location = NORMAL_LOCATION) in vec3 aNormal;
layout(location = TEXCOORD_LOCATION) in vec2 aTexCoord;

layout(binding = MATRICES_UNIFORM_BINDING) uniform _matrices
{
	mat4 projection;
	mat4 view;
	mat4 model;
	vec3 eye_position;
} matrices;

layout(location = 0) out struct {
	vec3 frag_position;
	vec3 eye_position;
	vec3 normal;
	vec2 tex_coord;
} Out;

out gl_PerVertex { vec4 gl_Position; };

void main()
{
	Out.frag_position = vec3(matrices.model * vec4(aPosition, 1.0));
	Out.eye_position = matrices.eye_position;
	Out.normal = vec3(matrices.model * vec4(aNormal, 1.0));
	Out.tex_coord = aTexCoord;
#ifdef FLIP_TEXCOORD_Y
	Out.tex_coord.y = 1.0 - Out.tex_coord.y;
#endif
	gl_Position = matrices.projection * matrices.view * matrices.model * vec4(aPosition, 1.0);
})";

static const std::string directional_light_fragment_shader_code = R"(
#version 450 core

layout(binding = DIRECTIONAL_LIGHT_UNIFORM_BINDING) uniform _light
{
	vec3 direction;
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
	float shininess;
} light;

layout(location = 0) in struct {
	vec3 frag_position;
	vec3 eye_position;
	vec3 normal;
	vec2 tex_coord;
} In;

layout(location = 0) out vec4 result;

layout(binding = COLOR_TEXTURE_BINDING) uniform sampler2D sColorTexture;
layout(binding = NORMAL_TEXTURE_BINDING) uniform sampler2D sNormalTexture;

void main()
{
	result = texture(sColorTexture, In.tex_coord);

	vec3 normal = normalize(In.normal * vec3(texture(sNormalTexture, In.tex_coord)));

	vec3 view_dir = normalize(In.eye_position - In.frag_position);
	vec3 light_dir = normalize(light.direction);

	float diff = max(dot(normal, -light_dir), 0.0);
	vec3 reflect_dir = reflect(light_dir, normal);
	float spec = pow(max(dot(view_dir, reflect_dir), 0.0), light.shininess);

	vec3 intensity = light.ambient + (light.diffuse * diff) + (light.specular * spec);

	result *= vec4(intensity, 1.0);
})";

static const std::string point_light_fragment_shader_code = R"(
#version 450 core

layout(binding = POINT_LIGHT_UNIFORM_BINDING) uniform _light
{
	vec3 position;
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
	float constant_attenuation;
	float linear_attenuation;
	float quadratic_attenuation;
	float shininess;
} light;

layout(location = 0) in struct {
	vec3 frag_position;
	vec3 eye_position;
	vec3 normal;
	vec2 tex_coord;
} In;

layout(location = 0) out vec4 result;

layout(binding = COLOR_TEXTURE_BINDING) uniform sampler2D sColorTexture;
layout(binding = NORMAL_TEXTURE_BINDING) uniform sampler2D sNormalTexture;

void main()
{ 
	result = texture(sColorTexture, In.tex_coord);

	vec3 normal = normalize(In.normal * vec3(texture(sNormalTexture, In.tex_coord)));

	vec3 light_offset = light.position - In.frag_position;

	float distance = length(light_offset);
	float linear_attn = light.linear_attenuation * distance;
	float quadratic_attn = light.quadratic_attenuation * (distance * distance);
	float attenuation = 1.0 / (light.constant_attenuation + linear_attn + quadratic_attn);

	vec3 light_dir = normalize(light_offset);
	float diff = max(dot(normal, light_dir), 0.0);
	vec3 reflect_dir = reflect(-light_dir, normal);
	vec3 view_dir = normalize(In.eye_position - In.frag_position);
	float spec = pow(max(dot(view_dir, reflect_dir), 0.0), light.shininess);

	vec3 intensity = light.ambient + (light.diffuse * diff) + (light.specular * spec);

	intensity *= attenuation;

	result *= vec4(intensity, 1.0);	
})";

using Vertex = skygfx::Vertex::PositionTextureNormal;

struct alignas(16) Matrices
{
	glm::mat4 projection = glm::mat4(1.0f);
	glm::mat4 view = glm::mat4(1.0f);
	glm::mat4 model = glm::mat4(1.0f);
	alignas(16) glm::vec3 eye_position = { 0.0f, 0.0f, 0.0f };
};

struct alignas(16) DirectionalLight
{
	alignas(16) glm::vec3 direction = { 0.0f, 0.0f, 0.0f };
	alignas(16) glm::vec3 ambient = { 0.0f, 0.0f, 0.0f };
	alignas(16) glm::vec3 diffuse = { 0.0f, 0.0f, 0.0f };
	alignas(16) glm::vec3 specular = { 0.0f, 0.0f, 0.0f };
	float shininess = 0.0f; // TODO: only material has shininess
};

struct alignas(16) PointLight
{
	alignas(16) glm::vec3 position = { 0.0f, 0.0f, 0.0f };
	alignas(16) glm::vec3 ambient = { 0.0f, 0.0f, 0.0f };
	alignas(16) glm::vec3 diffuse = { 0.0f, 0.0f, 0.0f };
	alignas(16) glm::vec3 specular = { 0.0f, 0.0f, 0.0f };
	float constant_attenuation = 0.0f;
	float linear_attenuation = 0.0f;
	float quadratic_attenuation = 0.0f;
	float shininess = 0.0f; // TODO: only material has shininess
};

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
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
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

template<class E>
std::vector<std::string> MakeBindingDefines()
{
	static_assert(std::is_enum<E>());

	std::vector<std::string> result;
	for (auto enum_field : magic_enum::enum_values<E>())
	{
		auto name = magic_enum::enum_name(enum_field);
		auto value = std::to_string(static_cast<int>(enum_field));
		auto combined = std::string(name) + " " + value;
		result.push_back(combined);
	}
	return result;
}

template<class E>
constexpr auto GetBinding(E e)
{
	return static_cast<std::underlying_type_t<E>>(e);
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

void DrawRenderBuffer(skygfx::Device& device, const RenderBuffer& render_buffer,
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

// TODO: pass device as const ref
void RenderDirectionalLight(skygfx::Device& device, const RenderBuffer& render_buffer, 
	const Matrices& matrices, const DirectionalLight& light)
{
	// TODO: do not use static
	static auto directional_light_shader = skygfx::Shader(Vertex::Layout, common_vertex_shader_code,
		directional_light_fragment_shader_code, MakeBindingDefines<DirectionalLightBinding>());

	device.setShader(directional_light_shader);
	device.setUniformBuffer(GetBinding(DirectionalLightBinding::MATRICES_UNIFORM_BINDING), matrices);
	device.setUniformBuffer(GetBinding(DirectionalLightBinding::DIRECTIONAL_LIGHT_UNIFORM_BINDING), light);

	constexpr auto color_texture_binding = GetBinding(DirectionalLightBinding::COLOR_TEXTURE_BINDING);
	constexpr auto normal_texture_binding = GetBinding(DirectionalLightBinding::NORMAL_TEXTURE_BINDING);

	DrawRenderBuffer(device, render_buffer, color_texture_binding, normal_texture_binding);
}

// TODO: pass device as const ref
void RenderPointLight(skygfx::Device& device, const RenderBuffer& render_buffer,
	const Matrices& matrices, const PointLight& light)
{
	// TODO: do not use static
	static auto point_light_shader = skygfx::Shader(Vertex::Layout, common_vertex_shader_code,
		point_light_fragment_shader_code, MakeBindingDefines<PointLightBinding>());

	device.setShader(point_light_shader);
	device.setUniformBuffer(GetBinding(PointLightBinding::MATRICES_UNIFORM_BINDING), matrices);
	device.setUniformBuffer(GetBinding(PointLightBinding::POINT_LIGHT_UNIFORM_BINDING), light);

	constexpr auto color_texture_binding = GetBinding(PointLightBinding::COLOR_TEXTURE_BINDING);
	constexpr auto normal_texture_binding = GetBinding(PointLightBinding::NORMAL_TEXTURE_BINDING);

	DrawRenderBuffer(device, render_buffer, color_texture_binding, normal_texture_binding);
}

void ForwardRendering(skygfx::Device& device, const RenderBuffer& render_buffer,
	const Matrices& matrices, const DirectionalLight& directional_light, 
	const std::vector<PointLight>& point_lights)
{
	device.clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });

	device.setDepthMode(skygfx::DepthMode{ skygfx::ComparisonFunc::LessEqual });
	device.setCullMode(skygfx::CullMode::Front);
	device.setSampler(skygfx::Sampler::Linear);
	device.setTextureAddressMode(skygfx::TextureAddress::Wrap);

	device.setBlendMode(skygfx::BlendStates::Opaque);

	RenderDirectionalLight(device, render_buffer, matrices, directional_light);

	device.setBlendMode(skygfx::BlendStates::Additive);

	for (const auto& point_light : point_lights)
	{
		RenderPointLight(device, render_buffer, matrices, point_light);
	}
}

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

class ImguiHelper
{
public:
	ImguiHelper(GLFWwindow* window) : mGlfwWindow(window)
	{
		ImGui::CreateContext();
		ImGui::StyleColorsClassic();

		ImGui_ImplGlfw_InitForOpenGL(window, true);

		auto& io = ImGui::GetIO();

		io.IniFilename = NULL;

		if (io.Fonts->IsBuilt())
			return;

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

	~ImguiHelper()
	{
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	// TODO: pass device as const ref
	void draw(skygfx::Device& device)
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

		int w, h;
		glfwGetWindowSize(mGlfwWindow, &w, &h);

		matrices.projection = glm::orthoLH(0.0f, (float)w, (float)h, 0.0f, -1.0f, 1.0f);
		matrices.view = glm::lookAtLH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));

		device.setUniformBuffer(1, matrices);

		//GRAPHICS->pushOrthoMatrix(getLogicalWidth(), getLogicalHeight());

		auto draw_data = ImGui::GetDrawData();
		//drawData->ScaleClipRects({ PLATFORM->getScale() * getScale(), PLATFORM->getScale() * getScale() });

		for (int i = 0; i < draw_data->CmdListsCount; i++)
		{
			const auto cmds = draw_data->CmdLists[i];

			device.setVertexBuffer(skygfx::Buffer{ cmds->VtxBuffer.Data, static_cast<size_t>(cmds->VtxBuffer.size()) });
			device.setIndexBuffer(skygfx::Buffer{ cmds->IdxBuffer.Data, static_cast<size_t>(cmds->IdxBuffer.size()) });
			
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

	void newFrame()
	{
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}

private:
	std::shared_ptr<skygfx::Texture> mFontTexture = nullptr;
	std::shared_ptr<skygfx::Shader> mShader = nullptr;
	GLFWwindow* mGlfwWindow = nullptr;
};

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

	auto matrices = Matrices();

	auto directional_light = DirectionalLight();
	directional_light.ambient = { 0.125f, 0.125f, 0.125f };
	directional_light.diffuse = { 0.125f, 0.125f, 0.125f };
	directional_light.specular = { 1.0f, 1.0f, 1.0f };
	directional_light.shininess = 16.0f;
	directional_light.direction = { 0.5f, -1.0f, 0.5f };

	auto point_light = PointLight();
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

	while (!glfwWindowShouldClose(window))
	{
		imgui.newFrame();

		std::tie(matrices.view, matrices.projection) = UpdateCamera(window, camera, width, height);

		matrices.eye_position = camera.position;

		std::cout << "x: " << camera.position.x << ", y: " <<
			camera.position.y << ", z: " << camera.position.z << std::endl;

		auto time = (float)glfwGetTime();

		point_light.position.x = glm::cos(time / 4.0f) * 1200.0f;
		
		ForwardRendering(device, render_buffer, matrices, directional_light, { point_light });

		ImGui::ShowDemoWindow();

		imgui.draw(device);

		device.present();

		glfwPollEvents();
	}

	glfwTerminate();

	return 0;
}