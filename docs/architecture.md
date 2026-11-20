# Arquitetura do Sistema / System Architecture — OmniFrame 3D

<p align="center">
  <b>Selecione o idioma / Select language:</b><br>
  <a href="#ptbr">🇧🇷 Português (BR)</a> | <a href="#enus">🇺🇸 English (US)</a>
</p>

---

## <a id="ptbr"></a>🇧🇷 Português (BR)

<details>
<summary>Clique para expandir / recolher a arquitetura em português</summary><br>

# OmniFrame 3D — Arquitetura de Sistema e Especificações Técnicas

**Versão:** `2026.11.14.0`  
**Data:** `14 de novembro de 2026`  
**Padrão:** C++23 | Qt 6.11.1 | Eigen 3.4.0 | OpenGL 3.3 Core Profile  
**Plataforma Alvo:** Windows 11 / Windows 10 x64  
**Autor:** Fernando Nillsson Cidade  

---

## 1. Filosofia Arquitetural e Visão Geral em Camadas

O **OmniFrame 3D** combina alto desempenho computacional com independência e transparência científica, adotando separação rigorosa entre o núcleo numérico puro (`OmniFrameKernel`, em C++23 + Eigen 3.4.0 sem dependências Qt), a camada de projeto/solver/IO (`OmniFrameCore`), a interface gráfica desktop (`OmniFrame.exe` com Qt 6.11.1 + OpenGL 3.3 Core) e a interface de linha de comando (`omniframe-cli.exe`):

```
+-------------------------------------------------------------------------+
|                        CAMADA DE APRESENTAÇÃO / CLI                     |
|           OmniFrame.exe (Qt 6.11.1, DWM)  |  omniframe-cli.exe          |
|   MainWindow   |   EngineeringDialogs   |   GuidedSessionDialog (F2)    |
|      Dialogs      |       ManualDialog       |       AboutDialog        |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|                    VIEWPORT GRÁFICA 3D (RENDER ENGINE)                  |
|            Modern OpenGL 3.3 Core, GLSL 330, SSAA/MSAA/SDF              |
|  Viewport3D | Camera3D | ShaderCollection | Clip Plane | DCR | Animacao |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|              NÚCLEO NUMÉRICO PURO (OmniFrameKernel - Zero Qt)           |
|                      (C++23 + Eigen 3.4.0 Sparse)                       |
|  +------------------------+  +---------------------------------------+  |
|  |  Elementos & Materiais |  |  Suíte de Análise & Solvers           |  |
|  |  - BeamTruss1D/Corot1D |  |  - LinearSolverSuite (LDLT/LLT/LU/CG) |  |
|  |  - ShellElements (2D)  |  |  - BucklingSolver / DynamicSolver     |  |
|  |  - SolidElements (3D)  |  |  - NonlinearSolver (P-Delta/NR/ArcLen)|  |
|  |  - LinearElastic / J2  |  |  - NbrCodeChecks / MeshGenerator      |  |
|  +------------------------+  +---------------------------------------+  |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|               MODELO E INTEROPERABILIDADE (OmniFrameCore)               |
|  StructuralModel | LoadSystem | ProjectSerializer (.omniframe 2.1.0)    |
|  DxfInterop (.dxf) | IfcInterop (.ifc4) | PdfReportGenerator (PDF/MD)   |
+-------------------------------------------------------------------------+
```

---

## 2. Formulação Mecânica do Elemento Espacial de Viga-Coluna (Euler-Bernoulli)

### 2.1. Graus de Liberdade Nodais
Cada nó $i$ da estrutura possui 6 graus de liberdade no sistema global cartesiano:
$$\{d_i\} = \begin{Bmatrix} u_x & u_y & u_z & \theta_x & \theta_y & \theta_z \end{Bmatrix}^T$$

O elemento de barra conectando os nós $i$ e $j$ mobiliza um total de 12 graus de liberdade:
$$\{d_e\} = \begin{Bmatrix} u_{xi} & u_{yi} & u_{zi} & \theta_{xi} & \theta_{yi} & \theta_{zi} & u_{xj} & u_{yj} & u_{zj} & \theta_{xj} & \theta_{yj} & \theta_{zj} \end{Bmatrix}^T$$

