<!-- Multilanguage OBSERVACAO.md for OmniFrame_cpp -->

<p align="center">
  <b>Selecione o idioma / Select language:</b><br>
  <a href="#ptbr">🇧🇷 Português (BR)</a> |
  <a href="#enus">🇺🇸 English (US)</a> |
  <a href="#tree">🌳 Árvore de Arquivos / File Tree</a>
</p>

---

<h2 id="ptbr">🇧🇷 Português (BR)</h2>

<details>
<summary>Clique para expandir / recolher a Observação Técnica em português</summary>

# OMNIFRAME 3D - OBSERVAÇÃO TÉCNICA E POSICIONAMENTO ESTRATÉGICO

Versão documentada: `2026.11.16.0`  
Data do snapshot técnico: `16 de novembro de 2026`  
Repositório: `OmniFrame_cpp`  
Autor identificado no projeto: `Fernando Nillsson Cidade`  
Licença do repositório: `GNU AGPLv3 (GNU Affero General Public License v3)`  
Licença da Edição Microsoft Store: `EULA Oficial do Autor (Distribuição Gratuita)`  

## 1. Estado Atual do Projeto e Posicionamento

O repositório **OmniFrame 3D** consolida uma plataforma desktop corporativa e científica de engenharia estrutural **nativa em C++23 com Qt 6.11.1**, álgebra linear esparsa com **Eigen 3.4.0** e aceleração gráfica tridimensional em **OpenGL 3.3 Core Profile** (GLSL 330).

O produto transcende a categoria de visualizadores geométricos ou ferramentas CAD convencionais, posicionando-se como uma **Suíte Local de Análise Estrutural Tridimensional por Elementos Finitos (FEA) de Pórticos e Treliças Espaciais**.

Sua proposta de valor central responde diretamente à pergunta mais crítica no projeto e cálculo de estruturas civis e mecânicas:
> **“Quais são os deslocamentos nodais, tensões axiais e esforços solicitantes internos (N, Vy, Vz, T, My, Mz) ao longo de cada elemento no espaço tridimensional sob combinações reais de carregamento?”**

O produto atua como uma solução soberana **100% Local-First** (sem nuvem, sem telemetria, sem criação de contas, sem rastreadores e sem dependência de serviços externos), operando com independência e integridade analítica garantida.

## 2. Linha Técnica e Destaques da Versão 2026.11.16.0

Esta versão consolida a maturidade técnica da engine de cálculo e a estabilização completa dos subsistemas analíticos:

- **Núcleo Puro C++23 + Eigen 3.4.0 (`OmniFrameKernel`):** Biblioteca numérica estritamente sem Qt implementando elementos 1D/2D/3D (`BeamTruss1D`, `Corotational1D`, `ShellElements`, `SolidElements` com DOFs `mask3` e acoplamento automático), modelos constitutivos elástico linear e **Plasticidade J2** (`LinearElasticMaterial`, `J2PlasticityMaterial`), geração paramétrica de malhas (`MeshGenerator`) e verificações `NbrCodeChecks`;
- **Suíte Multi-Solver & Análises Avançadas:** `LinearSolverSuite` esparso (`SimplicialLDLT` AMD, `SimplicialLLT`, `SparseLU`, `ConjugateGradient`, `BiCGSTAB` com fallback rastreável), `BucklingSolver` (com aplicação direta de imperfeição geométrica inicial), `DynamicSolver` (Modal, Harmônica FRF, Espectro de Resposta e Transiente/Sísmica Newmark-$\beta$/HHT-$\alpha$ com histórico de força $F(t)$) e `NonlinearSolver` (P-Delta, Corrotacional 3D com Tangente Completa, Line Search, Newton-Raphson & Arc-Length), executados de forma assíncrona na GUI (`MainWindow`);
- **Viewport Gráfica Interativa em OpenGL 3.3 Core Profile:** Pipeline GLSL 330 com anti-aliasing SSAA/MSAA/Shader SDF (`RenderSettings`), câmera orbital 3D, Raycasting picking, diagramas contínuos de Hermite, **Planos de Corte 3D (Clip Plane)**, **contornos DCR e Tensão de von Mises (Casca/Sólido)**, **animação modal/deformada** (`PostProcessControlDialog`), plano padrão **`XY (Planta)`** nos 3 exemplos, janelas **modeless** sem contorno fantasma inicial (`WindowZOrderManager`) e setas vetoriais nativas de UI via `QProxyStyle` (`ThemeManager`);
- **Interoperabilidade, Conformidade & Relatórios Nativos:** Formato aberto **`.omniframe` JSON Schema `2.1.0`** (com `"surfaceMesh"` retrocompatível com `1.0` / `2.0`), troca bidirecional **DXF / IFC4** (`DxfInterop`, `IfcInterop` com caminhos UTF-8 em `Utf8Path.hpp`), exportação **VTK `UNSTRUCTURED_GRID`** 1D/2D/3D e **CSV** contínuo, auditoria **ABNT NBR** (`LocalPdfVerified`: `NBR 6118:2023`, `6120:2019`, `6122:2022`, `6123:2023`, `7480:2022`, `8800:2024`; `OutOfLocalPdfScope` explícito para normas ausentes), relatórios em **Markdown / HTML / PDF A4** (`PdfReportGenerator`) e CLI integrada **`omniframe-cli.exe`** (`source/cli/` + `source/os/WindowsCliIntegration`) com associação Windows 10/11 para CMD e PowerShell (`PATH`, `App Paths`, `.omniframe`, `register_cli_windows.ps1` e aliases MSIX).

