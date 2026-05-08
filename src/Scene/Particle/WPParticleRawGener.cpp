module;

#include <rstd/macro.hpp>
#include <cstring>

#include <Eigen/Dense>

module wescene.scene;
import wescene.spec_texs;
import wescene.core;
import rstd.log;
import rstd.cppstd;
import cppstd;

using namespace owe;
using namespace Eigen;

struct WPGOption {
    bool thick_format { false };
    bool geometry_shader { false };
};

namespace
{
inline void AssignVertexTimes(std::span<float> dst, std::span<const float> src, unsigned num) noexcept {
    const unsigned dst_one_size = dst.size() / num;
    for (unsigned i = 0; i < num; i++) {
        std::copy(src.begin(), src.end(), dst.begin() + i * dst_one_size);
    }
}

inline void AssignVertex(std::span<float> dst, std::span<const float> src, unsigned num) noexcept {
    const unsigned dst_one_size = dst.size() / num;
    const unsigned src_one_size = src.size() / num;
    for (unsigned i = 0; i < num; i++) {
        std::copy_n(src.begin() + i * src_one_size, src_one_size, dst.begin() + i * dst_one_size);
    }
}

inline usize GenParticleData(std::span<const std::unique_ptr<ParticleInstance>> instances,
                             const ParticleRawGenSpecOp& specOp, WPGOption opt,
                             SceneVertexArray& sv) noexcept {
    std::array<float, 32 * 4> storage;

    float* data = storage.data();

    const auto one_size   = sv.OneSize();
    const auto totle_size = 4 * one_size;
    usize      i { 0 };
    for (const auto& inst : instances) {
        if (inst->IsNoLiveParticle()) continue;

        for (const auto& p : inst->Particles()) {
            if (! ParticleModify::LifetimeOk(p)) {
                continue;
            }

            float lifetime = p.lifetime;
            specOp(p, { &lifetime });

            auto  pos  = inst->GetBoundedData().pos + p.position;
            float size = p.size / 2.0f;

            usize offset = 0;

            // pos
            AssignVertexTimes(
                { data + offset, totle_size }, std::array { pos[0], pos[1], pos[2] }, 4);
            offset += 4;
            // TexCoordVec4
            float      rz = p.rotation[2];
            std::array t { 0.0f, 1.0f, rz, size, 1.0f, 1.0f, rz, size,
                           1.0f, 0.0f, rz, size, 0.0f, 0.0f, rz, size };
            AssignVertex({ data + offset, totle_size }, t, 4);
            offset += 4;

            // color
            AssignVertexTimes({ data + offset, totle_size },
                              std::array { p.color[0], p.color[1], p.color[2], p.alpha },
                              4);
            offset += 4;

            if (opt.thick_format) {
                AssignVertexTimes(
                    { data + offset, totle_size },
                    std::array { p.velocity[0], p.velocity[1], p.velocity[2], lifetime },
                    4);
                offset += 4;
            }
            // TexCoordC2
            AssignVertexTimes(
                { data + offset, totle_size }, std::array { p.rotation[0], p.rotation[1] }, 4);

            sv.SetVertexs((i++) * 4, { data, totle_size });
        }
    }
    return i;
}

inline size_t GenRopeParticleData(std::span<const std::unique_ptr<ParticleInstance>> instances,
                                  const ParticleRawGenSpecOp& specOp, WPGOption opt,
                                  SceneVertexArray& sv) {
    /*
    attribute vec4 a_PositionVec4;
    attribute vec4 a_TexCoordVec4;
    attribute vec4 a_TexCoordVec4C1;

    #if THICKFORMAT
    attribute vec4 a_TexCoordVec4C2;
    attribute vec4 a_TexCoordVec4C3;
    attribute vec2 a_TexCoordC4;
    #else
    attribute vec3 a_TexCoordVec3C2;
    attribute vec2 a_TexCoordC3;
    #endif

    attribute vec4 a_Color;

    #define in_ParticleTrailLength (a_TexCoordVec4.w)
    #define in_ParticleTrailPosition (a_TexCoordVec4C1.w)
    */
    std::array<float, 32 * 4> storage;
    float* data = storage.data();

    const auto one_size   = sv.OneSize();
    const auto totle_size = one_size * 4;
    unsigned   total_i    { 0 };

    for (const auto& inst : instances) {
        if (inst->IsNoLiveParticle()) continue;
        const auto& particles = inst->Particles();
        if (particles.empty()) continue;

        unsigned local_i { 0 };
        for (const auto& p : particles) {
            if (local_i == 0) {
                local_i++;
                continue;
            }
            if (! ParticleModify::LifetimeOk(p)) break;

            const auto& pre_p  = particles[local_i - 1];
            float       size   = p.size / 2.0f;
            std::size_t offset = 0;

            float lifetime = p.lifetime;
            specOp(p, { &lifetime });
            float in_ParticleTrailLength   = static_cast<float>(particles.size());
            float in_ParticleTrailPosition = static_cast<float>(local_i - 1);

            Vector3f cp_vec = AngleAxisf(p.rotation[2] + M_PI / 2.0f, Vector3f::UnitZ()) *
                              Vector3f { 0.0f, size / 2.0f, 0.0f };
            Vector3f pos_vec = Vector3f { p.position } - Vector3f { pre_p.position };

            cp_vec       = pos_vec.normalized().dot(cp_vec) > 0 ? cp_vec : -1.0f * cp_vec;
            auto&    sp  = pre_p;
            auto&    ep  = p;
            
            auto base_pos = inst->GetBoundedData().pos;
            Vector3f sp_pos = base_pos + Vector3f{sp.position};
            Vector3f ep_pos = base_pos + Vector3f{ep.position};

            Vector3f scp = sp_pos + cp_vec;
            Vector3f ecp = ep_pos - cp_vec;

            // a_PositionVec4: start pos
            AssignVertexTimes({ data + offset, totle_size },
                              std::array { sp_pos[0], sp_pos[1], sp_pos[2], size },
                              4);
            offset += 4;
            // a_TexCoordVec4: end pos
            AssignVertexTimes(
                { data + offset, totle_size },
                std::array { ep_pos[0], ep_pos[1], ep_pos[2], in_ParticleTrailLength },
                4);
            offset += 4;

            // a_TexCoordVec4C1: cp start pos
            AssignVertexTimes({ data + offset, totle_size },
                              std::array { scp[0], scp[1], scp[2], in_ParticleTrailPosition },
                              4);
            offset += 4;

            if (opt.thick_format) {
                // a_TexCoordVec4C2: cp end pos, size_end
                AssignVertexTimes(
                    { data + offset, totle_size }, std::array { ecp[0], ecp[1], ecp[2], size }, 4);
                offset += 4;
                // a_TexCoordVec4C3: color_end
                AssignVertexTimes({ data + offset, totle_size },
                                  std::array { p.color[0], p.color[1], p.color[2], p.alpha },
                                  4);
                offset += 4;
                // a_TexCoordC4
                std::array t { 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
                AssignVertex({ data + offset, totle_size }, t, 4);
                offset += 4;
            } else {
                // a_TexCoordVec3C2: cp end pos
                AssignVertexTimes(
                    { data + offset, totle_size }, std::array { ecp[0], ecp[1], ecp[2] }, 4);
                offset += 4;

                // a_TexCoordC3
                std::array t { 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
                AssignVertex({ data + offset, totle_size }, t, 4);
                offset += 4;
            }

            // a_Color
            AssignVertexTimes({ data + offset, totle_size },
                              std::array { p.color[0], p.color[1], p.color[2], p.alpha },
                              4);

            sv.SetVertexs((total_i++) * 4, { data, totle_size });
            local_i++;
        }
    }
    return total_i;
}

inline void updateIndexArray(uint32_t index, size_t count, SceneIndexArray& iarray) noexcept {
    constexpr size_t single_size = 6;
    uint32_t         cv          = index * 4;

    std::array<uint32_t, single_size> single;
    // 0 1 3
    // 1 2 3
    single[0] = cv;
    single[1] = cv + 1;
    single[2] = cv + 3;
    single[3] = cv + 1;
    single[4] = cv + 2;
    single[5] = cv + 3;
    // every particle
    for (uint32_t i = index; i < count; i++) {
        iarray.Assign(i * single_size, single);
        for (auto& x : single) x += 4;
    }
}
} // namespace

void WPParticleRawGener::GenGLData(std::span<const std::unique_ptr<ParticleInstance>> instances,
                                   SceneMesh& mesh, ParticleRawGenSpecOp& specOp) {
    auto& sv = mesh.GetVertexArray(0);
    auto& si = mesh.GetIndexArray(0);

    WPGOption opt;

    opt.thick_format = sv.GetOption(WE_CB_THICK_FORMAT);

    usize particle_num { 0 };

    if (sv.GetOption(WE_PRENDER_ROPE))
        particle_num += GenRopeParticleData(instances, specOp, opt, sv);
    else
        particle_num += GenParticleData(instances, specOp, opt, sv);

    // rstd_info("num: {}", particle_num);

    u32 indexNum = (u32)(si.DataCount() / 6);
    if (particle_num > indexNum) {
        updateIndexArray(indexNum, particle_num, si);
    }
    si.SetRenderDataCount(particle_num * 6);
}
