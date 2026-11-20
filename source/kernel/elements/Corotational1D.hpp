#pragma once

#include "BeamTruss1D.hpp"
#include "../materials/J2PlasticityMaterial.hpp"
#include <array>
#include <vector>

namespace omniframe::kernel {

/**
 * Fibra discreta de uma seção transversal para integração plástica N-My-Mz.
 */
struct SectionFiber {
    double y = 0.0;
    double z = 0.0;
    double area = 0.0;
};

/**
 * Ponto de Gauss-Lobatto ao longo do comprimento de uma barra 1D.
 */
struct BeamPlasticGaussState {
    std::vector<J2State1D> fibers;
};

/**
 * Estado completo convergido / tentativa de um elemento 1D não linear corrotacional.
 */
struct Corotational1DState {
    // Matrizes de rotação finita totais em SO(3) nos nós inicial e final (inicializadas com Identidade)
    Mat3 R1 = Mat3::Identity();
    Mat3 R2 = Mat3::Identity();

    // Estado plástico em cada ponto de Gauss ao longo da barra (vazio se elástico)
    std::vector<BeamPlasticGaussState> gaussStates;
};

struct Corotational1DEvalResult {
    Eigen::Matrix<double, 12, 1> fIntGlobal = Eigen::Matrix<double, 12, 1>::Zero();
    Eigen::Matrix<double, 12, 12> KtGlobal = Eigen::Matrix<double, 12, 12>::Zero();
    Eigen::Matrix<double, 12, 1> fIntLocal = Eigen::Matrix<double, 12, 1>::Zero();
    double currentLength = 0.0;
    double axialForce = 0.0;
    bool yielded = false;
    Corotational1DState trialState;
};

/**
 * Formulação 3D Corrotacional Exata para barras/vigas-pilar e treliças 3D,
 * com suporte opcional a plasticidade J2 distribuída por fibras (N-My-Mz).
 */
class CorotationalBeam3D {
public:
    static Mat3 expSO3(const Vec3& theta);
    static Vec3 logSO3(const Mat3& R);

    /**
     * Gera malha de fibras equivalente à seção retangular ou I/geral preservando A, Iy, Iz.
     */
    static std::vector<SectionFiber> buildDefaultFibers(double A, double Iy, double Iz, int ny = 8, int nz = 6);

    /**
     * Avalia vetor de forças internas global e matriz tangente consistente global
     * no estado incrementado `u1, dTheta1, u2, dTheta2` (a partir do último estado convergido `prevState`).
     *
     * @param X1, X2     Coordenadas de referência indeformadas (3D)
     * @param R0         Orientação local inicial [e1_0, e2_0, e3_0] (colunas = eixos locais)
     * @param props      Propriedades elásticas/geométricas da barra (props.L será L0)
     * @param u1, u2     Deslocamentos translacionais totais globais
     * @param dTheta1, dTheta2  Incrementos de rotação na iteração/passo desde `prevState`
     *                          (se updateIncremental==true, R_i = expSO3(dTheta_i) * prevState.R_i;
     *                           se updateIncremental==false, R_i = expSO3(theta_total_i))
     * @param prevState  Estado convergido no passo anterior
     * @param plasticParams  Parâmetros J2 se elastoplástico (nullptr = elástico linear local)
     * @param fibers     Fibras da seção se elastoplástico
     * @param releaseStart, releaseEnd  Rótulas/liberações de extremidade
     * @param pDeltaOnly Se true, usa cinemática geometricamente não linear P-Delta de 2ª ordem em vez de corrotacional completo
     */
    static Corotational1DEvalResult evaluate(
        const Vec3& X1,
        const Vec3& X2,
        const Mat3& R0,
        const Beam1DProps& props,
        const Vec3& u1,
        const Vec3& theta1Inc,
        const Vec3& u2,
        const Vec3& theta2Inc,
        const Corotational1DState& prevState,
        const J2PlasticityParams* plasticParams = nullptr,
        const std::vector<SectionFiber>* fibers = nullptr,
        const std::array<bool, 6>& releaseStart = {false, false, false, false, false, false},
        const std::array<bool, 6>& releaseEnd = {false, false, false, false, false, false},
        bool pDeltaOnly = false);
};

} // namespace omniframe::kernel
