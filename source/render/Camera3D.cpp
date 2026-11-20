#include "Camera3D.hpp"
#include <cmath>
#include <numbers>

namespace omniframe {

Camera3D::Camera3D() {
    setViewIsometric();
    updateClipPlanes();
}

void Camera3D::computeBasis(QVector3D& outRight, QVector3D& outUp, QVector3D& outForward) const {
    const double radYaw = m_yaw * PI / 180.0;
    const double radPitch = m_pitch * PI / 180.0;
    const double cosP = std::cos(radPitch);
    const double sinP = std::sin(radPitch);
    const double cosY = std::cos(radYaw);
    const double sinY = std::sin(radYaw);

    outForward = QVector3D(static_cast<float>(-cosP * cosY),
                           static_cast<float>(-cosP * sinY),
                           static_cast<float>(-sinP)).normalized();
    outRight = QVector3D(static_cast<float>(-sinY),
                         static_cast<float>(cosY),
                         0.0f).normalized();
    outUp = QVector3D::crossProduct(outRight, outForward).normalized();
}

void Camera3D::updateClipPlanes() {
    const float span = std::max(m_distance, m_orthoSize);
    m_farPlane = std::max(2000.0f, span * 10.0f);
    m_nearPlane = std::clamp(m_distance * 0.001f, 0.05f, 5.0f);
}

QVector3D Camera3D::eyePosition() const {
    double radYaw = m_yaw * PI / 180.0;
    double radPitch = m_pitch * PI / 180.0;

    float cx = static_cast<float>(m_distance * std::cos(radPitch) * std::cos(radYaw));
    float cy = static_cast<float>(m_distance * std::cos(radPitch) * std::sin(radYaw));
    float cz = static_cast<float>(m_distance * std::sin(radPitch));

    return m_target + QVector3D(cx, cy, cz);
}

QMatrix4x4 Camera3D::viewMatrix() const {
    const QVector3D eye = eyePosition();
    QVector3D right, camUp, fwd;
    computeBasis(right, camUp, fwd);

    QMatrix4x4 v;
    v.lookAt(eye, m_target, camUp);
    return v;
}

QMatrix4x4 Camera3D::projectionMatrix(float aspectRatio) const {
    const float safeAspect = (std::isfinite(aspectRatio) && aspectRatio > 1e-4f) ? aspectRatio : 1.0f;
    QMatrix4x4 p;
    if (m_isOrthographic) {
        float h = m_orthoSize * 0.5f;
        float w = h * safeAspect;
        p.ortho(-w, w, -h, h, m_nearPlane, m_farPlane);
    } else {
        p.perspective(m_fov, safeAspect, m_nearPlane, m_farPlane);
    }
    return p;
}

QMatrix4x4 Camera3D::viewProjectionMatrix(float aspectRatio) const {
    return projectionMatrix(aspectRatio) * viewMatrix();
}

void Camera3D::rotate(float deltaYaw, float deltaPitch) {
    if (!std::isfinite(deltaYaw) || !std::isfinite(deltaPitch)) {
        return;
    }
    float wrappedYaw = std::fmod(m_yaw + deltaYaw, 360.0f);
    if (wrappedYaw < 0.0f) wrappedYaw += 360.0f;
    m_yaw = wrappedYaw;

    // Trava pitch para evitar inversão da câmera
    m_pitch = std::clamp(m_pitch + deltaPitch, -89.5f, 89.5f);
}

void Camera3D::pan(float deltaRight, float deltaUp) {
    if (!std::isfinite(deltaRight) || !std::isfinite(deltaUp)) {
        return;
    }
    QVector3D right, camUp, fwd;
    computeBasis(right, camUp, fwd);
    Q_UNUSED(fwd);

    float speed = (m_isOrthographic ? m_orthoSize : m_distance) * 0.0015f;
    const QVector3D nextTarget = m_target + (right * deltaRight + camUp * deltaUp) * speed;
    if (std::isfinite(nextTarget.x()) && std::isfinite(nextTarget.y()) && std::isfinite(nextTarget.z())) {
        m_target = nextTarget;
    }
}

void Camera3D::zoom(float factor) {
    if (!std::isfinite(factor)) {
        return;
    }
    float zoomSpeed = 1.15f;
    if (factor > 0) {
        m_distance /= zoomSpeed;
        m_orthoSize /= zoomSpeed;
    } else {
        m_distance *= zoomSpeed;
        m_orthoSize *= zoomSpeed;
    }
    m_distance = std::clamp(m_distance, 0.5f, 5000.0f);
    m_orthoSize = std::clamp(m_orthoSize, 0.5f, 5000.0f);
    updateClipPlanes();
}

void Camera3D::setViewIsometric() {
    m_yaw = 45.0f;
    m_pitch = 35.264f; // arctan(1/sqrt(2))
}

void Camera3D::setViewFrontalXZ() {
    m_yaw = -90.0f; // olhando do plano -Y em direção a +Y (para ver X na horizontal e Z na vertical)
    m_pitch = 0.0f;
}

void Camera3D::setViewTopXY() {
    m_yaw = -90.0f;
    m_pitch = 89.5f; // olhando de cima para o plano XY
}

void Camera3D::setViewSideYZ() {
    m_yaw = 0.0f; // olhando do plano +X em direção a -X
    m_pitch = 0.0f;
}

void Camera3D::fitToBoundingBox(const Vec3& minPt, const Vec3& maxPt) {
    if (!std::isfinite(minPt.x) || !std::isfinite(minPt.y) || !std::isfinite(minPt.z) ||
        !std::isfinite(maxPt.x) || !std::isfinite(maxPt.y) || !std::isfinite(maxPt.z)) {
        return;
    }
    Vec3 center = (minPt + maxPt) * 0.5;
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)) {
        return;
    }
    m_target = QVector3D(static_cast<float>(center.x),
                         static_cast<float>(center.y),
                         static_cast<float>(center.z));

    double diag = (maxPt - minPt).norm();
    if (!std::isfinite(diag)) diag = 4.0;
    float fDiag = std::clamp(static_cast<float>(std::max(diag, 4.0)), 4.0f, 3000.0f);

    m_distance = std::clamp(fDiag * 1.6f, 0.5f, 5000.0f);
    m_orthoSize = std::clamp(fDiag * 1.3f, 0.5f, 5000.0f);
    updateClipPlanes();
}

