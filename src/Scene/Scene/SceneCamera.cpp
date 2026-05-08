module;

#include <rstd/macro.hpp>
#include <Eigen/Dense>
module wescene.scene;
import rstd.log;
import rstd.cppstd;
import cppstd;
import wescene.utils;

using namespace owe;
using namespace Eigen;

Vector3d SceneCamera::GetPosition() const {
    if (m_node) {
        return Affine3d(m_node->GetLocalTrans()) * Vector3d::Zero();
    }
    return Vector3d::Zero();
}

Vector3d SceneCamera::GetDirection() const {
    if (m_node) {
        return (m_node->GetLocalTrans() * Vector4d(0.0f, 0.0f, -1.0f, 0.0f)).head<3>();
    }
    return -Vector3d::UnitZ();
}

Matrix4d SceneCamera::GetViewMatrix() const { return m_viewMat; }

Matrix4d SceneCamera::GetViewProjectionMatrix() const { return m_viewProjectionMat; }

void SceneCamera::CalculateViewProjectionMatrix() {
    {
        if (m_node) {
            Affine3d nodeTrans(m_node->GetLocalTrans());
            Vector3d eye    = nodeTrans * Vector3d::Zero();
            Vector3d center = nodeTrans * (-Vector3d::UnitZ());
            Vector3d up     = Vector3d::UnitY();
            m_viewMat       = LookAt(eye, center, up);
        } else
            m_viewMat = Matrix4d::Identity();
    };

    if (m_perspective) {
        m_viewProjectionMat =
            Perspective(Radians(m_fov), m_aspect, m_nearClip, m_farClip) * m_viewMat;
    } else {
        double left         = -m_width / 2.0f;
        double right        = m_width / 2.0f;
        double bottom       = -m_height / 2.0f;
        double up           = m_height / 2.0f;
        m_viewProjectionMat = Ortho(left, right, bottom, up, m_nearClip, m_farClip) * m_viewMat;
    }
}

void SceneCamera::Update() { CalculateViewProjectionMatrix(); }

void SceneCamera::AttachNode(std::shared_ptr<SceneNode> node) {
    if (! node) {
        rstd_error("Attach a null node to camera");
        return;
    }
    m_node = node;
    Update();
}

// ---- CameraPathAnimator ----------------------------------------------------

void CameraPathAnimator::Tick(double dt, SceneNode* node, SceneCamera* cam) {
    if (! HasKeyframes() || ! node) return;
    m_time += dt;

    // Wrap to loop
    double totalDuration = m_keyframes.back().time;
    if (totalDuration > 0.0 && m_time > totalDuration) {
        m_time = std::fmod(m_time, totalDuration);
    }

    // Find surrounding keyframes
    size_t i = 0;
    for (; i + 1 < m_keyframes.size(); ++i) {
        if (m_keyframes[i + 1].time >= m_time) break;
    }
    size_t j = std::min(i + 1, m_keyframes.size() - 1);

    double seg_len = m_keyframes[j].time - m_keyframes[i].time;
    float  t = (seg_len > 0.001)
                 ? static_cast<float>((m_time - m_keyframes[i].time) / seg_len)
                 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);

    // Smoothstep for organic feel
    t = t * t * (3.0f - 2.0f * t);

    auto lerp3 = [](const Vector3f& a, const Vector3f& b, float t) {
        return a + (b - a) * t;
    };

    node->SetTranslate(lerp3(m_keyframes[i].position, m_keyframes[j].position, t));
    node->SetRotation(lerp3(m_keyframes[i].angles, m_keyframes[j].angles, t));

    if (cam && m_keyframes[i].fov > 0 && m_keyframes[j].fov > 0) {
        cam->SetFov(m_keyframes[i].fov + (m_keyframes[j].fov - m_keyframes[i].fov) * t);
    }
}
