#include "NyxPCH.h"
#include "Shader.h"

namespace Nyx
{
    namespace Renderer
    {
        // Shader::~Shader()
        // {
        //     Unload();
        // }

        // bool Shader::DoLoad()
        // {
        //     std::string extension;
        //     switch (Stage)
        //     {
        //     case vk::ShaderStageFlagBits::eVertex: extension = ".vert"; break;
        //     case vk::ShaderStageFlagBits::eFragment: extension = ".frag"; break;
        //     case vk::ShaderStageFlagBits::eCompute: extension = ".comp"; break;
        //     default: return false;
        //     }

        //     std::string filePath = "shaders/" + GetId() + extension + ".spv";

        //     std::vector<char> shaderCode;
        //     if (!ReadFile(filePath, shaderCode))
        //     {
        //         return false;
        //     }

        //     CreateShaderModule(shaderCode);

        //     return Resource::Load();
        // }

        // bool Shader::DoUnload()
        // {
        //     if (IsLoaded())
        //     {
        //         // Get device from somewhere (e.g., singleton or parameter)
        //         vk::Device device = GetDevice();

        //         device.destroyShaderModule(ShaderModule);

        //         Resource::Unload();
        //     }

        //     return true;
        // }

        // vk::ShaderModule Shader::GetShaderModule() const
        // {
        //     return ShaderModule;
        // }

        // vk::ShaderStageFlagBits Shader::GetStage() const
        // {
        //     return Stage;
        // }

        // bool Shader::ReadFile(const std::string & filePath, std::vector<char>&buffer)
        // {
        //     ASSERT(false && "Not implemented yet.");
        //     return false;
        // }

        // void Shader::CreateShaderModule(const std::vector<char>&code)
        // {
        //     ASSERT(false && "Not implemented yet.");
        // }

        // vk::Device Shader::GetDevice()
        // {
        //     ASSERT(false && "Not implemented yet.");
        //     return vk::Device(); // placeholder!!
        // }

    } // Renderer
} // Nyx