void Camera3D::screenPointToRay(int mouseX, int mouseY, int screenWidth, int screenHeight,
                                QVector3D& rayOrigin, QVector3D& rayDirection) const {
    if (screenWidth <= 0 || screenHeight <= 0) return;

    float aspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
    QMatrix4x4 invVP = viewProjectionMatrix(aspect).inverted();

    float ndcX = (2.0f * static_cast<float>(mouseX)) / static_cast<float>(screenWidth) - 1.0f;
    float ndcY = 1.0f - (2.0f * static_cast<float>(mouseY)) / static_cast<float>(screenHeight);

    QVector4D nearPointClip(ndcX, ndcY, -1.0f, 1.0f);
    QVector4D farPointClip(ndcX, ndcY, 1.0f, 1.0f);

    QVector4D nearWorld = invVP.map(nearPointClip);
    QVector4D farWorld = invVP.map(farPointClip);

    if (std::abs(nearWorld.w()) > 1e-7f) nearWorld /= nearWorld.w();
    if (std::abs(farWorld.w()) > 1e-7f) farWorld /= farWorld.w();

    rayOrigin = nearWorld.toVector3D();
    rayDirection = (farWorld - nearWorld).toVector3D().normalized();
}

bool Camera3D::projectWorldToScreenF(const QVector3D& worldPoint, int screenWidth, int screenHeight,
                                     QPointF& outScreenPt, float* outNdcZ) const {
    if (screenWidth <= 0 || screenHeight <= 0) return false;
    if (!std::isfinite(worldPoint.x()) || !std::isfinite(worldPoint.y()) || !std::isfinite(worldPoint.z())) {
        return false;
    }

    const float aspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
    const QVector4D clip = viewProjectionMatrix(aspect).map(QVector4D(worldPoint, 1.0f));

    if (clip.w() <= 1e-6f) {
        return false; // Ponto atrás da câmera (perspectiva) ou degenerado
    }

    const float invW = 1.0f / clip.w();
    const float ndcX = clip.x() * invW;
    const float ndcY = clip.y() * invW;
    const float ndcZ = clip.z() * invW;
    if (!std::isfinite(ndcX) || !std::isfinite(ndcY) || !std::isfinite(ndcZ)) {
        return false;
    }
    if (ndcZ < -1.0f || ndcZ > 1.0f) {
        return false; // Atrás do plano próximo (inclusive em projeção ortográfica) ou além do plano distante
    }

    const double sx = (static_cast<double>(ndcX) + 1.0) * 0.5 * static_cast<double>(screenWidth);
    const double sy = (1.0 - static_cast<double>(ndcY)) * 0.5 * static_cast<double>(screenHeight);
    outScreenPt = QPointF(sx, sy);
    if (outNdcZ != nullptr) {
        *outNdcZ = ndcZ;
    }
    return true;
}

QPoint Camera3D::worldToScreen(const QVector3D& worldPoint, int screenWidth, int screenHeight) const {
    QPointF pt;
    if (!projectWorldToScreenF(worldPoint, screenWidth, screenHeight, pt, nullptr)) {
        return {-1, -1};
    }
    return {static_cast<int>(std::lround(pt.x())), static_cast<int>(std::lround(pt.y()))};
}

} // namespace omniframe

