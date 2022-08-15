#pragma once

#include <skygfx/skygfx.h>

class ForwardRendering : skygfx::noncopyable
{
public:
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

	using DrawGeometryFunc = std::function<void(skygfx::StackDevice& device, uint32_t color_texture_binding,
		uint32_t normal_texture_binding)>;

public:
	ForwardRendering(std::shared_ptr<skygfx::StackDevice> device, const skygfx::Vertex::Layout& layout);

	void Draw(DrawGeometryFunc draw_geometry_func,
		const Matrices& matrices, const DirectionalLight& directional_light,
		const std::vector<PointLight>& point_lights);

private:
	std::shared_ptr<skygfx::StackDevice> mDevice = nullptr;
	std::shared_ptr<skygfx::Shader> mDirectionalLightShader = nullptr;
	std::shared_ptr<skygfx::Shader> mPointLightShader = nullptr;

	std::shared_ptr<skygfx::UniformBuffer> mDirectionalLightUniformBuffer = nullptr;
	std::shared_ptr<skygfx::UniformBuffer> mPointLightUniformBuffer = nullptr;
	std::shared_ptr<skygfx::UniformBuffer> mMatricesUniformBuffer = nullptr;
};
