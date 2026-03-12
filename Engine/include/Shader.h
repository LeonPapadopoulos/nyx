#pragma once
#include "ResourceManager.h"
#include <vulkan/vulkan.hpp>

namespace Engine
{
	class Shader : public Resource
	{
	public:
		explicit Shader(const std::string& id, vk::ShaderStageFlagBits shaderStage)
			: Resource(id)
			, Stage(shaderStage)
		{
		}

		~Shader() override;

		bool DoLoad() override;
		bool DoUnload() override;

		vk::ShaderModule GetShaderModule() const;
		vk::ShaderStageFlagBits GetStage() const;

	private:
		bool ReadFile(const std::string& filePath, std::vector<char>& buffer);
		void CreateShaderModule(const std::vector<char>& code);
		vk::Device GetDevice();

	private:
		vk::ShaderModule ShaderModule;
		vk::ShaderStageFlagBits Stage;
	};

} // Engine