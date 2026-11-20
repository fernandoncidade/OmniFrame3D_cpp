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

**Versão:** `2026.11.21.0`  
**Data:** `21 de novembro de 2026`  
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
|  MainWindow | AsyncAnalysisExecutor | EngineeringDialogs | GuidedSession|
| ResultTableModel/ProxyModel/Configurator | StartupSession | Manual/About|
|   source/os/WindowsCliIntegration (CMD / PowerShell / HKCU / Forwarding)  |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|                    VIEWPORT GRÁFICA 3D (RENDER ENGINE)                  |
|      Modern OpenGL 3.3 Core, GLSL 330, SSAA/MSAA/SDF, VBO Cache        |
|  Viewport3D | Camera3D | ShaderCollection | Clip Plane | DCR | Fallback |
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
|  UserPreferencesStore (v2) | ProjectRecoveryStore | AppLogger (10 logs) |
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

### 3.2. Suíte de Solvers (`LinearSolverSuite`), `O(1)` Lookup & Fluxo Unificado 1D/2D/3D
- **Montagem Unificada (`FemSolver3D` & `StructuralModel`):** Índices hash `O(1)` (`ensureIndex`) e `ScopedUndoSuppression` aceleram operações de modelo e cargas em lote, enquanto exclusões nodais limpam em cascata barras, vínculos rígidos/diafragmas, cargas e seleções. Elementos 1D (`BeamTruss1D`), cascas 2D (`Tri3-MITC3`, `Quad4-MITC4/QM6`, `Quad8`) e sólidos 3D (`Tet4`, `Tet10`, `Hex8-Bbar`, `Hex20`) presentes em `StructuralModel::surfaceMesh()` compartilham o mesmo `DofManager` e sistema global; nós coincidentes são fundidos automaticamente, enquanto nós sólidos puros recebem máscara `mask3` ($u_x, u_y, u_z$) e pós-processamento `ContinuumElementResult`.
- **Solvers Lineares (`LinearSolverSuite`):** `SimplicialLDLT` (padrão, com reordenação simbólica AMD), `SimplicialLLT`, `SparseLU`, `ConjugateGradient` e `BiCGSTAB`, com detecção imediata de mecanismos cinemáticos, singularidades e política explícita/rastreável de *fallback* (`LinearSolverOptions::allowFallback` + diagnóstico em `stats.message`).
- **Análises Avançadas:** Autovalores/autovetores **Modais** e **Flambagem Linear** (`BucklingSolver` + `applyBucklingImperfection`), varredura **Harmônica FRF** (amortecimento modal/Rayleigh preciso sem duplicação acidental), **Espectro de Resposta (`NBR 15421` com patamar $2{,}5\,C_a\,a_g$ e combinação direcional ortogonal invariante a sinal)**, integração **Transiente/Sísmica Newmark/HHT** (`solveTransient`/`solveSeismic`), **P-Delta iterativo** (ativado também para combos `ELU-2 P-Delta`, com $\psi_2 = 0$ sísmico em ELS-QP) / **Corrotacional** (`Corotational1D`, `NonlinearSolver` Newton-Raphson & Arc-Length com *Line Search* restaurando estados rejeitados e verificação real de equilíbrio) e **Plasticidade J2** (`J2PlasticityMaterial`: $\sigma_y, H_{iso}, H_{kin}$ em 1D/constitutivo).
- **Orquestração Assíncrona Exclusiva (`AsyncAnalysisExecutor`):** A interface gráfica executa o solver sobre `StructuralModel::cloneWithoutHistory()` (eliminando clonagem pesada da pilha Undo/Redo) em *worker thread* gerenciada por fila exclusiva, `m_modelRevision` (descartando resultados obsoletos se o modelo mudar) e cancelamento cooperativo `std::stop_token`.

---

## 4. Reconstrução Contínua e Polinômios de Hermite

