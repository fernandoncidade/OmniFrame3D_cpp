# Release Notes — OmniFrame 3D v2026.11.16.0

<p align="center">
  <b>Selecione o idioma / Select language:</b><br>
  <a href="#ptbr">🇧🇷 Português (BR)</a> | <a href="#enus">🇺🇸 English (US)</a>
</p>

---

## <a id="ptbr"></a>🇧🇷 Português (BR)

<details>
<summary>Clique para expandir / recolher notas de lançamento em português</summary><br>

# OmniFrame 3D — Versão 2026.11.16.0 (Release Inicial)

**Data de Lançamento:** 16 de novembro de 2026  
**Autor:** Fernando Nillsson Cidade (`linceu_lighthouse@outlook.com`)  
**Repositório:** `fernandoncidade/OmniFrame_cpp`  
**Licenciamento:** GNU AGPLv3 (Código Aberto) / EULA (Microsoft Store)  

---

## Principais Recursos e Entregas

### 1. Integração E2E Contínua 1D/2D/3D, Validação Estruturada & Solvers Avançados
- **Malhas 1D/2D/3D Unificadas (`FemSolver3D`, `ShellElements`, `SolidElements`):** Integração completa de barras/treliças 1D (`BeamTruss1D`/`Corotational1D`), cascas 2D (`Quad4`, `Quad8`, `Tri3`, `Polígono Delaunay`) e sólidos 3D (`Hex8`, `Hex20`, `Tet4`, `Extrusão 2D -> 3D Hex8`) dentro do mesmo modelo esparso, com máscara translacional `mask3` em nós sólidos puros, acoplamento automático de nós coincidentes entre elementos 1D/2D/3D e preenchimento de **`ContinuumElementResult`** ($\sigma_{vM}$, tensões principais $\sigma_1, \sigma_3$ e resultantes de casca) diretamente pelo botão **`Calcular Estrutura (F5)`**.
- **Gerador Paramétrico de Malha (`MeshGeneratorDialog`):** Seleção integrada de **`Material da Malha:`**, **`Espessura (Casca 2D):`**, **`Condição de Contorno:`** e **`Pressão / Carga Z (2D/3D):`**, permitindo modelar, restringir, carregar e analisar lajes/paredes/blocos em um fluxo contínuo.
- **Política Explícita de Solvers Lineares & Pré-Validação:** `LinearSolverSuite` (`Auto`, `SimplicialLDLT`, `SimplicialLLT`, `SparseLU`, `ConjugateGradient`, `BiCGSTAB`) com fallback rastreável (`allowFallback` + diagnóstico em `stats.message`) e lista estruturada **`ModelValidation::issues`** (conectividade BFS, finitude e integridade de malha 2D/3D).
- **Flambagem com Imperfeição, Transiente $F(t)$ & Não Linear Corrotacional:** Flambagem linear com **`Imperfeição Inicial (amp)`** e ação **`Aplicar Imperfeição à Geometria`** (`applyBucklingImperfection`); aba sísmica/transiente ampliada com **`Newmark-Beta`** e **`HHT-Alpha (α=-0,05)`**, alternando entre **`Acelerograma Sísmico (a_g)`** e **`Histórico de Força F(t)`**; e solver não linear **`Corrotacional 3D`** com **`Tangente Completa`**, **`Line Search`**, Newton-Raphson / Crisfield Arc-Length e critério energético (`energyTol`).
- **Auditoria Normativa ABNT NBR:** Verificações automatizadas `NBR 6118:2023`, `6120:2019`, `6122:2022`, `6123:2023`, `7480:2022` e `8800:2024` (`LocalPdfVerified`), com marcação explícita `OutOfLocalPdfScope` para normas fora do pacote PDF local.

### 2. Viewport 3D OpenGL 3.3 Core, Pós-Processamento Contínuo & Tabela 2D/3D
- **Contornos 3D de von Mises & Tabulação Contínua (`PostProcessControlDialog`, Dock Inferior):** Novo modo **`Tensão de von Mises (Casca/Sólido)`** em `Corte/Contornos...` projetado diretamente sobre cascas 2D e faces 3D, somado à 4ª aba **`Esforços 2D/3D (Casca/Sólido)`** sincronizada no dock inferior, além dos diagramas cúbicos de Hermite (`N`, `Vy`, `Vz`, `Tx`, `My`, `Mz`), reações nodais, deformada (`1`–`100`, padrão `30`), planos de corte X/Y/Z, mapa de calor DCR e animações 3D.
- **Renderização HD & Plano Padrão `XY (Planta)`:** Shaders GLSL 330 com SSAA (`2.00×`), MSAA (`16×`) e Shader SDF (`graphics.json`), temas Claro/Escuro/Alto Contraste (`theme.json`), além de `MainWindow::applyDefaultExampleWorkPlane()` mantendo os 3 exemplos inicializados no plano **`XY (Planta)`** por padrão.

