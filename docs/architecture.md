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

**Versão:** `2026.11.13.0`  
**Data:** `13 de novembro de 2026`  
**Padrão:** C++23 | Qt 6.11.1 | Eigen 3.4.0 | OpenGL 3.3 Core Profile  
**Plataforma Alvo:** Windows 11 / Windows 10 x64  
**Autor:** Fernando Nillsson Cidade  

---

## 1. Filosofia Arquitetural e Visão Geral em Camadas

O **OmniFrame 3D** foi projetado para combinar alto desempenho computacional com independência e transparência científica. A arquitetura obedece a um desacoplamento estrito entre a formulação mecânica de elementos finitos, a renderização gráfica acelerada por hardware e a interface gráfica com o usuário:

```
+-------------------------------------------------------------------------+
|                        CAMADA DE APRESENTAÇÃO (UI)                      |
|                     (Qt 6.11.1 Widgets, QDockWidget, DWM)               |
|                                                                         |
|  MainWindow  |  PropertyInspector  |  NodeTableWidget  |  ElementTable  |
|  MaterialDialog  |  SectionDialog  |  LoadDialog  |  BoundaryDialog     |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|                    VIEWPORT GRÁFICA 3D (RENDER ENGINE)                  |
|                   (Modern OpenGL 3.3 Core, GLSL 330)                    |
|                                                                         |
|  Viewport3D  |  Camera3D (Orbital)  |  ShaderCollection (Phong Shaders) |
|  VAO/VBO/EBO Buffers  |  Raycasting 3D Picking  |  Color Gradient Maps  |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|                   SOLVER E MECÂNICA COMPUTACIONAL (FEA)                 |
|                      (FemSolver3D & Eigen 3.4.0 Sparse)                 |
|                                                                         |
|  +------------------------+  +---------------------------------------+  |
|  |  Matriz Local 12x12    |  |  Transformação Global 3D              |  |
|  |  - Rigidez Axial EA/L  |  |  - Eixos Locais x, y, z               |  |
|  |  - Torção GJ/L         |  |  - Ângulo de Roll (psi)               |  |
|  |  - Flexão Biaxial EI/L |  |  - Matriz de Rotação Ortogonal [T]    |  |
|  +------------------------+  +---------------------------------------+  |
|  +------------------------+  +---------------------------------------+  |
|  |  Montagem Global       |  |  Resolução Numérica Direta            |  |
|  |  - Matriz Esparsa [K]  |  |  - Cholesky (SimplicialLDLT)          |  |
|  |  - Vetor de Forças {F} |  |  - Eliminação / Condensação Cinemática|  |
|  |  - Forças Equivalentes |  |  - Detecção de Mecanismos / Singulares|  |
|  +------------------------+  +---------------------------------------+  |
|  +------------------------+  +---------------------------------------+  |
|  |  Pós-Processamento     |  |  Interpolação de Esforços             |  |
|  |  - Deslocamentos Nodais|  |  - Polinômios Cúbicos de Hermite      |  |
|  |  - Ações Extremas      |  |  - Diagramas Contínuos N, V, M, T     |  |
|  +------------------------+  +---------------------------------------+  |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|                      MODELO DE DADOS E TOPOLOGIA                        |
|                         (StructuralModel C++23)                         |
|                                                                         |
|    Node (6 DOFs)  |  Element (Barras)  |  CrossSection  |  Material     |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|                     SERIALIZAÇÃO E PERSISTÊNCIA (I/O)                   |
|                       (ProjectSerializer - Formato .omniframe)          |
|                                                                         |
|      JSON Estruturado  |  Validação Estrita de Esquemas  |  Local-First |
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

## 3. Resolução do Sistema Global e Solver Esparso

A montagem do sistema global $[K] \{U\} = \{F\}$ é realizada utilizando armazenamento esparso comprimido em colunas (`Eigen::SparseMatrix<double>`).

### 3.1. Condições de Contorno e Restrições Cinemáticas
Graus de liberdade com deslocamento prescrito $u_k = \bar{u}_k$ são tratados por substituição direta e condensação, garantindo a preservação da simetria estrita da matriz $[K]$ e permitindo o cálculo exato das reações de apoio:
$$\{R_{apoio}\} = [K_{livre-prescrito}]^T \{U_{livre}\} - \{F_{prescrito}\}$$

### 3.2. Solver Direto de Cholesky (`SimplicialLDLT`)
Utiliza-se a fatoração $P K P^T = L D L^T$:
1. **Reordenação Simbólica:** Minimização de preenchimento (*fill-in*) via algoritmo AMD (Approximate Minimum Degree).
2. **Fatoração Numérica:** Decomposição esparsa sem extração de raiz quadrada, estável para matrizes semi-definidas ou com pivôs pequenos.
3. **Detecção de Mecanismos:** Caso a matriz seja singular (determinante nulo ou autovalores negativos resultantes de nós soltos ou falta de vínculos estáveis), o solver reporta imediatamente a inconsistência cinemática ao usuário.

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

## 6. Persistência em Formato Aberto `.omniframe` e Exportação

O formato `.omniframe` é baseado em JSON estrito com codificação UTF-8 (além de exportação de relatórios técnicos em HTML e Markdown):
- Estrutura hierárquica legível por humanos (`materials`, `sections`, `nodes`, `elements`, `combinations`).
- Validação estrita de coordenadas finitas (`std::isfinite`), prevenindo `NaN` ou `Inf`.
- Compatibilidade universal para versionamento em Git.

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details>
<summary>Click to expand / collapse architecture in English</summary><br>

# OmniFrame 3D — System Architecture & Technical Specifications

**Version:** `2026.11.13.0`  
**Date:** `November 13, 2026`  
**Standards:** C++23 | Qt 6.11.1 | Eigen 3.4.0 | OpenGL 3.3 Core Profile  
**Target Platform:** Windows 11 / Windows 10 x64  
**Author:** Fernando Nillsson Cidade  

---

## 1. Architectural Philosophy and Layered Design

**OmniFrame 3D** is engineered to combine high computational performance with scientific independence and transparency. The architecture enforces strict decoupling between finite element mechanics, hardware-accelerated graphics rendering, and the graphical user interface:

```
+-------------------------------------------------------------------------+
|                        PRESENTATION LAYER (UI)                          |
|                     (Qt 6.11.1 Widgets, QDockWidget, DWM)               |
|                                                                         |
|  MainWindow  |  PropertyInspector  |  NodeTableWidget  |  ElementTable  |
|  MaterialDialog  |  SectionDialog  |  LoadDialog  |  BoundaryDialog     |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|                    3D GRAPHICS VIEWPORT (RENDER ENGINE)                 |
|                   (Modern OpenGL 3.3 Core, GLSL 330)                    |
|                                                                         |
|  Viewport3D  |  Camera3D (Orbital)  |  ShaderCollection (Phong Shaders) |
|  VAO/VBO/EBO Buffers  |  Raycasting 3D Picking  |  Color Gradient Maps  |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|                 COMPUTATIONAL MECHANICS & FEA SOLVER                    |
|                      (FemSolver3D & Eigen 3.4.0 Sparse)                 |
|                                                                         |
|  +------------------------+  +---------------------------------------+  |
|  |  12x12 Local Matrix    |  |  3D Global Transformation             |  |
|  |  - Axial Stiffness EA/L|  |  - Local Axes x, y, z                 |  |
|  |  - Torsion GJ/L        |  |  - Roll Angle (psi)                   |  |
|  |  - Biaxial Bending EI/L|  |  - Orthogonal Rotation Matrix [T]     |  |
|  +------------------------+  +---------------------------------------+  |
|  +------------------------+  +---------------------------------------+  |
|  |  Global Assembly       |  |  Direct Numerical Solve               |  |
|  |  - Sparse Matrix [K]   |  |  - Cholesky (SimplicialLDLT)          |  |
|  |  - Force Vector {F}    |  |  - Elimination / Kinematic Condens.   |  |
|  |  - Equivalent Forces   |  |  - Mechanism / Singularity Detection  |  |
|  +------------------------+  +---------------------------------------+  |
|  +------------------------+  +---------------------------------------+  |
|  |  Post-Processing       |  |  Internal Action Interpolation        |  |
|  |  - Nodal Displacements |  |  - Hermite Cubic Polynomials          |  |
|  |  - Extreme Actions     |  |  - Continuous Diagrams N, V, M, T     |  |
|  +------------------------+  +---------------------------------------+  |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|                      DATA MODEL AND TOPOLOGY                            |
|                         (StructuralModel C++23)                         |
|                                                                         |
|    Node (6 DOFs)  |  Element (Members) |  CrossSection  |  Material     |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|                    SERIALIZATION AND PERSISTENCE (I/O)                  |
|                       (ProjectSerializer - .omniframe Format)           |
|                                                                         |
|      Structured JSON   |  Strict Schema Validation  |  Local-First      |
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

