# Release Notes — OmniFrame 3D v2026.11.13.0

<p align="center">
  <b>Selecione o idioma / Select language:</b><br>
  <a href="#ptbr">🇧🇷 Português (BR)</a> | <a href="#enus">🇺🇸 English (US)</a>
</p>

---

## <a id="ptbr"></a>🇧🇷 Português (BR)

<details>
<summary>Clique para expandir / recolher notas de lançamento em português</summary><br>

# OmniFrame 3D — Versão 2026.11.13.0 (Release Inicial)

**Data de Lançamento:** 13 de novembro de 2026  
**Autor:** Fernando Nillsson Cidade (`linceu_lighthouse@outlook.com`)  
**Repositório:** `fernandoncidade/OmniFrame_cpp`  
**Licenciamento:** GNU AGPLv3 (Código Aberto) / EULA (Microsoft Store)  

---

## Principais Recursos e Entregas

### 1. Núcleo Mecânico e Solver FEA Espacial
- **Elemento 3D de Viga-Coluna (Euler-Bernoulli):** Formulação matricial exata de 12 graus de liberdade por barra (6 DOFs por nó: 3 translações e 3 rotações).
- **Matriz de Rigidez Local 12x12:** Considera resposta axial ($EA/L$), torção pura de Saint-Venant ($GJ/L$) e flexão biaxial acoplada com cortante em ambos os eixos principais da seção transversal ($EI_z/L^3$, $EI_y/L^3$).
- **Orientação Espacial e Ângulo de Roll ($\psi$):** Rotação arbitrária no espaço 3D com orientação explícita dos eixos principais de inércia da seção.
- **Liberação de Esforços (Rótulas nas Extremidades):** Suporte à liberação de momentos fletores e torção para modelagem precisa de treliças espaciais e ligações rotuladas.
- **Forças Nodais Equivalentes:** Vetores consistentes de forças nodais equivalentes para carregamentos distribuídos uniformes sobre barras e carregamentos térmicos, além de forças/momentos nodais concentrados, molas elásticas e deslocamentos/rotações prescritos.
- **Solver Esparso Direto Eigen (`Eigen::SimplicialLDLT`):** Fatoração de Cholesky esparsa de alta velocidade com ordenação de mínimo grau aproximado (AMD) e detecção automática de matrizes singulares ou mecanismos cinemáticos.

### 2. Viewport 3D Interativa e Pipeline OpenGL 3.3 Core
- **Shaders GLSL 330 Modernos:** Pipeline gráfico acelerado por hardware com iluminação difusa e especular de Phong.
- **Navegação Orbital Fluida:** Arraste com botão direito ou do meio para orbitar, `Shift` + arraste direito/meio para pan e roda do mouse para zoom suave (botão esquerdo executa a ferramenta ativa).
- **Visualização da Deformada:** Exibição da estrutura indeformada e da deformada elástica amplificada via controle deslizante (*slider* `1`–`100`, padrão `30`, correspondendo a ampliação de `1.5x`–`150x` na deformada e escala proporcional nos diagramas) em tempo real.
- **Seleção Espacial por Raycasting:** Seleção precisa de nós e barras no espaço tridimensional via clique do mouse.
- **Mapas Espectrais de Cores:** Gradiente contínuo para deslocamentos e diagramas de esforços internos (`N`, `Vy`, `Vz`, `Tx`, `My`, `Mz`), além de vetores de reações de apoio.

### 3. Interface Desktop com Qt 6.11.1
- **Docking Flexível:** Painéis acopláveis de inspetor de propriedades/entrada rápida e tabelas sincronizadas de deslocamentos, reações e esforços nas barras.
- **Catálogo Paramétrico de Seções:** Retangulares maciças, circulares maciças, tubos circulares, perfis I/W de abas paralelas e geometrias personalizadas.
- **Biblioteca de Materiais:** Aço estrutural, concreto armado, alumínio e madeira com parâmetros físicos pré-configurados.
- **Persistência Aberta em JSON (`.omniframe`) e Relatórios:** Salvamento local em JSON `.omniframe` UTF-8 com validação de esquema e checagem de finitude numérica, além de exportação nativa de relatórios técnicos de cálculo em HTML e Markdown.