### 3. Interface Qt 6.11.1 Polida, Sessão Guiada (`F2`), Janelas sem Flash & Vetores Nativos
- **Eliminação Total de Janelas Fantasmas em Cliques de Diálogos Filhos:** `WindowZOrderManager` prepara alças nativas ocultas (`Qt::WA_DontShowOnScreen`) antes do primeiro mapeamento em tela, removendo flashes rápidos de moldura vazia ao abrir qualquer diálogo filho e preservando operação **modeless** com **Z-Order** gerenciado e abertura por clique único.
- **Setas Vetoriais Nativas e Cantos Arredondados em Campos Numéricos:** `ThemeManager` (`OmniFrameFusionStyle`) desenha setas de `QComboBox`, `QSpinBox` e `QDoubleSpinBox` diretamente no tema ativo sem gerar PNGs temporários, com `subControlRect` e recorte interno sincronizados para que os cantos externos dos *spin buttons* acompanhem exatamente o arredondamento da moldura.
- **Painéis Acopláveis Recolhíveis Lateral e Inferiormente (`MainWindow`):** Cabeçalhos customizados (`DockTitleBar` com `SpinArrowButton`) permitem recolher e expandir **`Inspetor e Entrada Rápida`** para a lateral esquerda com setas **`◀`/`▶`** e rótulo rotacionado em **`-90°`** (`RotatableTitleLabel`), além de recolher e expandir **`Tabelas de Resultados e Diagnóstico Estrutural`** na base com setas **`▼`/`▲`**, com suporte a modo flutuante, temas Claro/Escuro, tradução ao vivo e expansão automática pela Sessão Guiada (`F2`).
- **Sessão Guiada (`Ajuda > Sessão Guiada...`, `F2`) Dimensionada & Integrada E2E:** Janela ampliada com barra de controles em duas linhas exibindo integralmente todos os botões e rótulos; percorre os **3 exemplos no plano `XY (Planta)`** cobrindo malhas 2D/3D, contorno `Tensão de von Mises`, imperfeição de flambagem e dinâmica/não linearidade, com 4 perfis de PPM, callout unificado em duas fases sem sobreposição, tradução dinâmica integral (`.ts`/`.qm`) e restauração automática ao estado inicial.

### 4. Persistência 2.1.0, Exportações 1D/2D/3D, CLI (`omniframe-cli.exe`) & 20 Suítes CTest
- **Interoperabilidade & Relatórios:** JSON `.omniframe` Schema `2.1.0` (com bloco `"surfaceMesh"` retrocompatível com `1.0` e `2.0`), **DXF / IFC4**, exportação **VTK `UNSTRUCTURED_GRID`** 1D/2D/3D com `von_mises_kPa` / $\sigma_1$ / $\sigma_3$, **CSV** com tabelas contínuas 2D/3D e relatórios em **HTML**, **Markdown** e **PDF Executivo A4** (Seção 9 itens 1–20 + esforços no contínuo).
- **CLI Integrada & Ecossistema Windows 10/11 (CMD / PowerShell):** `omniframe-cli.exe` totalmente integrado a `OmniFrame.exe` (`source/os/WindowsCliIntegration`), suportando `--version`/`-v`, `--help`/`-h`, `--list-examples`, `--example <1|2|3|building|cantilever|truss>`, `--nbr-audit`/`--audit-nbr`, `--analysis static|modal|buckling|nonlinear|pdelta|harmonic|spectrum|transient|seismic`, `--solver`, `--case`, `--combo`, `--modes`, `--steps`, `--dt 0.02`, `--scheme <newmark|hht>`, `--method <nr|arclength>`, `--load-factor`, `--full-tangent`/`--modified-newton`, `--line-search`/`--no-line-search`, `--imperfection`/`--imperfection-mode`, `--export`, `--report`, `--output`, `--json` e `--check|install|uninstall-windows-integration`, além de caminhos UTF-8 com acentos (`Utf8Path.hpp`), abertura direta de arquivos `.omniframe`/`.dxf`/`.ifc` via `OmniFrame.exe`, submenu **`Configurações > CLI Windows (CMD / PowerShell)`**, script portátil `register_cli_windows.ps1`, instalador `OmniFrame.iss`, aliases MSIX `AppxManifest.xml`, Capítulo 10 no Manual (`F1`, com botão de acesso rápido) e verificação nas **20 suítes CTest**.

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details>
<summary>Click to expand / collapse release notes in English</summary><br>

