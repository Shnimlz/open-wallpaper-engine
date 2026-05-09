module;

#include <rstd/macro.hpp>
#include <mutex>

module wescene.vulkan_render;
import wescene.scene;
import wescene.shader_compile;
import rstd.log;
import cppstd;

namespace owe::vulkan
{

static constexpr const char* kBloomThresholdHlsl = R"hlsl(
[[vk::binding(0, 0)]] cbuffer ww_Uniforms {
    column_major float4x4 g_ModelViewProjectionMatrix;
    float g_BloomThreshold;
};

struct VSInput {
    float3 a_Position : a_Position;
    float2 a_TexCoord : a_TexCoord;
};
struct PSInput {
    float4 sv_pos : SV_Position;
    float2 v_uv   : TEXCOORD0;
};

PSInput main_vs(VSInput i) {
    PSInput o;
    o.sv_pos = mul(g_ModelViewProjectionMatrix, float4(i.a_Position, 1.0));
    o.v_uv   = i.a_TexCoord;
    return o;
}

[[vk::combinedImageSampler]][[vk::binding(1, 0)]]
Texture2D<float4> g_Texture0;
[[vk::combinedImageSampler]][[vk::binding(1, 0)]]
SamplerState g_Texture0_sampler;

float4 main_ps(PSInput i) : SV_Target {
    float4 color = g_Texture0.Sample(g_Texture0_sampler, i.v_uv);
    float brightness = max(color.r, max(color.g, color.b));
    if (brightness > g_BloomThreshold) {
        return color;
    }
    return float4(0.0, 0.0, 0.0, 1.0);
}
)hlsl";

static constexpr const char* kBloomDownsampleHlsl = R"hlsl(
[[vk::binding(0, 0)]] cbuffer ww_Uniforms {
    column_major float4x4 g_ModelViewProjectionMatrix;
    float2 g_TexelSize;
};

struct VSInput {
    float3 a_Position : a_Position;
    float2 a_TexCoord : a_TexCoord;
};
struct PSInput {
    float4 sv_pos : SV_Position;
    float2 v_uv   : TEXCOORD0;
};

PSInput main_vs(VSInput i) {
    PSInput o;
    o.sv_pos = mul(g_ModelViewProjectionMatrix, float4(i.a_Position, 1.0));
    o.v_uv   = i.a_TexCoord;
    return o;
}

[[vk::combinedImageSampler]][[vk::binding(1, 0)]]
Texture2D<float4> g_Texture0;
[[vk::combinedImageSampler]][[vk::binding(1, 0)]]
SamplerState g_Texture0_sampler;

float4 main_ps(PSInput i) : SV_Target {
    float2 uv = i.v_uv;
    float2 halfTexel = g_TexelSize * 0.5;
    
    float4 sum = g_Texture0.Sample(g_Texture0_sampler, uv) * 4.0;
    sum += g_Texture0.Sample(g_Texture0_sampler, uv - halfTexel);
    sum += g_Texture0.Sample(g_Texture0_sampler, uv + halfTexel);
    sum += g_Texture0.Sample(g_Texture0_sampler, uv + float2(halfTexel.x, -halfTexel.y));
    sum += g_Texture0.Sample(g_Texture0_sampler, uv - float2(halfTexel.x, -halfTexel.y));
    
    return sum / 8.0;
}
)hlsl";

static constexpr const char* kBloomUpsampleHlsl = R"hlsl(
[[vk::binding(0, 0)]] cbuffer ww_Uniforms {
    column_major float4x4 g_ModelViewProjectionMatrix;
    float2 g_TexelSize;
    float g_BloomStrength;
};

struct VSInput {
    float3 a_Position : a_Position;
    float2 a_TexCoord : a_TexCoord;
};
struct PSInput {
    float4 sv_pos : SV_Position;
    float2 v_uv   : TEXCOORD0;
};

PSInput main_vs(VSInput i) {
    PSInput o;
    o.sv_pos = mul(g_ModelViewProjectionMatrix, float4(i.a_Position, 1.0));
    o.v_uv   = i.a_TexCoord;
    return o;
}

[[vk::combinedImageSampler]][[vk::binding(1, 0)]]
Texture2D<float4> g_Texture0;
[[vk::combinedImageSampler]][[vk::binding(1, 0)]]
SamplerState g_Texture0_sampler;

float4 main_ps(PSInput i) : SV_Target {
    float2 uv = i.v_uv;
    float2 halfTexel = g_TexelSize * 0.5;
    
    float4 sum = 0;
    sum += g_Texture0.Sample(g_Texture0_sampler, uv + float2(-halfTexel.x * 2.0, 0.0));
    sum += g_Texture0.Sample(g_Texture0_sampler, uv + float2(-halfTexel.x, halfTexel.y)) * 2.0;
    sum += g_Texture0.Sample(g_Texture0_sampler, uv + float2(0.0, halfTexel.y * 2.0));
    sum += g_Texture0.Sample(g_Texture0_sampler, uv + float2(halfTexel.x, halfTexel.y)) * 2.0;
    sum += g_Texture0.Sample(g_Texture0_sampler, uv + float2(halfTexel.x * 2.0, 0.0));
    sum += g_Texture0.Sample(g_Texture0_sampler, uv + float2(halfTexel.x, -halfTexel.y)) * 2.0;
    sum += g_Texture0.Sample(g_Texture0_sampler, uv + float2(0.0, -halfTexel.y * 2.0));
    sum += g_Texture0.Sample(g_Texture0_sampler, uv + float2(-halfTexel.x, -halfTexel.y)) * 2.0;
    
    return sum / 12.0 * g_BloomStrength;
}
)hlsl";

static std::shared_ptr<owe::SceneShader> CompileShaderInternal(const char* name, const char* source) {
    std::array<ShaderCompUnit, 2> units {
        ShaderCompUnit { owe::ShaderType::VERTEX,   source, "main_vs" },
        ShaderCompUnit { owe::ShaderType::FRAGMENT, source, "main_ps" },
    };
    ShaderCompOpt opt {};
    opt.target   = VulkanTarget::Vulkan_1_1;
    opt.optimize = true;

    std::vector<Uni_ShaderSpv> spvs;
    if (! CompileAndLinkShaderUnits(units, opt, spvs)) {
        rstd_error("bloom shader {} compile failed", name);
        return nullptr;
    }

    auto shader  = std::make_shared<owe::SceneShader>();
    shader->id   = 0;
    shader->name = name;
    shader->codes.reserve(spvs.size());
    for (auto& spv : spvs) {
        shader->codes.emplace_back(std::move(spv->spirv));
    }
    return shader;
}

std::shared_ptr<owe::SceneShader> GetBloomThresholdShader() {
    static std::once_flag once;
    static std::shared_ptr<owe::SceneShader> shader;
    std::call_once(once, [] { shader = CompileShaderInternal("bloom_threshold", kBloomThresholdHlsl); });
    return shader;
}

std::shared_ptr<owe::SceneShader> GetBloomDownsampleShader() {
    static std::once_flag once;
    static std::shared_ptr<owe::SceneShader> shader;
    std::call_once(once, [] { shader = CompileShaderInternal("bloom_downsample", kBloomDownsampleHlsl); });
    return shader;
}

std::shared_ptr<owe::SceneShader> GetBloomUpsampleShader() {
    static std::once_flag once;
    static std::shared_ptr<owe::SceneShader> shader;
    std::call_once(once, [] { shader = CompileShaderInternal("bloom_upsample", kBloomUpsampleHlsl); });
    return shader;
}

} // namespace owe::vulkan