## 3. Resumo Estrutural Consolidado do Código-Fonte

Estrutura validada na árvore de código-fonte `source/` e `tests/`:

| Camada | Subdiretório | Descrição Técnica e Responsabilidade |
|---|---|---|
| **Kernel** | `source/kernel/` | Biblioteca C++23 + Eigen3 sem Qt (`analysis/`, `assembly/`, `codes/`, `core/`, `elements/`, `geometry/`, `materials/`, `mesh/`, `postprocessing/`, `solver/`) |
| **Core** | `source/core/` | Ponte de modelo neutra à UI: `Node`, `Element`, `CrossSection`, `Material`, `StructuralModel` e `Types.hpp` |
| **Solver** | `source/solver/` | Orquestração conectando `StructuralModel` ao `OmniFrameKernel` (`FemSolver3D`) |
| **Render** | `source/render/` | Shaders GLSL 330 (`ShaderCollection`), câmera orbital `Camera3D`, `RenderSettings` e `Viewport3D` OpenGL 3.3 Core |
| **UI** | `source/ui/` | `MainWindow` (Qt 6.11.1), docks/tabelas, `Dialogs`, `EngineeringDialogs` (6 diálogos de engenharia), `GuidedSessionDialog` (Sessão Guiada `F2`), `ThemeManager`, `WindowZOrderManager`, `ManualDialog` e `AboutDialog` |
| **I/O** | `source/io/` | `ProjectSerializer` (`.omniframe` Schema `2.1.0`, relatórios HTML/MD), `DxfInterop`, `IfcInterop`, `Utf8Path` e `PdfReportGenerator` (PDF A4) |
| **OS Integration** | `source/os/` | `WindowsCliIntegration` (console UTF-8 CMD/PowerShell, `CommandLineToArgvW`, registro HKCU `PATH`/`App Paths`/`.omniframe` e delegação GUI->CLI) |
| **CLI** | `source/cli/` | Executável console integrado `omniframe-cli.exe` (`main_cli.cpp`) compartilhando `OmniFrameCore`, `OmniFrameKernel`, exemplos e auditoria NBR |
| **Testes Automatizados** | `tests/` | Suíte modularizada de **20 alvos CTest** (`test_Material`..`test_SpringsSettlementsAndExamples` + `test_Phase0ValidationAndBenchmarks`..`test_Phase9UiAndWorkflows`) |
| **Mocks & Ferramentas** | `mocks/` | Editor/auditor de versões (`VersionEditor`) e compiladores de tradução Qt (`CompileTranslationsGUI` / `CompileTranslationsTerminal`) |

## 4. Build, Toolchain e Dependências Reais

