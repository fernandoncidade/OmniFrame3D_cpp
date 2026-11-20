# MANUAL DO USUÁRIO E GUIA DE ENGENHARIA FEA 3D — OMNIFRAME 3D
# USER MANUAL AND 3D FEA ENGINEERING GUIDE — OMNIFRAME 3D

**Versão / Version:** `2026.11.13.0`  
**Data / Date:** `13 de novembro de 2026` / `November 13, 2026`  
**Ambiente Tecnológico / Tech Stack:** C++23 | Qt 6.11.1 | Eigen 3.4.0 Sparse Cholesky | OpenGL 3.3 Core Profile  
**Plataforma / Platform:** Windows 11 / Windows 10 x64  
**Autor / Author:** Fernando Nillsson Cidade (`linceu_lighthouse@outlook.com`)  
**Repositório Oficial / Official Repository:** https://github.com/fernandoncidade/OmniFrame_cpp  

---

## Sumário / Table of Contents

- [PARTE 1: MANUAL DE INSTRUÇÕES (PORTUGUÊS - BR)](#parte-1-manual-de-instruções-português---br)
  - [1. Introdução e Formulação Matemática FEA 3D](#1-introdução-e-formulação-matemática-fea-3d)
  - [2. Modelagem Geométrica: Nós e Conectividade](#2-modelagem-geométrica-nós-e-conectividade)
  - [3. Materiais e Seções Transversais Paramétricas](#3-materiais-e-seções-transversais-paramétricas)
  - [4. Condições de Contorno e Vínculos Nodais](#4-condições-de-contorno-e-vínculos-nodais)
  - [5. Carregamentos e Forças Nodais Equivalentes](#5-carregamentos-e-forças-nodais-equivalentes)
  - [6. Resolução com o Solver Esparso Cholesky](#6-resolução-com-o-solver-esparso-cholesky)
  - [7. Pós-Processamento e Polinômios de Hermite](#7-pós-processamento-e-polinômios-de-hermite)
  - [8. Viewport 3D Interativa, Raycasting e Deformada](#8-viewport-3d-interativa-raycasting-e-deformada)
  - [9. Serialização (.omniframe) e Exportação de Resultados](#9-serialização-omniframe-e-exportação-de-resultados)
  - [10. Linha de Comando (CLI), Atalhos, Desinstalação Limpa & FAQ](#10-linha-de-comando-cli-atalhos-desinstalação-limpa--faq)
- [PART 2: OPERATIONS MANUAL & 3D FEA ENGINEERING GUIDE (ENGLISH - US)](#part-2-operations-manual--3d-fea-engineering-guide-english---us)
  - [1. Overview & 3D Finite Element Formulation](#1-overview--3d-finite-element-formulation)
  - [2. Geometric Modeling: Nodes & Connectivity](#2-geometric-modeling-nodes--connectivity)
  - [3. Materials & Parametric Cross-Sections](#3-materials--parametric-cross-sections)
  - [4. Boundary Conditions & Support Restraints](#4-boundary-conditions--support-restraints)
  - [5. Applied Loads & Equivalent Nodal Vectors](#5-applied-loads--equivalent-nodal-vectors)
  - [6. Sparse Cholesky Solving & Stability Diagnostics](#6-sparse-cholesky-solving--stability-diagnostics)
  - [7. Post-Processing & Continuous Hermite Recovery](#7-post-processing--continuous-hermite-recovery)
  - [8. Interactive 3D Viewport, Raycasting & Deformed Shape](#8-interactive-3d-viewport-raycasting--deformed-shape)
  - [9. Model Persistence (.omniframe) & Data Export](#9-model-persistence-omniframe--data-export)
  - [10. Command Line Interface (CLI), Shortcuts, Clean Uninstall & FAQ](#10-command-line-interface-cli-shortcuts-clean-uninstall--faq)

---

# PARTE 1: MANUAL DE INSTRUÇÕES (PORTUGUÊS - BR)

## 1. Introdução e Formulação Matemática FEA 3D

O **OmniFrame 3D** é uma plataforma de simulação computacional estrutural desenvolvida para engenheiros civis, estruturais, mecânicos e pesquisadores. A aplicação opera de modo 100% autônomo e offline (Local-First), garantindo a soberania e o sigilo de seus cálculos.

### 1.1. Formulação do Elemento Espacial de Viga-Coluna
O programa utiliza o elemento finito tridimensional de Euler-Bernoulli com 12 graus de liberdade por barra (6 graus de liberdade em cada nó extremidade):
$$\{d\} = \begin{Bmatrix} u_x & u_y & u_z & \theta_x & \theta_y & \theta_z \end{Bmatrix}^T$$

A matriz de rigidez local $[k_e]$ ($12 \times 12$) acopla:
- **Resposta Axial:** termos $EA/L$ governando a tração e compressão no eixo baricêntrico local $x'$;
- **Torção Pura de Saint-Venant:** termos $GJ/L$ governando o giro em torno do eixo longitudinal $x'$;
- **Flexão no Plano Local $x'-y'$:** termos cúbicos $12EI_z/L^3$, $6EI_z/L^2$, $4EI_z/L$ e $2EI_z/L$ associados ao esforço cortante $V_y$ e momento fletor $M_z$;
- **Flexão no Plano Local $x'-z'$:** termos cúbicos $12EI_y/L^3$, $6EI_y/L^2$, $4EI_y/L$ e $2EI_y/L$ associados ao esforço cortante $V_z$ e momento fletor $M_y$.

### 1.2. Transformação de Coordenadas Globais e Ângulo de Roll ($\psi$)
A matriz de rotação espacial $[R]$ de dimensões $3 \times 3$ transforma vetores do sistema local para o referencial cartesiano global. O ângulo de rolamento $\psi$ permite girar os eixos principais de inércia da seção transversal em torno do eixo da barra, viabilizando a modelagem de vigas inclinadas, coberturas espaciais complexas e pilares rotacionados.

---

## 2. Modelagem Geométrica: Nós e Conectividade

A topologia da estrutura é definida no espaço tridimensional:
1. **Nós Estruturais:** Criados através de suas coordenadas cartesianas $(X, Y, Z)$ em metros. Cada nó recebe um identificador numérico único.
2. **Barras (Elementos):** Conectam um nó de origem $i$ a um nó de destino $j$. O comprimento euclidiano $L = \sqrt{(X_j - X_i)^2 + (Y_j - Y_i)^2 + (Z_j - Z_i)^2}$ é calculado automaticamente.
3. **Rótulas nas Extremidades (Releases):** É possível liberar os momentos fletores e de torção nas extremidades inicial ou final, convertendo a barra em um elemento de treliça articulada.

---

## 3. Materiais e Seções Transversais Paramétricas

### 3.1. Materiais Isotrópicos
O software calcula a matriz de elasticidade a partir dos parâmetros:
- Módulo de Elasticidade Longitudinal ($E$);
- Coeficiente de Poisson ($\nu$);
- Módulo de Elasticidade Transversal ($G = \frac{E}{2(1+\nu)}$);
- Massa Específica / Densidade ($\rho$).
O catálogo inclui **Aço Estrutural ASTM A36 / MR250** ($E = 200\text{ GPa}$), **Concreto Armado C25/C30** ($E = 30\text{ GPa}$), **Alumínio 6061-T6** ($E = 70\text{ GPa}$) e **Madeira Estrutural C30**.

### 3.2. Biblioteca de Seções Paramétricas
- **Retangular Maciça:** $A = b \cdot h$, $I_z = \frac{b h^3}{12}$, $I_y = \frac{h b^3}{12}$, $J \approx \beta b^3 h$;
- **Circular Maciça:** $A = \frac{\pi d^2}{4}$, $I_z = I_y = \frac{\pi d^4}{64}$, $J = \frac{\pi d^4}{32}$;
- **Tubo Circular:** $A = \pi (R_{ext}^2 - R_{int}^2)$, $I = \frac{\pi (R_{ext}^4 - R_{int}^4)}{4}$, $J = 2 I$;
- **Perfil Metálico I / W:** Cálculo analítico rigoroso de mesas e alma com inércias em torno dos eixos forte e fraco;
- **Genérica / Customizada:** Inserção direta de propriedades geométricas obtidas de tabelas de fabricantes.

---

## 4. Condições de Contorno e Vínculos Nodais

Os nós de suporte podem ter qualquer combinação de seus 6 graus de liberdade vinculada:
- **Engaste Perfeito:** $u_x = u_y = u_z = \theta_x = \theta_y = \theta_z = 0$;
- **Apoio Fixo (2º Gênero):** $u_x = u_y = u_z = 0$ ($\theta$ livres);
- **Apoio Móvel (1º Gênero):** $u_z = 0$ (translações horizontais e rotações livres);
- **Restrição Personalizada:** Seleção individual de cada DOF restrito, apoios com molas elásticas e deslocamentos prescritos.

---

## 5. Carregamentos e Forças Nodais Equivalentes

O OmniFrame 3D suporta carregamentos nodais e sobre barras:
- **Ações Nodais:** Forças pontuais $(F_x, F_y, F_z)$ e momentos concentrados $(M_x, M_y, M_z)$ aplicados diretamente sobre os nós.
- **Cargas Distribuídas em Barras:** Carregamento uniforme $q$ integrado através das funções cúbicas de forma, transferindo automaticamente o vetor consistente de forças nodais equivalentes para os nós extremos da barra:
  $$\{F_{eq}\} = \int_0^L [N(x')]^T q(x') \, dx'$$

---

## 6. Resolução com o Solver Esparso Cholesky

Ao pressionar **F5**:
1. A matriz global $[K]$ é montada no formato esparso comprimido em colunas (`Eigen::SparseMatrix<double>`).
2. Condições de contorno cinemáticas são aplicadas por eliminação e condensação.
3. A decomposição $L D L^T$ é realizada pelo algoritmo `SimplicialLDLT` do Eigen com ordenação simbólica de grau mínimo aproximado (AMD).
4. Em caso de instabilidade (mecanismos cinemáticos ou nós desconectados), o solver alerta imediatamente o usuário indicando o nó deficiente.
5. As reações de apoio nos nós vinculados são computadas pelo equilíbrio exato $\{R\} = [K] \{U\} - \{F\}$.

---

## 7. Pós-Processamento e Polinômios de Hermite

A partir dos deslocamentos nodais calculados, os esforços internos em qualquer seção transversal $x'$ ao longo da barra são avaliados analiticamente por interpolação de Hermite:
- **Esforço Normal $N(x')$:** $E A \frac{du}{dx'}$;
- **Momento Fletor $M_z(x')$:** $E I_z \frac{d^2 v}{dx'^2}$;
- **Esforço Cortante $V_y(x')$:** $-\frac{d M_z}{dx'}$;
- **Momento de Torção $T(x')$:** $G J \frac{d\theta_x}{dx'}$.

---

## 8. Viewport 3D Interativa, Raycasting e Deformada

A viewport OpenGL 3.3 Core Profile oferece renderização de última geração:
- **Navegação 3D:**
  - **Girar / Orbitar:** Botão direito (ou do meio) do mouse pressionado e arrastado.
  - **Transladar (Pan):** `Shift` + botão direito (ou do meio) do mouse pressionado e arrastado.
  - **Aproximar / Afastar (Zoom):** Roda do mouse (*scroll*).
  - **Botão esquerdo do mouse:** Executa a ferramenta ativa na barra superior (`Selecionar`, `Nó`, `Barra`, `Apoio`, etc.).
- **Seleção Espacial por Raycasting:** Clique diretamente com o botão esquerdo (modo `Selecionar`) em nós ou barras na cena 3D para selecionar a entidade e inspecionar suas propriedades.
- **Visualização da Deformada:** Utilize a barra de escala de diagramas e deformada (intervalo de `1` a `100`, padrão `30`, correspondente a ampliação de `1,5x` a `150x` na deformada e escala proporcional `val / 30.0` nos diagramas) sobreposta à geometria indeformada.
- **Gradientes de Cor:** Escolha no seletor de diagramas entre deslocamentos e esforços internos (normal, cortante, torção e momentos fletores) para identificar visualmente o comportamento estrutural.

---

## 9. Serialização (.omniframe) e Exportação de Resultados

- **Formato `.omniframe`:** Estrutura JSON transparente e auditável (UTF-8) contendo nós, seções, materiais, barras, apoios, molas, deslocamentos prescritos e cargas.
- **Exportação:** Geração de relatórios técnicos de cálculo em **HTML** e **Markdown** (`.html` e `.md`), sem necessidade de conexão de rede externa.

---

## 10. Linha de Comando (CLI), Atalhos, Desinstalação Limpa & FAQ

### Observação sobre CLI (`OmniFrame.exe`):
- O executável principal `OmniFrame.exe` inicia diretamente a interface gráfica desktop maximizada e **não possui** flags CLI de processamento *headless* ou em lote nesta versão (consulte [docs/cli-reference.md](docs/cli-reference.md) para detalhes do alias MSIX `omniframe.exe` e das ferramentas utilitárias `CompileTranslationsTerminal.exe` e `VersionEditor.exe`).

### Atalhos Úteis:
- `Ctrl+N`: Novo Projeto
- `Ctrl+O`: Abrir Arquivo `.omniframe`
- `Ctrl+S`: Salvar Projeto
- `Ctrl+Q`: Sair do Aplicativo
- `F5`: Executar Solver FEA (`Calcular`)
- `F`: Enquadrar Estrutura na Câmera (`Zoom All`)
- `S` / `N` / `B` / `M`: Ferramentas `Selecionar` / `Nó` / `Barra` / `Medir` (`S` / `N` / `B` / `M`)
- `Del`: Excluir Entidade Selecionada
- `Esc`: Cancelar Operação / Voltar à Ferramenta `Selecionar`

### Desinstalação Limpa (Política Microsoft Store 10.2.7):
- A desinstalação via Configurações do Windows / Microsoft Store ou pelo desinstalador do Inno Setup remove os arquivos do programa sem deixar serviços em segundo plano. O único dado de preferência persistido automaticamente pelo aplicativo é `%LOCALAPPDATA%\OmniFrame\OmniFrame\language.json`, além dos projetos `.omniframe` e relatórios `.html`/`.md` salvos manualmente pelo usuário.

---

# PART 2: OPERATIONS MANUAL & 3D FEA ENGINEERING GUIDE (ENGLISH - US)

## 1. Overview & 3D Finite Element Formulation

**OmniFrame 3D** is a computational structural simulation platform engineered for civil, structural, and mechanical engineers and researchers. The application operates 100% autonomously and offline (Local-First), guaranteeing sovereignty and confidentiality for your calculations.

### 1.1. 3D Euler-Bernoulli Space Frame Element
The program employs the three-dimensional Euler-Bernoulli finite element with 12 degrees of freedom per member (6 degrees of freedom at each end node):
$$\{d\} = \begin{Bmatrix} u_x & u_y & u_z & \theta_x & \theta_y & \theta_z \end{Bmatrix}^T$$

The local stiffness matrix $[k_e]$ ($12 \times 12$) couples:
- **Axial Response:** terms $EA/L$ governing tension and compression along the local centroidal axis $x'$;
- **Saint-Venant Pure Torsion:** terms $GJ/L$ governing twist about the longitudinal axis $x'$;
- **Bending in Local Plane $x'-y'$:** cubic terms $12EI_z/L^3$, $6EI_z/L^2$, $4EI_z/L$, and $2EI_z/L$ associated with shear force $V_y$ and bending moment $M_z$;
- **Bending in Local Plane $x'-z'$:** cubic terms $12EI_y/L^3$, $6EI_y/L^2$, $4EI_y/L$, and $2EI_y/L$ associated with shear force $V_z$ and bending moment $M_y$.

### 1.2. Spatial Coordinate Transformation & Roll Angle ($\psi$)
The $3 \times 3$ spatial rotation matrix $[R]$ transforms vectors from the local system to the global Cartesian frame. The roll angle $\psi$ allows rotating the cross-section principal axes of inertia around the member axis, enabling the modeling of inclined beams, complex space frames, and rotated columns.

---

## 2. Geometric Modeling: Nodes & Connectivity

The structural topology is defined in three-dimensional space:
1. **Structural Nodes:** Created using Cartesian coordinates $(X, Y, Z)$ in meters. Each node receives a unique numeric identifier.
2. **Members (Elements):** Connect a start node $i$ to an end node $j$. Euclidean length $L = \sqrt{(X_j - X_i)^2 + (Y_j - Y_i)^2 + (Z_j - Z_i)^2}$ is computed automatically.
3. **End Releases (Hinges):** Bending and torsional moments may be released at the start or end node, converting the member into an articulated truss element.

---

## 3. Materials & Parametric Cross-Sections

### 3.1. Isotropic Materials
The software computes the elasticity matrix from physical parameters:
- Modulus of Elasticity ($E$);
- Poisson's Ratio ($\nu$);
- Shear Modulus ($G = \frac{E}{2(1+\nu)}$);
- Specific Weight / Density ($\rho$).
The catalog includes **Structural Steel ASTM A36 / MR250** ($E = 200\text{ GPa}$), **Reinforced Concrete C25/C30** ($E = 30\text{ GPa}$), **Aluminum 6061-T6** ($E = 70\text{ GPa}$), and **Structural Timber C30**.

### 3.2. Parametric Cross-Section Library
- **Solid Rectangular:** $A = b \cdot h$, $I_z = \frac{b h^3}{12}$, $I_y = \frac{h b^3}{12}$, $J \approx \beta b^3 h$;
- **Solid Circular:** $A = \frac{\pi d^2}{4}$, $I_z = I_y = \frac{\pi d^4}{64}$, $J = \frac{\pi d^4}{32}$;
- **Circular Pipe:** $A = \pi (R_{ext}^2 - R_{int}^2)$, $I = \frac{\pi (R_{ext}^4 - R_{int}^4)}{4}$, $J = 2 I$;
- **Wide-Flange I / W Profile:** Rigorous analytical calculation of flanges and web with moments of inertia about strong and weak axes;
- **Generic / Custom:** Direct entry of geometric properties obtained from manufacturer catalogs.

---

## 4. Boundary Conditions & Support Restraints

Support nodes may constrain any combination of their 6 degrees of freedom:
- **Fixed Support:** $u_x = u_y = u_z = \theta_x = \theta_y = \theta_z = 0$;
- **Pinned Support (2nd Order):** $u_x = u_y = u_z = 0$ ($\theta$ free);
- **Roller Support (1st Order):** $u_z = 0$ (horizontal translations and rotations free);
- **Custom Restraint:** Individual selection of each restrained DOF, elastic spring supports, and prescribed displacements.

---

## 5. Applied Loads & Equivalent Nodal Vectors

OmniFrame 3D supports nodal and member loading:
- **Nodal Actions:** Concentrated forces $(F_x, F_y, F_z)$ and concentrated moments $(M_x, M_y, M_z)$ applied directly to nodes.
- **Distributed Member Loads:** Uniform distributed load $q$ integrated via cubic shape functions, automatically transferring the consistent equivalent nodal force vector to the member end nodes:
  $$\{F_{eq}\} = \int_0^L [N(x')]^T q(x') \, dx'$$

---

## 6. Sparse Cholesky Solving & Stability Diagnostics

Upon pressing **F5**:
1. The global matrix $[K]$ is assembled in Compressed Column Storage sparse format (`Eigen::SparseMatrix<double>`).
2. Kinematic boundary conditions are enforced via elimination and condensation.
3. $L D L^T$ decomposition is performed by Eigen's `SimplicialLDLT` algorithm with Approximate Minimum Degree (AMD) symbolic ordering.
4. In the event of instability (kinematic mechanisms or disconnected nodes), the solver alerts the user immediately indicating the deficient node.
5. Support reactions at restrained nodes are recovered via exact equilibrium $\{R\} = [K] \{U\} - \{F\}$.

---

## 7. Post-Processing & Continuous Hermite Recovery

From calculated nodal displacements, internal forces at any cross-section $x'$ along the member are evaluated analytically via Hermite interpolation:
- **Axial Force $N(x')$:** $E A \frac{du}{dx'}$;
- **Bending Moment $M_z(x')$:** $E I_z \frac{d^2 v}{dx'^2}$;
- **Shear Force $V_y(x')$:** $-\frac{d M_z}{dx'}$;
- **Torsional Moment $T(x')$:** $G J \frac{d\theta_x}{dx'}$.

---

## 8. Interactive 3D Viewport, Raycasting & Deformed Shape

The OpenGL 3.3 Core Profile viewport delivers state-of-the-art rendering:
- **3D Navigation:**
  - **Rotate / Orbit:** Press and drag the right (or middle) mouse button.
  - **Pan Lateral:** `Shift` + right (or middle) mouse button pressed and dragged.
  - **Zoom In / Out:** Mouse scroll wheel.
  - **Left mouse button:** Executes the active tool from the top toolbar (`Select`, `Node`, `Member`, `Support`, etc.).
- **3D Raycast Entity Picking:** Left-click directly (`Select` mode) on nodes or members in the 3D scene to select the entity and inspect its properties.
- **Deformed Shape Visualization:** Use the diagram and deformed scale bar (range `1` to `100`, default `30`, mapping to `1.5x`–`150x` deformed amplification and `val / 30.0` diagram scale) overlaid on the undeformed geometry.
- **Color Gradients:** Choose in the diagram selector between displacements and internal forces (axial, shear, torsion, and bending moments) to inspect structural behavior visually.

---

## 9. Model Persistence (.omniframe) & Data Export

- **`.omniframe` Format:** Transparent, auditable UTF-8 JSON structure containing nodes, sections, materials, members, supports, springs, prescribed displacements, and loads.
- **Export:** Engineering technical report generation in **HTML** and **Markdown** (`.html` and `.md`), requiring no external network connection.

---

## 10. Command Line Interface (CLI), Shortcuts, Clean Uninstall & FAQ

### CLI Note (`OmniFrame.exe`):
- The main executable `OmniFrame.exe` launches the desktop GUI maximized directly and does **not** expose *headless* or batch CLI flags in this release (see [docs/cli-reference.md](docs/cli-reference.md) for details on the MSIX `omniframe.exe` alias and companion tools `CompileTranslationsTerminal.exe` and `VersionEditor.exe`).

### Useful Shortcuts:
- `Ctrl+N`: New Project
- `Ctrl+O`: Open `.omniframe` File
- `Ctrl+S`: Save Project
- `Ctrl+Q`: Exit Application
- `F5`: Solve FEA Model (`Solve`)
- `F`: Frame Structure in Camera (`Fit All`)
- `S` / `N` / `B` / `M`: `Select` / `Node` / `Member` / `Measure` tools (`S` / `N` / `B` / `M`)
- `Del`: Delete Selected Entity
- `Esc`: Cancel Operation / Return to `Select` Tool

### Clean Uninstallation (Microsoft Store Policy 10.2.7):
- Uninstalling via Windows Settings / Microsoft Store or the Inno Setup uninstaller removes program files cleanly without leaving background services. The only preference data persisted automatically by the application is `%LOCALAPPDATA%\OmniFrame\OmniFrame\language.json`, alongside `.omniframe` projects and `.html`/`.md` reports saved manually by the user.
