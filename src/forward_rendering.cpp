#include "forward_rendering.h"
#include <magic_enum.hpp>

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

ForwardRendering::ForwardRendering(std::shared_ptr<skygfx::Device> device, const skygfx::Vertex::Layout& layout) : 
	mDevice(device)
{
	mDirectionalLightShader = std::make_shared<skygfx::Shader>(layout, common_vertex_shader_code,
		directional_light_fragment_shader_code, MakeBindingDefines<DirectionalLightBinding>());

	mPointLightShader = std::make_shared<skygfx::Shader>(layout, common_vertex_shader_code,
		point_light_fragment_shader_code, MakeBindingDefines<PointLightBinding>());

	mDirectionalLightUniformBuffer = std::make_shared<skygfx::UniformBuffer>(DirectionalLight());
	mPointLightUniformBuffer = std::make_shared<skygfx::UniformBuffer>(PointLight());
	mMatricesUniformBuffer = std::make_shared<skygfx::UniformBuffer>(Matrices());
}

void ForwardRendering::Draw(DrawGeometryFunc draw_geometry_func,
	const Matrices& matrices, const DirectionalLight& directional_light,
	const std::vector<PointLight>& point_lights)
{
	mMatricesUniformBuffer->write(matrices);
	mDirectionalLightUniformBuffer->write(directional_light);

	mDevice->setDepthMode(skygfx::DepthMode{ skygfx::ComparisonFunc::LessEqual });
	mDevice->setCullMode(skygfx::CullMode::Front);
	mDevice->setSampler(skygfx::Sampler::Linear);
	mDevice->setTextureAddress(skygfx::TextureAddress::Wrap);

	mDevice->setBlendMode(skygfx::BlendStates::Opaque);

	mDevice->setShader(*mDirectionalLightShader);

	mDevice->setUniformBuffer(GetBinding(DirectionalLightBinding::MATRICES_UNIFORM_BINDING), *mMatricesUniformBuffer);
	mDevice->setUniformBuffer(GetBinding(DirectionalLightBinding::DIRECTIONAL_LIGHT_UNIFORM_BINDING), *mDirectionalLightUniformBuffer);

	draw_geometry_func(*mDevice, GetBinding(DirectionalLightBinding::COLOR_TEXTURE_BINDING), 
		GetBinding(DirectionalLightBinding::NORMAL_TEXTURE_BINDING));

	mDevice->setBlendMode(skygfx::BlendStates::Additive);

	for (const auto& point_light : point_lights)
	{
		mPointLightUniformBuffer->write(point_light);

		mDevice->setShader(*mPointLightShader);
		mDevice->setUniformBuffer(GetBinding(PointLightBinding::MATRICES_UNIFORM_BINDING), *mMatricesUniformBuffer);
		mDevice->setUniformBuffer(GetBinding(PointLightBinding::POINT_LIGHT_UNIFORM_BINDING), *mPointLightUniformBuffer);

		draw_geometry_func(*mDevice, GetBinding(PointLightBinding::COLOR_TEXTURE_BINDING), 
			GetBinding(PointLightBinding::NORMAL_TEXTURE_BINDING));
	}
}