Para determinar os deslocamentos transversais $v(x')$ e $w(x')$ e os diagramas contínuos de esforços solicitantes, empregam-se as funções cúbicas de forma de Hermite:
$$N_1(\xi) = 1 - 3\xi^2 + 2\xi^3, \quad N_2(\xi) = L(\xi - 2\xi^2 + \xi^3)$$
$$N_3(\xi) = 3\xi^2 - 2\xi^3, \quad N_4(\xi) = L(-\xi^2 + \xi^3), \quad \text{onde } \xi = \frac{x'}{L}$$

Os momentos fletores e esforços cortantes internos contínuos derivam analiticamente:
$$M_z(x') = E I_z \frac{d^2 v}{dx'^2}, \quad V_y(x') = -\frac{d M_z}{dx'}, \quad M_y(x') = E I_y \frac{d^2 w}{dx'^2}, \quad V_z(x') = \frac{d M_y}{dx'}$$

---

## 5. Pipeline Gráfico 3D (OpenGL 3.3 Core) e Shaders GLSL

A Viewport 3D é implementada por meio do componente `QOpenGLWidget`:
- **Shaders Compilados In-Process e Fallback Limpo:** Shaders modernos GLSL 330 com iluminação difusa Lambertiana e realce especular de Phong; quando OpenGL 3.3 Core não está disponível na sessão, exibe aviso amigável sem falha de inicialização.
- **Buffers Otimizados & Teardown Seguro:** Cache persistente de VBOs por camada, renderização instanciada de nós e barras, além de liberação determinística conectada a `QOpenGLContext::aboutToBeDestroyed`.
- **Picking Espacial por Raycasting:** Ao clicar na tela nas coordenadas $(x_s, y_s)$, as matrizes de projeção inversa e visualização inversa mapeiam o raio do olho para o espaço de mundo ($Ray_{origem}, Ray_{direcao}$), com picking de segmentos oblíquos e plano distante (*far-plane*) calibrados.
- **Janelas Filhas Modeless, Estilo Dual/Vetorial, Docks Recolhíveis e Modelo MVC Compartilhado:** O `WindowZOrderManager` prepara alças nativas ocultas (`Qt::WA_DontShowOnScreen`) antes da primeira exibição para impedir contornos fantasmas na abertura por clique único; o `ThemeManager` (`OmniFrameFusionStyle`) suporta alternância **`Customizado` / `Nativo`** (`UiStyleMode::Custom` / `Native`) e setas vetoriais sem PNGs temporários; `MainWindow` equipa os painéis com `DockTitleBar`, `RotatableTitleLabel` e `SpinArrowButton`; `ResultTableModel` / `ResultTableProxyModel` / `ResultTableConfigurator` compartilham um único modelo MVC (`QTableView`) entre `1`–`4` layouts com zero duplicação de células, seleção estrita, ordenação (`Σ Reações` fixa), coloração sRGB, duplo clique para picking 3D e exportação segura (`.xlsx`/`.csv`/`.txt`/`.json`/`.xml` antifórmula e filtro XML 1.0); `GuidedSessionDialog` utiliza sinais orientados a eventos com expansão de docks quando necessário; `Viewport3D` suporta animações em malhas 2D/3D puras, paradas DCR (`0,50/0,85/1,00`) e suprime *Clip Plane* em grades/cursor; `ManualDialog` atalha ao Capítulo 10; e `MainWindow::applyDefaultExampleWorkPlane()` padroniza os 3 exemplos em `XY (Planta)`.

---

## 6. Persistência em `.omniframe` (Schema 2.1.0), Preferências, Recuperação, Logs & CLI

- **Formato `.omniframe` (Schema `2.1.0`, retrocompatível com `1.0` e `2.0`):** JSON UTF-8 auditável (`materials` com $\rho$ e J2, `sections` preservando `Poligonal Customizada` e `Seção Composta`, `nodes`, `elements`, `"surfaceMesh"`, `LoadSystem`, análises avançadas e parâmetros ABNT).
- **Persistência Debounced, Recuperação Multi-Instância & Tail Logs:** `UserPreferencesStore` concentra em `%LOCALAPPDATA%\OmniFrame\user_preferences.json` (Schema v2, escrita atômica `QSaveFile` com *debounce* e backup `.corrupt.bak`) todas as preferências; `ProjectRecoveryStore` grava pacote único atômico por sessão (Schema v3 via `QSaveFile` + `QLockFile`) em `%LOCALAPPDATA%\OmniFrame\recovery\`; e `AppLogger` grava logs diários (`omniframe_aaaa-mm-dd.log`, máx. 10 arquivos) com leitura rápida em *tail*.
- **Interoperabilidade & Exportação:** Troca bidirecional **DXF** (`DxfInterop`) e **IFC4** (`IfcInterop`) com teto anti-DoS de `128 MB`, preservação de `surfaceMesh`, apoios e espessuras reais de casca sem abortar em nós ausentes; exportação **CSV** e **VTK `UNSTRUCTURED_GRID`**; auditoria normativa **ABNT NBR** (`NbrCodeChecks`: `LocalPdfVerified` com envoltórias `elementExtremes` sincronizadas, teto plástico `NBR 8800` em $C_b > 1$ e $N_k$ consistente `NBR 6122`; `OutOfLocalPdfScope` para normas ausentes); e relatórios **HTML**, **Markdown** e **PDF executivo A4** (`PdfReportGenerator`). Veja a auditoria técnica, ADRs e matriz V&V completa em [`docs/technical-audit-and-roadmap.md`](technical-audit-and-roadmap.md).
- **Validação estrita & CLI/OS (`source/cli/`, `source/os/`, `Utf8Path.hpp`):** Finitude (`std::isfinite`), BFS e topologia 1D/2D/3D em `ModelValidation::issues`; `OmniFrame.exe` delega flags *headless* a `omniframe-cli.exe` (validação numérica estrita via `std::from_chars` emitindo erro JSON válido); `WindowsCliIntegration` carrega APIs Win32 via `LOAD_LIBRARY_SEARCH_SYSTEM32` / caminhos explícitos em `System32` e gerencia `HKCU` (`PATH`, `App Paths`, `.omniframe`), console UTF-8 (`CommandLineToArgvW`) e `pathFromUtf8`.

> [!NOTE]
> **Transparência Matemática vs. Maturidade para Produção ("AS IS"):** A exposição aberta das equações e formulações matriciais nesta arquitetura visa transparência didática e auditabilidade acadêmica. Contudo, o **OmniFrame 3D** é um aplicativo gratuito para uso auxiliar — **e não definitivo ou final** —, ainda não maduro o suficiente para produção ou uso profissional devido a limitações, falhas humanas e possíveis erros no código-fonte (abrangendo tanto a matemática aplicada quanto a física envolvida, razão pela qual seus algoritmos podem apresentar falhas). Todos os resultados devem ser conferidos manualmente ou com ferramentas mais consolidadas no mercado; caso o usuário decida utilizá-lo exclusivamente em produção ou para uso profissional na análise e dimensionamento de estruturas, todas as consequências serão de sua inteira e exclusiva responsabilidade ([`README.md`](../README.md#ptbr) / [EULA](../source/assets/EULA/EULA_pt_BR.txt)).

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details>
<summary>Click to expand / collapse architecture in English</summary><br>

# OmniFrame 3D — System Architecture & Technical Specifications

**Version:** `2026.11.21.0`  
**Date:** `November 21, 2026`  
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
|  MainWindow | AsyncAnalysisExecutor | EngineeringDialogs | GuidedSession|
| ResultTableModel/ProxyModel/Configurator | StartupSession | Manual/About|
|   source/os/WindowsCliIntegration (CMD / PowerShell / HKCU / Forwarding)  |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|                    3D GRAPHICS VIEWPORT (RENDER ENGINE)                 |
|      Modern OpenGL 3.3 Core, GLSL 330, SSAA/MSAA/SDF, VBO Cache        |
|  Viewport3D | Camera3D | ShaderCollection | ClipPlane | DCR | Fallback  |
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
|  UserPreferencesStore (v2) | ProjectRecoveryStore | AppLogger (10 logs) |
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

### 3.2. Solver Suite (`LinearSolverSuite`), `O(1)` Lookup & Unified 1D/2D/3D Flow
- **Unified Assembly (`FemSolver3D` & `StructuralModel`):** `O(1)` hash lookup indices (`ensureIndex`) and `ScopedUndoSuppression` accelerate bulk model/load operations, while node deletions cascade cleanly across members, rigid links/diaphragms, loads, and selection lists. 1D frame/truss members (`BeamTruss1D`), 2D shells (`Tri3-MITC3`, `Quad4-MITC4/QM6`, `Quad8`), and 3D solids (`Tet4`, `Tet10`, `Hex8-Bbar`, `Hex20`) in `StructuralModel::surfaceMesh()` share one `DofManager` and global sparse assembly; coincident 1D/2D/3D nodes merge automatically, pure solid nodes allocate translational `mask3` DOFs ($u_x, u_y, u_z$), and post-processing computes `ContinuumElementResult`.
- **Linear Solvers (`LinearSolverSuite`):** `SimplicialLDLT` (default, with AMD symbolic ordering), `SimplicialLLT`, `SparseLU`, `ConjugateGradient`, and `BiCGSTAB`, with immediate mechanism/singularity detection and explicit tracked fallback (`LinearSolverOptions::allowFallback` + `stats.message`).
- **Advanced Analyses:** **Modal** and **Linear Buckling** eigenvalues/vectors (`BucklingSolver` + `applyBucklingImperfection`), **Harmonic FRF** sweep (accurate non-doubled modal/Rayleigh damping) and **Response Spectrum** (`DynamicSolver`, `NBR 15421` $2.5\,C_a\,a_g$ plateau and sign-invariant orthogonal directional combination), **Transient / Seismic** Newmark-$\beta$/HHT time-history (`solveTransient`/`solveSeismic`), iterative **P-Delta** (activated for `ELU-2 P-Delta` combos, with $\psi_2 = 0$ seismic in SLS-QP) / **Corotational** (`Corotational1D`, `NonlinearSolver` Newton-Raphson & Arc-Length with backtracking Line Search restoring rejected states and real physical equilibrium checks), and **J2 Plasticity** (`J2PlasticityMaterial`: $\sigma_y, H_{iso}, H_{kin}$ for 1D/constitutive scope).
- **Exclusive Asynchronous Orchestration (`AsyncAnalysisExecutor`):** The GUI runs solvers on `StructuralModel::cloneWithoutHistory()` (avoiding heavy Undo/Redo stack cloning) inside a single serialized queue tracking `m_modelRevision` (discarding stale results if the model changes mid-solve) and cooperative `std::stop_token` cancellation.

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
- **In-Process Compiled Shaders & Clean Fallback:** Modern GLSL 330 shaders with Lambertian diffuse illumination and Phong specular highlights; displays a user-friendly fallback notice cleanly without crashing if OpenGL 3.3 Core is unavailable in the session.
- **Optimized Buffers & Safe Teardown:** Persistent per-layer VBO caching, instanced node sphere rendering, and deterministic GPU cleanup tied to `QOpenGLContext::aboutToBeDestroyed`.
- **Spatial Raycasting Picking:** Clicking screen coordinates $(x_s, y_s)$ uses inverse projection and view matrices to cast a ray from eye to world space ($Ray_{origin}, Ray_{direction}$), with calibrated oblique-segment picking and camera far-plane depth.
- **Ghost-Free Modeless Windows, Dual/Vector Styling, Collapsible Docks & Shared MVC Tables:** `WindowZOrderManager` prepares hidden native window handles (`Qt::WA_DontShowOnScreen`) prior to first mapping to prevent transient ghost frames; `ThemeManager` (`OmniFrameFusionStyle`) supports live **`Custom` / `Native`** styling (`UiStyleMode::Custom` / `Native`) and vector arrows without temporary PNGs; `MainWindow` mounts `DockTitleBar`, `RotatableTitleLabel`, and `SpinArrowButton`; `ResultTableModel` / `ResultTableProxyModel` / `ResultTableConfigurator` share one MVC data model (`QTableView`) across `1`–`4` layouts with zero cell duplication, strict selection, pinned `Reaction Sum` sorting, sRGB contrast coloring, 3D double-click picking, and formula-safe `.xlsx`/`.csv`/`.txt`/`.json`/`.xml` export filtering XML 1.0 control chars; `GuidedSessionDialog` uses signal-driven transitions with automatic dock expansion; `Viewport3D` supports mesh-only 2D/3D animations, aligned DCR stops (`0.50/0.85/1.00`), and grid/cursor clip suppression; `ManualDialog` links straight to Chapter 10; and `MainWindow::applyDefaultExampleWorkPlane()` sets all three examples to `XY (Plan)`.

---

## 6. `.omniframe` Persistence (Schema 2.1.0), Preferences, Recovery, Logs & CLI

- **`.omniframe` Format (Schema `2.1.0`, backward-compatible with `1.0` and `2.0`):** Auditable UTF-8 JSON layout (`materials` with $\rho$ and J2, `sections` preserving `Custom Polygon` and `Composite Section`, `nodes`, `elements`, `"surfaceMesh"`, `LoadSystem`, advanced analyses, and ABNT site parameters).
- **Debounced Preferences, Multi-Instance Recovery & Tail Logs:** `UserPreferencesStore` persists `%LOCALAPPDATA%\OmniFrame\user_preferences.json` (Schema v2, debounced atomic `QSaveFile` + `.corrupt.bak`); `ProjectRecoveryStore` maintains one atomic per-session recovery bundle (Schema v3 via `QSaveFile` + `QLockFile`) inside `%LOCALAPPDATA%\OmniFrame\recovery\`; and `AppLogger` writes thread-safe daily logs (`omniframe_yyyy-mm-dd.log`, max 10 files) using fast tail seeking.
- **Interoperability & Export:** Bidirectional **DXF** (`DxfInterop`) and **IFC4** (`IfcInterop`) bounded by a `128 MB` anti-DoS ceiling while preserving `surfaceMesh`, supports, and real shell thickness cleanly even if referenced nodes are missing; **CSV** and **VTK `UNSTRUCTURED_GRID`** exports; **ABNT NBR** code checks (`NbrCodeChecks`: `LocalPdfVerified` fed by synced `elementExtremes`, NBR 8800 plastic cap for $C_b > 1$, and consistent $N_k$ in NBR 6122; `OutOfLocalPdfScope` explicitly flagged for absent standards); and native **HTML**, **Markdown**, and **executive A4 PDF** (`PdfReportGenerator`) reports. See [`docs/technical-audit-and-roadmap.md`](technical-audit-and-roadmap.md) for the full audit, ADRs, and V&V matrix.
- **Strict Validation & CLI/OS (`source/cli/`, `source/os/`, `Utf8Path.hpp`):** Finiteness (`std::isfinite`), BFS, and 1D/2D/3D topology checks (`ModelValidation::issues`); `OmniFrame.exe` forwards headless flags to `omniframe-cli.exe` (strict `std::from_chars` numeric parsing emitting valid JSON errors); `WindowsCliIntegration` enforces `LOAD_LIBRARY_SEARCH_SYSTEM32` / explicit `System32` system DLL loading alongside `HKCU` registry integration, UTF-8 consoles (`CommandLineToArgvW`), and `pathFromUtf8`.

> [!NOTE]
> **Mathematical Transparency vs. Production Maturity ("AS IS"):** Open presentation of equations and matrix formulations in this architecture serves didactic transparency and academic auditability. Nevertheless, **OmniFrame 3D** is a free application for auxiliary use — **not a definitive or final tool** — and is not yet mature enough for production or professional use due to limitations, human flaws, and possible defects in the source code (encompassing both the applied mathematics and the underlying physics, due to which its algorithms may produce errors). All outputs must be verified manually or against more established tools in the market; should the user decide to rely exclusively on it for production or professional structural analysis and design, all resulting consequences shall be the user's sole and entire responsibility ([`README.md`](../README.md#enus) / [EULA](../source/assets/EULA/EULA_en_US.txt)).

</details>