### 4. Ferramentas de Build, Distribuição e Conformidade Microsoft Store
- Suporte nativo em **MSVC** (Visual Studio / Windows SDK x64) em C++23 com diretórios de build separados (`OmniFrame3D`, `Version`, `CompileLanguage` e `Testes`) e distribuições limpas externas em `D:\MISCELANEAS\VSCode\OmniFrame3D\`.
- Presets CMake padronizados (`CMakePresets.json`: `omniframe-msvc-release`, `testes-msvc-release`, etc.) para compilação em Release e execução de testes automatizados via `ctest`.
- Manifesto MSIX (`repo/AppxManifest.xml`) e script Inno Setup (`repo/OmniFrame.iss`) acompanhados de Política de Privacidade 100% offline (`docs/privacy-policy.md`), divulgação de `runFullTrust`, esclarecimento de suporte independente pelo desenvolvedor e desinstalação limpa (`%LOCALAPPDATA%\OmniFrame\OmniFrame\language.json`).

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details>
<summary>Click to expand / collapse release notes in English</summary><br>

# OmniFrame 3D — Version 2026.11.13.0 (Initial Release)

**Release Date:** November 13, 2026  
**Author:** Fernando Nillsson Cidade (`linceu_lighthouse@outlook.com`)  
**Repository:** `fernandoncidade/OmniFrame_cpp`  
**Licensing:** GNU AGPLv3 (Open Source) / EULA (Microsoft Store)  

---

## Major Features & Highlights

### 1. Mechanical Core & 3D FEA Solver
- **3D Euler-Bernoulli Beam-Column Element:** 12 DOFs per member (6 DOFs per node: 3 translations and 3 rotations).
- **12x12 Local Stiffness Matrix:** Incorporates axial deformation, Saint-Venant torsion, and biaxial flexure coupled with shear.
- **Spatial Transformation & Member Roll Angle ($\psi$):** True 3D spatial rotation with explicit orientation of section principal axes.
- **Member End Releases:** Moment releases (hinges) for modeling pin-jointed space trusses and pinned connections.
- **Consistent Equivalent Nodal Loads:** Work-equivalent nodal load vectors for uniform distributed member loads and thermal loads, alongside concentrated nodal forces/moments, elastic springs, and prescribed displacements/rotations.
- **Direct Sparse Cholesky Solver (`Eigen::SimplicialLDLT`):** High-speed sparse Cholesky factorization with Approximate Minimum Degree (AMD) ordering and automatic singularity or kinematic mechanism detection.

### 2. Interactive 3D Viewport & OpenGL 3.3 Core Pipeline
- **Modern GLSL 330 Shaders:** Hardware-accelerated pipeline with Phong diffuse and specular lighting.
- **Orbital Camera Navigation:** Right-click or middle-click drag for orbit, `Shift` + right/middle drag for pan, and scroll wheel for smooth zoom (left-click executes the active editing tool).
- **Deformed Shape Visualization:** Simultaneous rendering of undeformed geometry and real-time amplified elastic deformation via scaling slider (`1`–`100`, default `30`, mapping to `1.5x`–`150x` deformed amplification and proportional diagram scale).
- **3D Raycast Entity Picking:** High-precision mouse selection of nodes and members in 3D world space.
- **Spectral Color Mapping:** Continuous color gradients for displacements and internal action diagrams (`N`, `Vy`, `Vz`, `Tx`, `My`, `Mz`), plus support reaction vectors.

### 3. Qt 6.11.1 Desktop Interface
- **Dockable Workspace:** Customizable panels for property inspection/quick entry and synchronized node displacements, reaction, and member force tables.
- **Parametric Cross-Sections:** Solid rectangular, solid circular, hollow pipe, parallel-flange I/W profiles, and user-defined geometries.
- **Standard Materials Library:** Pre-configured structural steel, reinforced concrete, aluminum, and timber physical parameters.
- **Open `.omniframe` JSON Storage & Reports:** Local project persistence in UTF-8 `.omniframe` JSON with schema validation and numerical finiteness checks, plus native HTML and Markdown calculation report export.

### 4. Build Tooling, Distribution & Microsoft Store Compliance
- Native C++23 support on **MSVC** (Visual Studio / Windows SDK x64) with separated build directories (`OmniFrame3D`, `Version`, `CompileLanguage`, and `Testes`) and external clean distributions under `D:\MISCELANEAS\VSCode\OmniFrame3D\`.
- Standardized `CMakePresets.json` (`omniframe-msvc-release`, `testes-msvc-release`, etc.) for Release builds and automated testing via `ctest`.
- MSIX manifest (`repo/AppxManifest.xml`) and Inno Setup script (`repo/OmniFrame.iss`) paired with a 100% offline Privacy Policy (`docs/privacy-policy.md`), `runFullTrust` disclosure, independent developer support boundary, and clean uninstall support (`%LOCALAPPDATA%\OmniFrame\OmniFrame\language.json`).

</details>
