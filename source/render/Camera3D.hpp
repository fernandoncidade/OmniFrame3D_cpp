#pragma once

#include "core/Types.hpp"
#include <QMatrix4x4>
#include <QVector3D>
#include <QPoint>

namespace omniframe {

class Camera3D {
public:
    Camera3D();

    // Matrizes
    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projectionMatrix(float aspectRatio) const;
    QMatrix4x4 viewProjectionMatrix(float aspectRatio) const;

    // Posição no mundo calculada a partir de target, distance, yaw e pitch
    QVector3D eyePosition() const;

    // Manipulação Interativa
    void rotate(float deltaYaw, float deltaPitch);
    void pan(float deltaRight, float deltaUp);
    void zoom(float factor);

    // Vistas Padrão Rápidas
    void setViewIsometric();
    void setViewFrontalXZ();
    void setViewTopXY();
    void setViewSideYZ();
    void fitToBoundingBox(const Vec3& minPt, const Vec3& maxPt);

    // Alternância de Projeção
    bool isOrthographic() const { return m_isOrthographic; }
    void setOrthographic(bool ortho) { m_isOrthographic = ortho; }

    // Raycasting e Projeção
    void screenPointToRay(int mouseX, int mouseY, int screenWidth, int screenHeight,
                          QVector3D& rayOrigin, QVector3D& rayDirection) const;

    QPoint worldToScreen(const QVector3D& worldPoint, int screenWidth, int screenHeight) const;

    // Getters & Setters
    QVector3D target() const { return m_target; }
    void setTarget(const QVector3D& tgt) { m_target = tgt; }

    float distance() const { return m_distance; }
    void setDistance(float dist) { m_distance = std::clamp(dist, 0.5f, 5000.0f); }

    float yaw() const { return m_yaw; }
    void setYaw(float y) { m_yaw = y; }

    float pitch() const { return m_pitch; }
    void setPitch(float p) { m_pitch = std::clamp(p, -89.5f, 89.5f); }

private:
    QVector3D m_target{0.0f, 0.0f, 0.0f};
    float m_distance = 18.0f;
    float m_yaw = 45.0f;       // Rotação em torno do eixo Z (graus)
    float m_pitch = 30.0f;     // Elevação em relação ao plano horizontal (graus)
    float m_fov = 45.0f;       // Campo de visão (graus)
    float m_nearPlane = 0.1f;
    float m_farPlane = 2000.0f;
    float m_orthoSize = 12.0f;
    bool m_isOrthographic = false;
};

} // namespace omniframe