Dados validados no arquivo `CMakeLists.txt` e `CMakePresets.json`:
- Requisito de CMake: `cmake_minimum_required(VERSION 3.28)`;
- Padrão de linguagem: `C++23` (`set(CMAKE_CXX_STANDARD 23)` com `CMAKE_CXX_STANDARD_REQUIRED ON`);
- Recursos de automação Qt: `CMAKE_AUTOMOC ON`, `CMAKE_AUTORCC ON`, `CMAKE_AUTOUIC ON`;
- Toolchain oficial: **MSVC** (Visual Studio 2026 / Windows SDK x64) com otimização `/O2 /Oi /Ot` em Release e builds separados em `build/OmniFrame3D`, `build/Version`, `build/CompileLanguage` e `build/Testes`;
- Componentes Qt 6.11.1 vinculados: `Core`, `Gui`, `Widgets`, `OpenGL`, `OpenGLWidgets` (além de `LinguistTools` / `lrelease` para compilação `.ts` -> `.qm`);
- Álgebra linear: biblioteca Eigen 3.4.0 (`FetchContent` em modo header-only com `BUILD_TESTING OFF`).

## 5. Duplo Licenciamento Estruturado

O projeto adota uma fronteira estrita de duplo licenciamento:
1. **GitHub (Código Aberto):** Todo o repositório está sob a licença **GNU AGPLv3**, assegurando liberdade total para estudo, auditoria, compilações independentes, modificação e criação de forks com preservação do copyleft.
2. **Microsoft Store (Distribuição Oficial):** O pacote MSIX compilado, assinado e distribuído oficialmente pelo autor é disponibilizado gratuitamente sob o **EULA Oficial do Autor**, vedando a revenda comercial paga por terceiros e preservando a integridade da marca oficial (com Política de Privacidade própria independente da Microsoft e suporte prestado exclusivamente pelo desenvolvedor).

</details>

---

<h2 id="enus">🇺🇸 English (US)</h2>

<details>
<summary>Click to expand / collapse Technical Observation in English</summary>

# OMNIFRAME 3D - TECHNICAL OBSERVATION AND STRATEGIC POSITIONING

Documented Version: `2026.11.16.0`  
Technical Snapshot Date: `November 16, 2026`  
Repository: `OmniFrame_cpp`  
Author Identified in Project: `Fernando Nillsson Cidade`  
Repository License: `GNU AGPLv3 (GNU Affero General Public License v3)`  
Microsoft Store Edition License: `Author's Official EULA (Free Distribution)`  

## 1. Current Project State and Positioning

The **OmniFrame 3D** repository consolidates a scientific and enterprise structural engineering desktop platform **natively built in C++23 with Qt 6.11.1**, sparse linear algebra via **Eigen 3.4.0**, and 3D graphics acceleration in **OpenGL 3.3 Core Profile** (GLSL 330).

The product transcends conventional geometric viewers or CAD utilities, positioning itself as a **Local 3D Space Frame & Truss Finite Element Analysis (FEA) Suite**.

Its core value proposition directly answers the most critical question in civil and mechanical structural engineering design:
> **“What are the nodal displacements, axial stresses, and internal member forces (N, Vy, Vz, T, My, Mz) along each 3D element under realistic load combinations?”**

The product acts as a **100% Local-First** sovereign solution (no cloud, no telemetry, no account creation, no trackers, and no external service dependencies), operating with full independence and guaranteed analytical integrity.

## 2. Technical Line & Architecture Highlights

This version consolidates the separated C++23 computational kernel and multi-physics/engineering subsystems:

- **Pure C++23 + Eigen 3.4.0 Kernel (`OmniFrameKernel`):** Strictly Qt-free computational library implementing 1D/2D/3D finite elements (`BeamTruss1D`, `Corotational1D`, `ShellElements`, `SolidElements` with `mask3` DOFs and automatic node merging), linear elastic and **J2 Plasticity** constitutive models (`LinearElasticMaterial`, `J2PlasticityMaterial`), parametric mesh generation (`MeshGenerator`), and `NbrCodeChecks`;
- **Multi-Solver & Advanced Analyses:** Sparse `LinearSolverSuite` (`SimplicialLDLT` AMD, `SimplicialLLT`, `SparseLU`, `ConjugateGradient`, `BiCGSTAB` with tracked fallback), `BucklingSolver` (with direct initial geometric imperfection seeding), `DynamicSolver` (Modal, Harmonic FRF, Response Spectrum, and Newmark-$\beta$/HHT-$\alpha$ Transient/Seismic with force time-history $F(t)$), and `NonlinearSolver` (P-Delta, Corotational 3D with Full Tangent, Line Search, Newton-Raphson & Arc-Length), executed asynchronously in the GUI (`MainWindow`);
- **Interactive 3D Viewport in OpenGL 3.3 Core Profile:** Modern GLSL 330 shader pipeline with SSAA/MSAA/Shader SDF anti-aliasing (`RenderSettings`), 3D orbital camera, Raycasting picking, Hermite continuous diagrams, **3D Clip Planes**, **DCR & von Mises Stress (Shell/Solid) contours**, **modal/deformed animation** (`PostProcessControlDialog`), default **`XY (Plan)`** workplane for built-in examples, ghost-free **modeless** child windows (`WindowZOrderManager`), and vector UI combo/spin arrows painted natively via `QProxyStyle` (`ThemeManager`);
- **Interoperability, Compliance & Native Reports:** Open **`.omniframe` JSON Schema `2.1.0`** (with `"surfaceMesh"` backward-compatible with `1.0` / `2.0`), bidirectional **DXF / IFC4** (`DxfInterop`, `IfcInterop` with UTF-8 paths via `Utf8Path.hpp`), 1D/2D/3D **VTK `UNSTRUCTURED_GRID`** and continuum **CSV** export, **ABNT NBR** audit scope (`LocalPdfVerified`: `NBR 6118:2023`, `6120:2019`, `6122:2022`, `6123:2023`, `7480:2022`, `8800:2024`; `OutOfLocalPdfScope` explicitly flagged for absent standards), native **Markdown / HTML / A4 PDF** (`PdfReportGenerator`) reports, and integrated **`omniframe-cli.exe`** (`source/cli/` + `source/os/WindowsCliIntegration`) with Windows 10/11 CMD & PowerShell integration (`PATH`, `App Paths`, `.omniframe`, `register_cli_windows.ps1`, and MSIX console alias).

## 3. Consolidated Structural Source Tree Summary

Validated layout across `source/` and `tests/`:

| Layer | Subdirectory | Technical Description and Responsibility |
|---|---|---|
| **Kernel** | `source/kernel/` | Pure C++23 + Eigen3 library (`analysis/`, `assembly/`, `codes/`, `core/`, `elements/`, `geometry/`, `materials/`, `mesh/`, `postprocessing/`, `solver/`) |
| **Core** | `source/core/` | UI-neutral model bridge: `Node`, `Element`, `CrossSection`, `Material`, `StructuralModel`, and `Types.hpp` |
| **Solver** | `source/solver/` | Orchestration bridge connecting `StructuralModel` to `OmniFrameKernel` (`FemSolver3D`) |
| **Render** | `source/render/` | GLSL 330 shaders (`ShaderCollection`), orbital `Camera3D`, `RenderSettings`, and OpenGL 3.3 Core `Viewport3D` |
| **UI** | `source/ui/` | `MainWindow` (Qt 6.11.1, collapsible lateral `-90°` / bottom dock inspector and tables with spin-button toggles), `Dialogs`, `EngineeringDialogs` (6 engineering dialogs), `GuidedSessionDialog` (Guided Session `F2`), `ThemeManager` (QProxyStyle rounded spin buttons & vector arrows), `WindowZOrderManager`, `ManualDialog`, `AboutDialog` |
| **I/O** | `source/io/` | `ProjectSerializer` (`.omniframe` Schema `2.1.0`, HTML/MD reports), `DxfInterop`, `IfcInterop`, `Utf8Path`, and `PdfReportGenerator` (A4 PDF) |
| **OS Integration** | `source/os/` | `WindowsCliIntegration` (CMD/PowerShell UTF-8 console, `CommandLineToArgvW`, HKCU `PATH`/`App Paths`/`.omniframe` registration, and GUI->CLI forwarding) |
| **CLI** | `source/cli/` | Integrated native console executable `omniframe-cli.exe` (`main_cli.cpp`) sharing `OmniFrameCore`, `OmniFrameKernel`, examples, and NBR auditing |
| **Automated Tests** | `tests/` | Modularized suite of **20 CTest targets** (`test_Material`..`test_SpringsSettlementsAndExamples` plus `test_Phase0ValidationAndBenchmarks`..`test_Phase9UiAndWorkflows`) |
| **Tools** | `mocks/` | Version editor/auditor (`VersionEditor`) and Qt translation compilers (`CompileTranslationsGUI` / `CompileTranslationsTerminal`) |

