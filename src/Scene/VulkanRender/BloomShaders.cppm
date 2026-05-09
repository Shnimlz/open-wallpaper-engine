module;

export module wescene.vulkan_render:bloom_shaders;
import cppstd;
import wescene.scene;

export namespace owe::vulkan
{
    std::shared_ptr<owe::SceneShader> GetBloomThresholdShader();
    std::shared_ptr<owe::SceneShader> GetBloomDownsampleShader();
    std::shared_ptr<owe::SceneShader> GetBloomUpsampleShader();
}