# OmniFrame 3D — Version 2026.11.16.0 (Initial Release)

**Release Date:** November 16, 2026  
**Author:** Fernando Nillsson Cidade (`linceu_lighthouse@outlook.com`)  
**Repository:** `fernandoncidade/OmniFrame_cpp`  
**Licensing:** GNU AGPLv3 (Open Source) / EULA (Microsoft Store)  

---

## Major Features & Deliverables

### 1. End-to-End 1D/2D/3D Continuum Integration, Structured Validation & Advanced Solvers
- **Unified 1D/2D/3D Meshes (`FemSolver3D`, `ShellElements`, `SolidElements`):** Full integration of 1D beams/trusses (`BeamTruss1D`/`Corotational1D`), 2D shells (`Quad4`, `Quad8`, `Tri3`, `Delaunay Polygon`), and 3D solids (`Hex8`, `Hex20`, `Tet4`, `Extrusion 2D -> 3D Hex8`) inside the same sparse global assembly, using translational `mask3` DOFs on pure solid nodes, automatic coincident-node coupling across 1D/2D/3D elements, and **`ContinuumElementResult`** stress/resultant recovery ($\sigma_{vM}$, principal stresses $\sigma_1, \sigma_3$, shell resultants) triggered directly via **`Solve Structure (F5)`**.
- **Parametric Mesh Generator (`MeshGeneratorDialog`):** Integrated **`Mesh Material:`**, **`Thickness (2D Shell):`**, **`Boundary Condition:`**, and **`Pressure / Z Load (2D/3D):`** selectors enabling end-to-end modeling, restraining, loading, and unified solving of slabs, walls, and solid blocks.
- **Explicit Linear Solver Policy & Structured Pre-Validation:** `LinearSolverSuite` (`Auto`, `SimplicialLDLT`, `SimplicialLLT`, `SparseLU`, `ConjugateGradient`, `BiCGSTAB`) with tracked explicit fallback (`allowFallback` + diagnostic `stats.message`) and structured **`ModelValidation::issues`** diagnostics covering BFS connectivity, coordinate finiteness, and 2D/3D mesh integrity.
- **Buckling Imperfection Seeding, Transient $F(t)$ & Corotational 3D Nonlinear:** Linear buckling with **`Initial Imperfection (amp)`** and **`Apply Imperfection to Geometry`** (`applyBucklingImperfection`); seismic/transient tab supporting **`Newmark-Beta`** and **`HHT-Alpha (α=-0.05)`** across **`Seismic Accelerogram (a_g)`** and **`Force Time History F(t)`**; plus **`Corotational 3D`** nonlinear solver with **`Full Tangent`**, **`Line Search`**, Newton-Raphson / Crisfield Arc-Length, and energy convergence (`energyTol`).
- **ABNT NBR Code Audit:** Automated checks for `NBR 6118:2023`, `6120:2019`, `6122:2022`, `6123:2023`, `7480:2022`, and `8800:2024` (`LocalPdfVerified`), explicitly flagging absent standards as `OutOfLocalPdfScope`.

### 2. OpenGL 3.3 Core 3D Viewport, Continuum Post-Processing & 2D/3D Tab
- **3D von Mises Contours & Continuum Table (`PostProcessControlDialog`, Bottom Dock):** New **`von Mises Stress (Shell/Solid)`** contour mode under `Clip/Contours...` rendered over 2D shells and 3D solid faces alongside the synchronized 4th bottom tab **`2D/3D Forces (Shell/Solid)`**, Hermite continuous bar diagrams (`N`, `Vy`, `Vz`, `Tx`, `My`, `Mz`), support reaction arrows, amplified 3D deformation (`1`–`100`, default `30`), X/Y/Z clip planes, DCR heatmap, and 3D animations.
- **HD Rendering & Default `XY (Plan)` Work Plane:** GLSL 330 pipeline with SSAA (`2.00×`), MSAA (`16×`), and Shader SDF (`graphics.json`), Light/Dark/High Contrast themes (`theme.json`), and `MainWindow::applyDefaultExampleWorkPlane()` keeping all 3 built-in examples initialized in **`XY (Plan)`** by default.