## 4. Build, Toolchain & Real Dependencies

Validated configuration from `CMakeLists.txt` and `CMakePresets.json`:
- CMake requirement: `cmake_minimum_required(VERSION 3.28)`;
- Language standard: `C++23` (`set(CMAKE_CXX_STANDARD 23)` with `CMAKE_CXX_STANDARD_REQUIRED ON`);
- Qt automation features: `CMAKE_AUTOMOC ON`, `CMAKE_AUTORCC ON`, `CMAKE_AUTOUIC ON`;
- Official toolchain: **MSVC** (Visual Studio 2026 / Windows SDK x64) with `/O2 /Oi /Ot` Release optimization and separated builds in `build/OmniFrame3D`, `build/Version`, `build/CompileLanguage`, and `build/Testes`;
- Linked Qt 6.11.1 components: `Core`, `Gui`, `Widgets`, `OpenGL`, `OpenGLWidgets` (plus `LinguistTools` / `lrelease` for `.ts` -> `.qm` compilation; PDF generation in `PdfReportGenerator` uses Qt Gui `QPdfWriter`/`QPainter`);
- Linear algebra: Eigen 3.4.0 library (`FetchContent` in header-only mode with `BUILD_TESTING OFF`).

## 5. Structured Dual Licensing

The project adopts a strict dual-licensing boundary:
1. **GitHub (Open Source):** The entire repository is licensed under **GNU AGPLv3**, guaranteeing full freedom to study, audit, compile independently, modify, and fork while preserving copyleft.
2. **Microsoft Store (Official Distribution):** The MSIX package compiled, signed, and officially distributed by the author is provided free of charge under the **Author's Official EULA**, prohibiting paid commercial resale by third parties and preserving official brand integrity (with its own Privacy Policy independent from Microsoft and support provided exclusively by the developer).

</details>

---

<h2 id="tree">🌳 Árvore de Arquivos / File Tree</h2>

<details>
<summary>Clique para expandir / recolher a árvore de arquivos do projeto</summary>

