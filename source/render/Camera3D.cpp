#include "Camera3D.hpp"
#include <cmath>
#include <numbers>

namespace omniframe {

Camera3D::Camera3D() {
    setViewIsometric();
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
    QVector3D eye = eyePosition();
    QVector3D up(0.0f, 0.0f, 1.0f);

    // Evita singularidade quando olhando estritamente de cima ou de baixo
    if (std::abs(m_pitch) > 89.0f) {
        up = QVector3D(0.0f, 1.0f, 0.0f);
    }

    QMatrix4x4 v;
    v.lookAt(eye, m_target, up);
    return v;
}

QMatrix4x4 Camera3D::projectionMatrix(float aspectRatio) const {
    QMatrix4x4 p;
    if (m_isOrthographic) {
        float h = m_orthoSize * 0.5f;
        float w = h * aspectRatio;
        p.ortho(-w, w, -h, h, m_nearPlane, m_farPlane);
    } else {
        p.perspective(m_fov, aspectRatio, m_nearPlane, m_farPlane);
    }
    return p;
}

QMatrix4x4 Camera3D::viewProjectionMatrix(float aspectRatio) const {
    return projectionMatrix(aspectRatio) * viewMatrix();
}

void Camera3D::rotate(float deltaYaw, float deltaPitch) {
    m_yaw += deltaYaw;
    m_pitch += deltaPitch;

    // Normaliza yaw em [0, 360)
    while (m_yaw >= 360.0f) m_yaw -= 360.0f;
    while (m_yaw < 0.0f) m_yaw += 360.0f;

    // Trava pitch para evitar inversão da câmera
    m_pitch = std::clamp(m_pitch, -89.5f, 89.5f);
}

void Camera3D::pan(float deltaRight, float deltaUp) {
    QVector3D eye = eyePosition();
    QVector3D fwd = (m_target - eye).normalized();
    QVector3D worldUp(0.0f, 0.0f, 1.0f);
    if (std::abs(m_pitch) > 89.0f) {
        worldUp = QVector3D(0.0f, 1.0f, 0.0f);
    }

    QVector3D right = QVector3D::crossProduct(fwd, worldUp).normalized();
    QVector3D camUp = QVector3D::crossProduct(right, fwd).normalized();

    float speed = (m_isOrthographic ? m_orthoSize : m_distance) * 0.0015f;
    m_target += (right * deltaRight + camUp * deltaUp) * speed;
}

void Camera3D::zoom(float factor) {
    float zoomSpeed = 1.15f;
    if (factor > 0) {
        m_distance /= zoomSpeed;
        m_orthoSize /= zoomSpeed;
    } else {
        m_distance *= zoomSpeed;
        m_orthoSize *= zoomSpeed;
    }
    m_distance = std::clamp(m_distance, 0.5f, 2000.0f);
    m_orthoSize = std::clamp(m_orthoSize, 0.5f, 2000.0f);
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
    Vec3 center = (minPt + maxPt) * 0.5;
    m_target = QVector3D(static_cast<float>(center.x),
                         static_cast<float>(center.y),
                         static_cast<float>(center.z));

    double diag = (maxPt - minPt).norm();
    float fDiag = static_cast<float>(std::max(diag, 4.0));

    m_distance = fDiag * 1.6f;
    m_orthoSize = fDiag * 1.3f;
}

void Camera3D::screenPointToRay(int mouseX, int mouseY, int screenWidth, int screenHeight,
                                QVector3D& rayOrigin, QVector3D& rayDirection) const {
    if (screenWidth <= 0 || screenHeight <= 0) return;

    float aspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
    QMatrix4x4 invVP = viewProjectionMatrix(aspect).inverted();

    float ndcX = (2.0f * mouseX) / screenWidth - 1.0f;
    float ndcY = 1.0f - (2.0f * mouseY) / screenHeight;

    QVector4D nearPointClip(ndcX, ndcY, -1.0f, 1.0f);
    QVector4D farPointClip(ndcX, ndcY, 1.0f, 1.0f);

    QVector4D nearWorld = invVP.map(nearPointClip);
    QVector4D farWorld = invVP.map(farPointClip);

    if (std::abs(nearWorld.w()) > 1e-7f) nearWorld /= nearWorld.w();
    if (std::abs(farWorld.w()) > 1e-7f) farWorld /= farWorld.w();

    rayOrigin = nearWorld.toVector3D();
    rayDirection = (farWorld - nearWorld).toVector3D().normalized();
}

QPoint Camera3D::worldToScreen(const QVector3D& worldPoint, int screenWidth, int screenHeight) const {
    if (screenWidth <= 0 || screenHeight <= 0) return {-1, -1};

    float aspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
    QVector4D clip = viewProjectionMatrix(aspect).map(QVector4D(worldPoint, 1.0f));

    if (clip.w() <= 0.0f) {
        return {-1, -1}; // Ponto atrás da câmera
    }

    float ndcX = clip.x() / clip.w();
    float ndcY = clip.y() / clip.w();

    int sx = static_cast<int>((ndcX + 1.0f) * 0.5f * screenWidth);
    int sy = static_cast<int>((1.0f - ndcY) * 0.5f * screenHeight);

    return {sx, sy};
}

} // namespace omniframe