## 3. Global System Solution and Sparse Solver

Assembly of the global system $[K] \{U\} = \{F\}$ utilizes Compressed Column Storage (`Eigen::SparseMatrix<double>`).

### 3.1. Boundary Conditions and Kinematic Restraints
Degrees of freedom with prescribed displacement $u_k = \bar{u}_k$ are handled via direct substitution and condensation, preserving strict symmetry of matrix $[K]$ and allowing exact support reaction computation:
$$\{R_{support}\} = [K_{free-prescribed}]^T \{U_{free}\} - \{F_{prescribed}\}$$

### 3.2. Direct Cholesky Solver (`SimplicialLDLT`)
Factorization follows $P K P^T = L D L^T$:
1. **Symbolic Reordering:** Fill-in minimization via the AMD (Approximate Minimum Degree) algorithm.
2. **Numerical Factorization:** Square-root-free sparse decomposition, stable for semi-definite matrices or small pivots.
3. **Mechanism Detection:** When the matrix is singular (zero determinant or negative eigenvalues resulting from floating nodes or insufficient stable restraints), the solver immediately reports the kinematic inconsistency to the user.

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

## 6. Open `.omniframe` Persistence and Report Export

The `.omniframe` format relies on strict UTF-8 encoded JSON (alongside engineering calculation report export in HTML and Markdown):
- Human-readable hierarchical layout (`materials`, `sections`, `nodes`, `elements`, `combinations`).
- Strict coordinate finiteness validation (`std::isfinite`), preventing `NaN` or `Inf`.
- Universal compatibility for Git version control.

</details>