```text
OmniFrame_cpp/
├── .gitattributes
├── .gitignore
├── CMakeLists.txt
├── CMakePresets.json
├── CODE_OF_CONDUCT.md
├── CONTRIBUTING.md
├── LICENSE
├── MANUAL.md
├── OBSERVACAO.md
├── README.md
├── rascunho.cpp
├── SECURITY.md
└── prompt_mestre_omniframe_3d.md

**Root Path:** `c:\Users\ferna\APLICATIVOS\CPP\OmniFrame3D_cpp\source`

```
├── 📁 assets
│   ├── 📁 ABOUT
│   │   ├── 📄 ABOUT_en_US.txt
│   │   ├── 📄 ABOUT_pt_BR.txt
│   │   ├── 📄 History_APP_en_US.txt
│   │   └── 📄 History_APP_pt_BR.txt
│   ├── 📁 CLC
│   │   ├── 📄 CLC_en_US.txt
│   │   └── 📄 CLC_pt_BR.txt
│   ├── 📁 COPYRIGHT
│   │   ├── 📄 AVISO DE COPYRIGHT E MARCA REGISTRADA_pt_BR.txt
│   │   └── 📄 COPYRIGHT AND TRADEMARK NOTICE_en_US.txt
│   ├── 📁 EULA
│   │   ├── 📄 EULA_en_US.txt
│   │   └── 📄 EULA_pt_BR.txt
│   ├── 📁 LICENSES
│   │   ├── 📄 Apache License Version 2.0.txt
│   │   ├── 📄 BSD License Clause-2.txt
│   │   ├── 📄 BSD License Clause-3.txt
│   │   ├── 📄 GNU AFFERO GENERAL PUBLIC LICENSE Version3.txt
│   │   ├── 📄 GNU GENERAL PUBLIC LICENSE Version 3.txt
│   │   ├── 📄 GNU LESSER GENERAL PUBLIC LICENSE Version 2.1.txt
│   │   ├── 📄 GNU LESSER GENERAL PUBLIC LICENSE Version 3.txt
│   │   ├── 📄 ISC LICENSE.txt
│   │   ├── 📄 Khronos OpenGL License.txt
│   │   ├── 📄 MIT License.txt
│   │   ├── 📄 Mozilla Public License Version 2.0.txt
│   │   ├── 📄 PYTHON SOFTWARE FOUNDATION LICENSE.txt
│   │   └── 📄 SQLite Is Public Domain.txt
│   ├── 📁 MANUAL
│   │   ├── 📄 MANUAL_en_US.txt
│   │   └── 📄 MANUAL_pt_BR.txt
│   ├── 📁 NOTICES
│   │   ├── 📄 NOTICE_en_US.txt
│   │   └── 📄 NOTICE_pt_BR.txt
│   ├── 📁 PRIVACY_POLICY
│   │   ├── 📄 Privacy_Policy_en_US.txt
│   │   └── 📄 Privacy_Policy_pt_BR.txt
│   ├── 📁 RELEASE
│   │   ├── 📄 RELEASE NOTES_en_US.txt
│   │   └── 📄 RELEASE NOTES_pt_BR.txt
│   └── 📁 icons
│       ├── 📄 omniframe.ico
│       ├── 🖼️ omniframe.png
│       ├── 🖼️ omniframe_1080-1080.png
│       ├── 🖼️ omniframe_1440-2160.png
│       ├── 🖼️ omniframe_150-150.png
│       ├── 🖼️ omniframe_1920-1080.png
│       ├── 🖼️ omniframe_2160-2160.png
│       ├── 🖼️ omniframe_300-300.png
│       ├── 🖼️ omniframe_3840-2160.png
│       ├── 🖼️ omniframe_584-800.png
│       ├── 🖼️ omniframe_71-71.png
├── 📁 cli
│   └── ⚡ main_cli.cpp
├── 📁 core
│   ├── ⚡ CrossSection.cpp
│   ├── ⚡ CrossSection.hpp
│   ├── ⚡ Element.cpp
│   ├── ⚡ Element.hpp
│   ├── ⚡ Material.hpp
│   ├── ⚡ Node.hpp
│   ├── ⚡ StructuralModel.cpp
│   ├── ⚡ StructuralModel.hpp
│   └── ⚡ Types.hpp
├── 📁 io
│   ├── ⚡ DxfInterop.cpp
│   ├── ⚡ DxfInterop.hpp
│   ├── ⚡ IfcInterop.cpp
│   ├── ⚡ IfcInterop.hpp
│   ├── ⚡ PdfReportGenerator.cpp
│   ├── ⚡ PdfReportGenerator.hpp
│   ├── ⚡ ProjectSerializer.cpp
│   ├── ⚡ ProjectSerializer.hpp
│   └── ⚡ Utf8Path.hpp
├── 📁 os
│   ├── ⚡ WindowsCliIntegration.cpp
│   └── ⚡ WindowsCliIntegration.hpp
├── 📁 kernel
│   ├── 📁 analysis
│   │   ├── ⚡ BucklingSolver.cpp
│   │   ├── ⚡ BucklingSolver.hpp
│   │   ├── ⚡ DynamicSolver.cpp
│   │   ├── ⚡ DynamicSolver.hpp
│   │   ├── ⚡ LoadSystem.cpp
│   │   ├── ⚡ LoadSystem.hpp
│   │   ├── ⚡ NonlinearSolver.cpp
│   │   └── ⚡ NonlinearSolver.hpp
│   ├── 📁 assembly
│   │   ├── ⚡ GlobalAssembler.cpp
│   │   └── ⚡ GlobalAssembler.hpp
│   ├── 📁 codes
│   │   ├── ⚡ NbrCodeChecks.cpp
│   │   └── ⚡ NbrCodeChecks.hpp
│   ├── 📁 core
│   │   ├── ⚡ DofManager.cpp
│   │   ├── ⚡ DofManager.hpp
│   │   └── ⚡ KernelTypes.hpp
│   ├── 📁 elements
│   │   ├── ⚡ BeamTruss1D.cpp
│   │   ├── ⚡ BeamTruss1D.hpp
│   │   ├── ⚡ Corotational1D.cpp
│   │   ├── ⚡ Corotational1D.hpp
│   │   ├── ⚡ IFiniteElement.hpp
│   │   ├── ⚡ ShellElements.cpp
│   │   ├── ⚡ ShellElements.hpp
│   │   ├── ⚡ SolidElements.cpp
│   │   └── ⚡ SolidElements.hpp
│   ├── 📁 geometry
│   │   ├── ⚡ CrossSectionGeometry.cpp
│   │   └── ⚡ CrossSectionGeometry.hpp
│   ├── 📁 materials
│   │   ├── ⚡ IConstitutiveModel.hpp
│   │   ├── ⚡ J2PlasticityMaterial.cpp
│   │   ├── ⚡ J2PlasticityMaterial.hpp
│   │   ├── ⚡ LinearElasticMaterial.cpp
│   │   └── ⚡ LinearElasticMaterial.hpp
│   ├── 📁 mesh
│   │   ├── ⚡ MeshGenerator.cpp
│   │   ├── ⚡ MeshGenerator.hpp
│   │   └── ⚡ MeshPrimitives.hpp
│   ├── 📁 postprocessing
│   │   ├── ⚡ PostProcessor.cpp
│   │   └── ⚡ PostProcessor.hpp
│   └── 📁 solver
│       ├── ⚡ LinearSolverSuite.cpp
│       └── ⚡ LinearSolverSuite.hpp
├── 📁 locale
│   ├── 📁 language
│   │   ├── 📄 omniframe_en_US.qm
│   │   ├── 📄 omniframe_en_US.ts
│   │   ├── 📄 omniframe_pt_BR.qm
│   │   └── 📄 omniframe_pt_BR.ts
│   ├── ⚡ GerenciadorTraducao.cpp
│   └── ⚡ GerenciadorTraducao.hpp
├── 📁 render
│   ├── ⚡ Camera3D.cpp
│   ├── ⚡ Camera3D.hpp
│   ├── ⚡ RenderSettings.cpp
│   ├── ⚡ RenderSettings.hpp
│   ├── ⚡ ShaderCollection.hpp
│   ├── ⚡ Viewport3D.cpp
│   └── ⚡ Viewport3D.hpp
├── 📁 solver
│   ├── ⚡ FemSolver3D.cpp
│   └── ⚡ FemSolver3D.hpp
├── 📁 ui
│   ├── ⚡ AboutDialog.cpp
│   ├── ⚡ AboutDialog.hpp
│   ├── ⚡ Dialogs.cpp
│   ├── ⚡ Dialogs.hpp
│   ├── ⚡ EngineeringDialogs.cpp
│   ├── ⚡ EngineeringDialogs.hpp
│   ├── ⚡ GuidedSessionDialog.cpp
│   ├── ⚡ GuidedSessionDialog.hpp
│   ├── ⚡ MainWindow.cpp
│   ├── ⚡ MainWindow.hpp
│   ├── ⚡ ManualDialog.cpp
│   ├── ⚡ ManualDialog.hpp
│   ├── ⚡ RenderSettingsDialog.cpp
│   ├── ⚡ RenderSettingsDialog.hpp
│   ├── ⚡ ThemeManager.cpp
│   ├── ⚡ ThemeManager.hpp
│   ├── ⚡ WindowZOrderManager.cpp
│   └── ⚡ WindowZOrderManager.hpp
└── ⚡ main.cpp
```

**Root Path:** `c:\Users\ferna\APLICATIVOS\CPP\OmniFrame3D_cpp\tests`

```
├── 📄 CMakeLists.txt
├── ⚡ TestHelper.hpp
├── ⚡ test_CantileverBenchmark.cpp
├── ⚡ test_CoordinateTransform.cpp
├── ⚡ test_CrossSection.cpp
├── ⚡ test_DistributedAndInternalForces.cpp
├── ⚡ test_Element3D.cpp
├── ⚡ test_EquilibriumReactions.cpp
├── ⚡ test_Material.cpp
├── ⚡ test_Phase0ValidationAndBenchmarks.cpp
├── ⚡ test_Phase1KernelAndSolvers.cpp
├── ⚡ test_Phase2SectionsAnd1D.cpp
├── ⚡ test_Phase3NonlinearAndBuckling.cpp
├── ⚡ test_Phase4Dynamics.cpp
├── ⚡ test_Phase5Shells.cpp
├── ⚡ test_Phase6Solids.cpp
├── ⚡ test_Phase7MeshInteropCli.cpp
├── ⚡ test_Phase8ComplianceAndReports.cpp
├── ⚡ test_Phase9UiAndWorkflows.cpp
├── ⚡ test_ProjectSerializer.cpp
├── ⚡ test_ReleasesAndThermal.cpp
└── ⚡ test_SpringsSettlementsAndExamples.cpp
```

**Root Path:** `c:\Users\ferna\APLICATIVOS\CPP\OmniFrame3D_cpp\repo\LISTAGEM`

```
├── 📁 en_US
│   ├── 🖼️ Captura de tela 2026-11-14 013358.png
│   └── 🖼️ Captura de tela 2026-11-14 013412.png
└── 📁 pt_BR
    ├── 🖼️ Captura de tela 2026-11-14 013311.png
    └── 🖼️ Captura de tela 2026-11-14 013333.png