### 2.2. Matriz de Rigidez Local $[k_e]$ ($12 \times 12$)
No referencial local onde o eixo $x'$ coincide com o eixo baricêntrico longitudinal da barra, e os eixos $y'$ e $z'$ coincidem com os eixos principais de inércia da seção transversal:

1. **Rigidez Axial:**
   $$k_{axial} = \frac{E A}{L} \begin{bmatrix} 1 & -1 \\ -1 & 1 \end{bmatrix}$$
2. **Rigidez Torcional (Torção Pura de Saint-Venant):**
   $$k_{torcao} = \frac{G J}{L} \begin{bmatrix} 1 & -1 \\ -1 & 1 \end{bmatrix}$$
3. **Flexão no Plano Local $x'-y'$ (Inércia $I_z$):**
   $$k_{flex,z} = \begin{bmatrix}
   \frac{12 E I_z}{L^3} & \frac{6 E I_z}{L^2} & -\frac{12 E I_z}{L^3} & \frac{6 E I_z}{L^2} \\
   \frac{6 E I_z}{L^2} & \frac{4 E I_z}{L} & -\frac{6 E I_z}{L^2} & \frac{2 E I_z}{L} \\
   -\frac{12 E I_z}{L^3} & -\frac{6 E I_z}{L^2} & \frac{12 E I_z}{L^3} & -\frac{6 E I_z}{L^2} \\
   \frac{6 E I_z}{L^2} & \frac{2 E I_z}{L} & -\frac{6 E I_z}{L^2} & \frac{4 E I_z}{L}
   \end{bmatrix}$$
4. **Flexão no Plano Local $x'-z'$ (Inércia $I_y$):**
   $$k_{flex,y} = \begin{bmatrix}
   \frac{12 E I_y}{L^3} & -\frac{6 E I_y}{L^2} & -\frac{12 E I_y}{L^3} & -\frac{6 E I_y}{L^2} \\
   -\frac{6 E I_y}{L^2} & \frac{4 E I_y}{L} & \frac{6 E I_y}{L^2} & \frac{2 E I_y}{L} \\
   -\frac{12 E I_y}{L^3} & \frac{6 E I_y}{L^2} & \frac{12 E I_y}{L^3} & \frac{6 E I_y}{L^2} \\
   -\frac{6 E I_y}{L^2} & \frac{2 E I_y}{L} & \frac{6 E I_y}{L^2} & \frac{4 E I_y}{L}
   \end{bmatrix}$$

### 2.3. Matriz de Transformação Espacial $[T]$ e Ângulo de Roll ($\psi$)
A matriz de transformação $[T]$ de dimensões $12 \times 12$ é montada por blocos a partir da matriz ortogonal de rotação $3 \times 3$ $[R]$:
$$[T] = \begin{bmatrix} [R] & [0] & [0] & [0] \\ [0] & [R] & [0] & [0] \\ [0] & [0] & [R] & [0] \\ [0] & [0] & [0] & [R] \end{bmatrix}$$

O vetor unitário longitudinal é dado por:
$$\vec{v}_x = \frac{\vec{X}_j - \vec{X}_i}{L} = \begin{Bmatrix} c_x & c_y & c_z \end{Bmatrix}^T$$

Para barras que não coincidem com a vertical absoluta, o eixo de referência auxiliar vertical $\vec{v}_{aux} = (0, 0, 1)^T$ permite obter a base ortonormal primária:
$$\vec{v}_{y0} = \frac{\vec{v}_{aux} \times \vec{v}_x}{\|\vec{v}_{aux} \times \vec{v}_x\|}, \quad \vec{v}_{z0} = \vec{v}_x \times \vec{v}_{y0}$$

Aplicando o ângulo de rolamento $\psi$ (orientação dos eixos principais da seção):
$$\vec{v}_y = \cos(\psi)\vec{v}_{y0} + \sin(\psi)\vec{v}_{z0}$$
$$\vec{v}_z = -\sin(\psi)\vec{v}_{y0} + \cos(\psi)\vec{v}_{z0}$$

A matriz de rigidez global do elemento é obtida pela transformação de congruência:
$$[K_e] = [T]^T [k_e] [T]$$

---

## 3. Resolução do Sistema Global, Suíte de Solvers & Análises Avançadas