### 3. Refined Qt 6.11.1 Workspace, Enlarged Guided Session (`F2`), Zero-Ghost Dialogs & Vector Styling
- **Zero-Ghost-Flash Child Window Creation:** `WindowZOrderManager` prepares hidden native window handles (`Qt::WA_DontShowOnScreen`) prior to first on-screen mapping, eliminating brief blank frame flashes when launching any child dialog while keeping **modeless** workflow, single-click launch, and managed **Z-Order**.
- **Native Vector Combo/Spin Arrows & Rounded Outer Spin-Button Corners:** `ThemeManager` (`OmniFrameFusionStyle`) renders `QComboBox`, `QSpinBox`, and `QDoubleSpinBox` arrows cleanly per active Light/Dark theme without writing temporary PNG files on disk, aligning `subControlRect` and inner clipping so spin-button outer corners match the rounded field frame seamlessly.
- **Collapsible Lateral & Bottom Dock Panels (`MainWindow`):** Custom dock headers (`DockTitleBar` with `SpinArrowButton`) collapse and expand **`Inspector & Quick Input`** horizontally to the left edge using **`◀`/`▶`** arrows and a **`-90°` rotated vertical label** (`RotatableTitleLabel`), and collapse/expand **`Result Tables & Structural Diagnostics`** along the bottom edge using **`▼`/`▲`** arrows, supporting floating mode, Light/Dark themes, live translation, and automatic expansion by Guided Session (`F2`).
- **Enlarged & End-to-End Integrated Guided Session (`Help > Guided Session...`, `F2`):** Two-row transport layout displaying all button labels without clipping; walks through all **3 examples in the default `XY (Plan)` work plane** including 2D/3D meshing, `von Mises Stress` contours, buckling imperfection, and dynamic/nonlinear solvers across 4 reading-pace WPM profiles, two-phase non-overlapping callout, complete runtime translation (`.ts`/`.qm`), and automatic example state restoration.

### 4. `.omniframe` 2.1.0, 1D/2D/3D Exports, Headless CLI (`omniframe-cli.exe`) & 20 CTest Suites
- **Interoperability & Engineering Reports:** Open `.omniframe` JSON Schema `2.1.0` (`"surfaceMesh"` backward-compatible with `1.0` & `2.0`), **DXF / IFC4** import/export, 1D/2D/3D **VTK `UNSTRUCTURED_GRID`** with `von_mises_kPa` / $\sigma_1$ / $\sigma_3$ scalar fields, continuum **CSV** stress tables, and **HTML**, **Markdown**, and **Executive A4 PDF** calculation reports (Section-9 items 1–20 plus 2D/3D continuum tables).
- **Integrated CLI & Windows 10/11 Ecosystem (CMD / PowerShell):** `omniframe-cli.exe` deeply integrated with `OmniFrame.exe` (`source/os/WindowsCliIntegration`), supporting `--version`/`-v`, `--help`/`-h`, `--list-examples`, `--example <1|2|3|building|cantilever|truss>`, `--nbr-audit`/`--audit-nbr`, `--analysis static|modal|buckling|nonlinear|pdelta|harmonic|spectrum|transient|seismic`, `--solver`, `--case`, `--combo`, `--modes`, `--steps`, `--dt 0.02`, `--scheme <newmark|hht>`, `--method <nr|arclength>`, `--load-factor`, `--full-tangent`/`--modified-newton`, `--line-search`/`--no-line-search`, `--imperfection`/`--imperfection-mode`, `--export`, `--report`, `--output`, `--json`, and `--check|install|uninstall-windows-integration`, full accented UTF-8 path support (`Utf8Path.hpp`), direct `.omniframe`/`.dxf`/`.ifc` opening via `OmniFrame.exe`, the **`Settings > Windows CLI (CMD / PowerShell)`** menu, `register_cli_windows.ps1`, `OmniFrame.iss` desktop installer tasks, MSIX `AppExecutionAlias` in `AppxManifest.xml`, Chapter 10 + quick jump button in the Interactive Manual (`F1`), and **20 CTest suites**.

</details>