```

**Root Path:** `c:\Users\ferna\APLICATIVOS\CPP\OmniFrame3D_cpp\mocks`

```
├── 📁 icons
│   ├── 📄 omniframe.ico
│   ├── 🖼️ omniframe.png
│   ├── 🖼️ omniframe_1080-1080.png
│   ├── 🖼️ omniframe_1440-2160.png
│   ├── 🖼️ omniframe_150-150.png
│   ├── 🖼️ omniframe_1920-1080.png
│   ├── 🖼️ omniframe_2160-2160.png
│   ├── 🖼️ omniframe_300-300.png
│   ├── 🖼️ omniframe_3840-2160.png
│   ├── 🖼️ omniframe_584-800.png
│   └── 🖼️ omniframe_71-71.png
├── 📄 CMakeLists.txt
├── ⚡ CompileTranslationsGUI.cpp
├── ⚡ CompileTranslationsGUI.hpp
├── ⚡ CompileTranslationsTerminal.cpp
├── ⚡ CompileTranslationsTerminal.hpp
├── ⚡ VersionEditor.cpp
└── ⚡ VersionEditor.hpp
```

**Root Path:** `c:\Users\ferna\APLICATIVOS\CPP\OmniFrame3D_cpp\docs`

```
├── 📁 images
│   ├── 🖼️ banner.png
│   └── 🖼️ logo.png
├── 📝 architecture.md
├── 📝 cli-reference.md
├── 📝 compliance.md
├── 📝 licensing.md
├── 📝 privacy-policy.md
├── 📝 release-v2026.11.16.0.md
├── 📝 security-model.md
├── 📝 suggested-issues.md
├── 📝 technical-audit-and-roadmap.md
└── 📝 user-guide.md
```

**Root Path:** `c:\Users\ferna\APLICATIVOS\CPP\OmniFrame3D_cpp\.github`

```
├── 📁 ISSUE_TEMPLATE
│   ├── 📝 bug_report.md
│   ├── ⚙️ config.yml
│   ├── 📝 feature_request.md
│   └── 📝 question.md
├── 📁 workflows
│   └── ⚙️ update-readme-version.yml
└── 📝 PULL_REQUEST_TEMPLATE.md
```

```

</details>