A montagem do sistema global $[K] \{U\} = \{F\}$ utiliza armazenamento esparso comprimido em colunas (`Eigen::SparseMatrix<double>`) dentro de `OmniFrameKernel` (`GlobalAssembler` e `DofManager`).

### 3.1. Condições de Contorno e Restrições Cinemáticas
Graus de liberdade com deslocamento prescrito $u_k = \bar{u}_k$ e molas elásticas são tratados com preservação da simetria estrita da matriz $[K]$ e cálculo exato das reações de apoio:
$$\{R_{apoio}\} = [K_{livre-prescrito}]^T \{U_{livre}\} - \{F_{prescrito}\}$$

### 3.2. Suíte de Solvers (`LinearSolverSuite`) e Solvers Não Lineares / Dinâmicos
- **Solvers Lineares (`LinearSolverSuite`):** `SimplicialLDLT` (padrão, com reordenação simbólica AMD), `SimplicialLLT`, `SparseLU`, `ConjugateGradient` e `BiCGSTAB`, com detecção imediata de mecanismos cinemáticos e singularidades.
- **Análises Avançadas:** Autovalores/autovetores **Modais** e **Flambagem Linear** (`BucklingSolver`), varredura **Harmônica FRF** e **Espectro de Resposta** (`DynamicSolver`), **P-Delta / Corrotacional** (`Corotational1D`, `NonlinearSolver` Newton-Raphson & Arc-Length) e **Plasticidade J2** (`J2PlasticityMaterial`: $\sigma_y, H_{iso}, H_{kin}$).
- **Execução Assíncrona:** A interface gráfica executa o solver sobre cópia profunda de `StructuralModel` em *worker thread* (`QThread::create`) sem bloquear o *event loop*.

---

## 4. Reconstrução Contínua e Polinômios de Hermite

Para determinar os deslocamentos transversais $v(x')$ e $w(x')$ e os diagramas contínuos de esforços solicitantes, empregam-se as funções cúbicas de forma de Hermite:
$$N_1(\xi) = 1 - 3\xi^2 + 2\xi^3, \quad N_2(\xi) = L(\xi - 2\xi^2 + \xi^3)$$
$$N_3(\xi) = 3\xi^2 - 2\xi^3, \quad N_4(\xi) = L(-\xi^2 + \xi^3), \quad \text{onde } \xi = \frac{x'}{L}$$

Os momentos fletores e esforços cortantes internos contínuos derivam analiticamente:
$$M_z(x') = E I_z \frac{d^2 v}{dx'^2}, \quad V_y(x') = -\frac{d M_z}{dx'}, \quad M_y(x') = E I_y \frac{d^2 w}{dx'^2}, \quad V_z(x') = \frac{d M_y}{dx'}$$

---

## 5. Pipeline Gráfico 3D (OpenGL 3.3 Core) e Shaders GLSL

A Viewport 3D é implementada através do componente `QOpenGLWidget`:
- **Shaders Compilados In-Process:** Shaders modernos GLSL 330 com iluminação difusa Lambertiana e realce especular de Phong.
- **Buffers Otimizados:** VBOs e VAOs dedicados para nós (esferas instanciadas), barras indeformadas (cilindros neutros) e barras deformadas amplificadas.
- **Picking Espacial por Raycasting:** Ao clicar na tela nas coordenadas $(x_s, y_s)$, as matrizes de projeção inversa e visualização inversa mapeiam o raio do olho para o espaço de mundo ($Ray_{origem}, Ray_{direcao}$). A distância mínima entre o raio e cada segmento de reta de elemento tridimensional permite a seleção imediata da barra com tolerância em pixels.

---

## 6. Persistência em `.omniframe` (Schema 2.1.0), Interoperabilidade & Relatórios

- **Formato `.omniframe` (Schema `2.1.0`, retrocompatível com `1.0` e `2.0`):** JSON UTF-8 auditável (`materials` com $\rho$ e plasticidade J2, `sections`, `nodes`, `elements`, casos e combinações `LoadSystem`, análises avançadas e parâmetros ABNT).
- **Interoperabilidade & Exportação:** Importação/exportação bidirecional **DXF** (`DxfInterop`) e **IFC4** (`IfcInterop`), exportação **CSV** / **VTK**, auditoria normativa **ABNT NBR** (`NbrCodeChecks`: escopo `LocalPdfVerified` para `NBR 6118:2023`, `6120:2019`, `6122:2022`, `6123:2023`, `7480:2022`, `8800:2024`; `OutOfLocalPdfScope` para normas ausentes) e geração de relatórios em **HTML**, **Markdown** e **PDF executivo A4** (`PdfReportGenerator`).
- **Validação estrita:** Checagem de finitude (`std::isfinite`) e topologia consistente no carregamento.

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details>
<summary>Click to expand / collapse architecture in English</summary><br>

# OmniFrame 3D — System Architecture & Technical Specifications

**Version:** `2026.11.14.0`  
**Date:** `November 14, 2026`  
**Standards:** C++23 | Qt 6.11.1 | Eigen 3.4.0 | OpenGL 3.3 Core Profile  
**Target Platform:** Windows 11 / Windows 10 x64  
**Author:** Fernando Nillsson Cidade  

---

## 1. Architectural Philosophy and Layered Design

**OmniFrame 3D** combines high computational performance with scientific independence and transparency, enforcing strict decoupling between its Qt-free numerical kernel (`OmniFrameKernel` — C++23 + Eigen 3.4.0), project/solver/IO core (`OmniFrameCore`), desktop GUI (`OmniFrame.exe` — Qt 6.11.1 + OpenGL 3.3 Core), and headless CLI (`omniframe-cli.exe`):

```
+-------------------------------------------------------------------------+
|                        PRESENTATION / CLI LAYER                         |
|           OmniFrame.exe (Qt 6.11.1, DWM)  |  omniframe-cli.exe          |
|   MainWindow   |   EngineeringDialogs   |   GuidedSessionDialog (F2)    |
|      Dialogs      |       ManualDialog       |       AboutDialog        |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|                    3D GRAPHICS VIEWPORT (RENDER ENGINE)                 |
|            Modern OpenGL 3.3 Core, GLSL 330, SSAA/MSAA/SDF              |
|  Viewport3D | Camera3D | ShaderCollection | ClipPlane | DCR | Animation |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|              PURE C++23 NUMERICAL KERNEL (OmniFrameKernel)              |
|                      (C++23 + Eigen 3.4.0 Sparse)                       |
|  +------------------------+  +---------------------------------------+  |
|  |  Elements & Materials  |  |  Analysis Suite & Solvers             |  |
|  |  - BeamTruss1D/Corot1D |  |  - LinearSolverSuite (LDLT/LLT/LU/CG) |  |
|  |  - ShellElements (2D)  |  |  - BucklingSolver / DynamicSolver     |  |
|  |  - SolidElements (3D)  |  |  - NonlinearSolver (P-Delta/NR/ArcLen)|  |
|  |  - LinearElastic / J2  |  |  - NbrCodeChecks / MeshGenerator      |  |
|  +------------------------+  +---------------------------------------+  |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|              PROJECT MODEL & INTEROPERABILITY (OmniFrameCore)           |
|  StructuralModel | LoadSystem | ProjectSerializer (.omniframe 2.1.0)    |
|  DxfInterop (.dxf) | IfcInterop (.ifc4) | PdfReportGenerator (PDF/MD)   |
+-------------------------------------------------------------------------+
```

---

## 2. 3D Beam-Column Element Mechanical Formulation (Euler-Bernoulli)

### 2.1. Nodal Degrees of Freedom
Each node $i$ of the structure possesses 6 degrees of freedom in the global Cartesian system:
$$\{d_i\} = \begin{Bmatrix} u_x & u_y & u_z & \theta_x & \theta_y & \theta_z \end{Bmatrix}^T$$

The member element connecting nodes $i$ and $j$ mobilizes a total of 12 degrees of freedom:
$$\{d_e\} = \begin{Bmatrix} u_{xi} & u_{yi} & u_{zi} & \theta_{xi} & \theta_{yi} & \theta_{zi} & u_{xj} & u_{yj} & u_{zj} & \theta_{xj} & \theta_{yj} & \theta_{zj} \end{Bmatrix}^T$$

### 2.2. Local Stiffness Matrix $[k_e]$ ($12 \times 12$)
In the local frame where axis $x'$ coincides with the longitudinal centroidal axis of the member and axes $y'$ and $z'$ align with the principal axes of inertia of the cross-section:

1. **Axial Stiffness:**
   $$k_{axial} = \frac{E A}{L} \begin{bmatrix} 1 & -1 \\ -1 & 1 \end{bmatrix}$$
2. **Torsional Stiffness (Saint-Venant Pure Torsion):**
   $$k_{torsion} = \frac{G J}{L} \begin{bmatrix} 1 & -1 \\ -1 & 1 \end{bmatrix}$$
3. **Bending in Local Plane $x'-y'$ (Inertia $I_z$):**
   $$k_{flex,z} = \begin{bmatrix}
   \frac{12 E I_z}{L^3} & \frac{6 E I_z}{L^2} & -\frac{12 E I_z}{L^3} & \frac{6 E I_z}{L^2} \\
   \frac{6 E I_z}{L^2} & \frac{4 E I_z}{L} & -\frac{6 E I_z}{L^2} & \frac{2 E I_z}{L} \\
   -\frac{12 E I_z}{L^3} & -\frac{6 E I_z}{L^2} & \frac{12 E I_z}{L^3} & -\frac{6 E I_z}{L^2} \\
   \frac{6 E I_z}{L^2} & \frac{2 E I_z}{L} & -\frac{6 E I_z}{L^2} & \frac{4 E I_z}{L}
   \end{bmatrix}$$
4. **Bending in Local Plane $x'-z'$ (Inertia $I_y$):**
   $$k_{flex,y} = \begin{bmatrix}
   \frac{12 E I_y}{L^3} & -\frac{6 E I_y}{L^2} & -\frac{12 E I_y}{L^3} & -\frac{6 E I_y}{L^2} \\
   -\frac{6 E I_y}{L^2} & \frac{4 E I_y}{L} & \frac{6 E I_y}{L^2} & \frac{2 E I_y}{L} \\
   -\frac{12 E I_y}{L^3} & \frac{6 E I_y}{L^2} & \frac{12 E I_y}{L^3} & \frac{6 E I_y}{L^2} \\
   -\frac{6 E I_y}{L^2} & \frac{2 E I_y}{L} & \frac{6 E I_y}{L^2} & \frac{4 E I_y}{L}
   \end{bmatrix}$$

### 2.3. Spatial Transformation Matrix $[T]$ and Roll Angle ($\psi$)
The $12 \times 12$ transformation matrix $[T]$ is assembled in blocks from the $3 \times 3$ orthogonal rotation matrix $[R]$:
$$[T] = \begin{bmatrix} [R] & [0] & [0] & [0] \\ [0] & [R] & [0] & [0] \\ [0] & [0] & [R] & [0] \\ [0] & [0] & [0] & [R] \end{bmatrix}$$

The longitudinal unit vector is given by:
$$\vec{v}_x = \frac{\vec{X}_j - \vec{X}_i}{L} = \begin{Bmatrix} c_x & c_y & c_z \end{Bmatrix}^T$$

For members that do not coincide with the absolute vertical direction, the auxiliary vertical reference vector $\vec{v}_{aux} = (0, 0, 1)^T$ yields the primary orthonormal basis:
$$\vec{v}_{y0} = \frac{\vec{v}_{aux} \times \vec{v}_x}{\|\vec{v}_{aux} \times \vec{v}_x\|}, \quad \vec{v}_{z0} = \vec{v}_x \times \vec{v}_{y0}$$

Applying the roll angle $\psi$ (orientation of the section principal axes):
$$\vec{v}_y = \cos(\psi)\vec{v}_{y0} + \sin(\psi)\vec{v}_{z0}$$
$$\vec{v}_z = -\sin(\psi)\vec{v}_{y0} + \cos(\psi)\vec{v}_{z0}$$

The global element stiffness matrix is obtained by the congruence transformation:
$$[K_e] = [T]^T [k_e] [T]$$

---

## 3. Global System Solution, Solver Suite & Advanced Analyses

Assembly of the global system $[K] \{U\} = \{F\}$ utilizes Compressed Column Storage (`Eigen::SparseMatrix<double>`) inside `OmniFrameKernel` (`GlobalAssembler` and `DofManager`).

### 3.1. Boundary Conditions and Kinematic Restraints
Degrees of freedom with prescribed displacement $u_k = \bar{u}_k$ and elastic springs are handled while preserving strict symmetry of matrix $[K]$ and recovering exact support reactions:
$$\{R_{support}\} = [K_{free-prescribed}]^T \{U_{free}\} - \{F_{prescribed}\}$$

### 3.2. Solver Suite (`LinearSolverSuite`) & Dynamic / Nonlinear Solvers
- **Linear Solvers (`LinearSolverSuite`):** `SimplicialLDLT` (default, with AMD symbolic ordering), `SimplicialLLT`, `SparseLU`, `ConjugateGradient`, and `BiCGSTAB`, with immediate mechanism and singularity detection.
- **Advanced Analyses:** **Modal** and **Linear Buckling** eigenvalues/vectors (`BucklingSolver`), **Harmonic FRF** sweep and **Response Spectrum** (`DynamicSolver`), **P-Delta / Corotational** (`Corotational1D`, `NonlinearSolver` Newton-Raphson & Arc-Length), and **J2 Plasticity** (`J2PlasticityMaterial`: $\sigma_y, H_{iso}, H_{kin}$).
- **Asynchronous Execution:** The GUI executes solvers on a deep copy of `StructuralModel` on a worker thread (`QThread::create`) so the viewport remains responsive.

---

## 4. Continuous Recovery and Hermite Polynomials

To determine transverse displacements $v(x')$ and $w(x')$ and continuous internal force diagrams, Hermite cubic shape functions are applied:
$$N_1(\xi) = 1 - 3\xi^2 + 2\xi^3, \quad N_2(\xi) = L(\xi - 2\xi^2 + \xi^3)$$
$$N_3(\xi) = 3\xi^2 - 2\xi^3, \quad N_4(\xi) = L(-\xi^2 + \xi^3), \quad \text{where } \xi = \frac{x'}{L}$$

Continuous internal bending moments and shear forces are derived analytically:
$$M_z(x') = E I_z \frac{d^2 v}{dx'^2}, \quad V_y(x') = -\frac{d M_z}{dx'}, \quad M_y(x') = E I_y \frac{d^2 w}{dx'^2}, \quad V_z(x') = \frac{d M_y}{dx'}$$

---

## 5. 3D Graphics Pipeline (OpenGL 3.3 Core) and GLSL Shaders

The 3D Viewport is implemented using the `QOpenGLWidget` component:
- **In-Process Compiled Shaders:** Modern GLSL 330 shaders with Lambertian diffuse illumination and Phong specular highlights.
- **Optimized Buffers:** Dedicated VBOs and VAOs for nodes (instanced spheres), undeformed members (neutral cylinders), and amplified deformed members.
- **Spatial Raycasting Picking:** Clicking screen coordinates $(x_s, y_s)$ uses inverse projection and inverse view matrices to cast a ray from eye to world space ($Ray_{origin}, Ray_{direction}$). Minimum distance between the ray and each 3D member segment enables instant bar picking within pixel tolerance.

---

## 6. `.omniframe` Persistence (Schema 2.1.0), Interoperability & Reports

- **`.omniframe` Format (Schema `2.1.0`, backward-compatible with `1.0` and `2.0`):** Auditable UTF-8 JSON layout (`materials` with $\rho$ and J2 plasticity, `sections`, `nodes`, `elements`, `LoadSystem` cases/combinations, advanced analyses, and ABNT site parameters).
- **Interoperability & Export:** Bidirectional **DXF** (`DxfInterop`) and **IFC4** (`IfcInterop`) exchange, **CSV** / **VTK** export, **ABNT NBR** code checks (`NbrCodeChecks`: `LocalPdfVerified` scope for `NBR 6118:2023`, `6120:2019`, `6122:2022`, `6123:2023`, `7480:2022`, `8800:2024`; `OutOfLocalPdfScope` explicitly flagged for absent standards), and native **HTML**, **Markdown**, and **executive A4 PDF** (`PdfReportGenerator`) reports.
- **Strict Validation:** Coordinate finiteness (`std::isfinite`) and consistent topology checks upon loading.

</details>
