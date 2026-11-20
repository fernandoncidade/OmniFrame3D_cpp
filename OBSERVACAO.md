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

Versão documentada: `2026.11.13.0`  
Data do snapshot técnico: `13 de novembro de 2026`  
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

## 2. Linha Técnica e Destaques da Versão 2026.11.13.0

Esta versão consolida a maturidade técnica da engine de cálculo e a estabilização completa dos subsistemas analíticos:

- **Formulação Mecânica Exata de Viga-Coluna (Euler-Bernoulli):** Modelagem de barras com 12 graus de liberdade (6 DOFs por nó: $u_x, u_y, u_z, \theta_x, \theta_y, \theta_z$). A matriz de rigidez local $12 \times 12$ captura simultaneamente a resposta axial ($EA/L$), torção pura de Saint-Venant ($GJ/L$) e flexão biaxial acoplada com cortante em ambos os eixos principais locais ($EI_z/L^3, EI_y/L^3$);
- **Transformação Espacial Tridimensional com Ângulo de Rolamento ($\psi$):** Matriz ortonormal $[T]$ de dimensões $12 \times 12$ com cálculo rigoroso de cossenos diretores e suporte à rotação angular dos eixos de inércia da seção transversal;
- **Reconstrução Contínua via Polinômios Cúbicos de Hermite:** Interpolação analítica contínua de deslocamentos transversais e diagramas de momentos fletores e esforços cortantes ao longo de qualquer seção $x' \in [0, L]$ do elemento;
- **Vetores Consistentes de Forças Nodais Equivalentes:** Integração estrita de trabalhos virtuais para cargas uniformemente distribuídas sobre barras e carregamentos térmicos, além de forças/momentos nodais concentrados, molas elásticas e deslocamentos prescritos;
- **Solver Direto Esparso de Cholesky (`Eigen::SimplicialLDLT`):** Resolução direta de alta performance da matriz global de rigidez esparsa $[K] \{U\} = \{F\}$, com ordenação simbólica de grau mínimo aproximado (AMD) e detecção instantânea de singularidades numéricas ou mecanismos estruturais cinemáticos;
- **Viewport Gráfica Interativa em OpenGL 3.3 Core Profile:** Pipeline moderno baseado em shaders GLSL 330 com iluminação de Phong, buffers VAO/VBO/EBO otimizados, câmera orbital 3D com navegação suave por mouse e algoritmo matemático de seleção por Raycasting (*Eye-to-World ray cast*);
- **Visualização Contínua da Deformada e Mapas de Cores:** Exibição sobreposta da estrutura indeformada (aramado neutro) e da deformada elástica com controle contínuo de escala (slider `1` a `100`, padrão `30`, ampliação de `1,5x` a `150x` na deformada e escala `val / 30.0` nos diagramas), acompanhada de gradiente espectral de cores para deslocamentos e esforços internos;
- **Persistência em Formato Aberto `.omniframe` (JSON):** Salvamento e abertura estruturada com validação de números finitos e compatibilidade universal com controle de versão em Git. Exportação de relatórios técnicos em HTML e Markdown.

## 3. Resumo Estrutural Consolidado do Código-Fonte

Contagem validada na árvore de código-fonte `source/` e `tests/`:

| Camada | Subdiretório | Descrição Técnica e Responsabilidade |
|---|---|---|
| **Core** | `source/core/` | Modelo de dados neutro: `Node`, `Element`, `CrossSection`, `Material`, `StructuralModel` e primitivas `Types.hpp` |
| **Solver** | `source/solver/` | Montagem da rigidez global, forças nodais equivalentes, aplicação de condições de contorno e solver esparso `FemSolver3D` |
| **Render** | `source/render/` | Shaders GLSL 330 (`ShaderCollection`), câmera 3D orbital (`Camera3D`) e viewport OpenGL 3.3 Core (`Viewport3D`) com Raycasting |
| **UI** | `source/ui/` | Janela principal `MainWindow` com sistema de docking Qt 6, inspetores de propriedades e diálogos de edição modal |
| **I/O** | `source/io/` | Serialização e desserialização robusta de projetos em formato aberto `.omniframe` JSON e exportação em HTML/Markdown (`ProjectSerializer`) |
| **Testes Automatizados** | `tests/` | Suíte modularizada de 7 baterias de testes e benchmarks analíticos com CTest (`test_Material`, `test_CrossSection`, `test_Element3D`, `test_CoordinateTransform`, `test_CantileverBenchmark`, `test_EquilibriumReactions`, `test_ProjectSerializer`) |
| **Mocks & Ferramentas** | `mocks/` | Editor/auditor de versões (`VersionEditor`), compiladores de tradução Qt (`CompileTranslationsGUI` / `CompileTranslationsTerminal`) e scripts de apoio |

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

Documented Version: `2026.11.13.0`  
Technical Snapshot Date: `November 13, 2026`  
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

## 2. Technical Line & Highlights of Version 2026.11.13.0

This version consolidates the technical maturity of the calculation engine and full stabilization of analytical subsystems:

- **Exact Euler-Bernoulli Beam-Column Formulation:** Modeling of 12-DOF members (6 DOFs per node: $u_x, u_y, u_z, \theta_x, \theta_y, \theta_z$). The local $12 \times 12$ stiffness matrix simultaneously captures axial response ($EA/L$), Saint-Venant pure torsion ($GJ/L$), and coupled biaxial bending with shear along both principal local axes ($EI_z/L^3, EI_y/L^3$);
- **3D Spatial Transformation with Roll Angle ($\psi$):** Orthonormal $12 \times 12$ matrix $[T]$ with rigorous directional cosines calculation and support for angular rotation of cross-section principal axes of inertia;
- **Continuous Reconstruction via Hermite Cubic Polynomials:** Continuous analytical interpolation of transverse displacements and bending moment / shear force diagrams along any member coordinate $x' \in [0, L]$;
- **Consistent Equivalent Nodal Force Vectors:** Strict virtual work integration for uniform distributed member loads and thermal loads, alongside concentrated nodal forces/moments, elastic springs, and prescribed displacements;
- **Direct Sparse Cholesky Solver (`Eigen::SimplicialLDLT`):** High-performance direct solve of the global sparse stiffness matrix $[K] \{U\} = \{F\}$, featuring Approximate Minimum Degree (AMD) symbolic ordering and instant detection of numerical singularities or kinematic mechanisms;
- **Interactive 3D Viewport in OpenGL 3.3 Core Profile:** Modern GLSL 330 shader-based pipeline with Phong lighting, optimized VAO/VBO/EBO buffers, 3D orbital camera with smooth mouse navigation, and mathematical spatial Raycasting picking algorithm (*Eye-to-World ray cast*);
- **Continuous Deformed Shape Visualization and Color Maps:** Superimposed display of the undeformed geometry (neutral wireframe) and elastic deformed shape with continuous scaling factor control (slider `1` to `100`, default `30`, mapping to `1.5x`–`150x` deformed amplification and `val / 30.0` diagram scale), accompanied by spectral color gradients for displacements and internal actions;
- **Open `.omniframe` (JSON) Persistence:** Structured saving and opening with numerical finiteness validation and universal Git version control compatibility. Technical report export in HTML and Markdown.

## 3. Consolidated Structural Source Tree Summary

Validated count across `source/` and `tests/` source trees:

| Layer | Subdirectory | Technical Description and Responsibility |
|---|---|---|
| **Core** | `source/core/` | UI-neutral data model: `Node`, `Element`, `CrossSection`, `Material`, `StructuralModel`, and `Types.hpp` primitives |
| **Solver** | `source/solver/` | Global stiffness assembly, equivalent nodal forces, boundary conditions application, and sparse `FemSolver3D` solver |
| **Render** | `source/render/` | GLSL 330 shaders (`ShaderCollection`), orbital 3D camera (`Camera3D`), and OpenGL 3.3 Core viewport (`Viewport3D`) with Raycasting |
| **UI** | `source/ui/` | Main `MainWindow` with Qt 6 docking system, property inspectors, and modal editing dialogs |
| **I/O** | `source/io/` | Resilient project JSON serialization and deserialization in open `.omniframe` format and HTML/Markdown export (`ProjectSerializer`) |
| **Automated Tests** | `tests/` | Modularized suite of 7 CTest analytical test suites and benchmarks (`test_Material`, `test_CrossSection`, `test_Element3D`, `test_CoordinateTransform`, `test_CantileverBenchmark`, `test_EquilibriumReactions`, `test_ProjectSerializer`) |
| **Mocks & Tools** | `mocks/` | Version editor/auditor (`VersionEditor`), Qt translation compilers (`CompileTranslationsGUI` / `CompileTranslationsTerminal`), and helper scripts |

## 4. Build, Toolchain & Real Dependencies

Validated configuration from `CMakeLists.txt` and `CMakePresets.json`:
- CMake requirement: `cmake_minimum_required(VERSION 3.28)`;
- Language standard: `C++23` (`set(CMAKE_CXX_STANDARD 23)` with `CMAKE_CXX_STANDARD_REQUIRED ON`);
- Qt automation features: `CMAKE_AUTOMOC ON`, `CMAKE_AUTORCC ON`, `CMAKE_AUTOUIC ON`;
- Official toolchain: **MSVC** (Visual Studio 2026 / Windows SDK x64) with `/O2 /Oi /Ot` Release optimization and separated builds in `build/OmniFrame3D`, `build/Version`, `build/CompileLanguage`, and `build/Testes`;
- Linked Qt 6.11.1 components: `Core`, `Gui`, `Widgets`, `OpenGL`, `OpenGLWidgets` (plus `LinguistTools` / `lrelease` for `.ts` -> `.qm` compilation);
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
├── prompt_mestre_omniframe_3d.md
├── .github/
│   ├── PULL_REQUEST_TEMPLATE.md
│   ├── ISSUE_TEMPLATE/
│   │   ├── bug_report.md
│   │   ├── config.yml
│   │   ├── feature_request.md
│   │   └── question.md
│   └── workflows/
│       ├── ci.yml
│       └── update-readme-version.yml
├── .vscode/
│   ├── c_cpp_properties.json
│   ├── cmake-kits.json
│   ├── settings.json
│   └── tasks.json
├── cmake/
│   ├── deploy_optional_qt_binary.cmake
│   ├── generate_icons.ps1
│   ├── gerar_distribuicoes_limpas.ps1
│   ├── run_mingw.bat
│   ├── run_msvc.bat
│   └── windows_app_icon.rc.in
├── docs/
│   ├── architecture.md
│   ├── cli-reference.md
│   ├── compliance.md
│   ├── licensing.md
│   ├── privacy-policy.md
│   ├── release-v2026.11.13.0.md
│   ├── security-model.md
│   ├── suggested-issues.md
│   ├── user-guide.md
│   └── images/
│       ├── banner.png
│       └── logo.png
├── mocks/
│   ├── CMakeLists.txt
│   ├── CompileTranslationsGUI.cpp
│   ├── CompileTranslationsGUI.hpp
│   ├── CompileTranslationsTerminal.cpp
│   ├── CompileTranslationsTerminal.hpp
│   ├── INSTRUCOES_find_VersionEditor.txt
│   ├── launch_detached_gui.cmake
│   ├── VersionEditor.cpp
│   ├── VersionEditor.hpp
│   └── icons/
├── repo/
│   ├── AppxManifest.xml
│   ├── emojis.md
│   ├── limpar_artefatos_regeneraveis_qt.ps1
│   ├── MANUAL_TECNICO_CONFIGURACAO_AMBIENTE_CPP_QT6.txt
│   ├── OmniFrame_cpp.txt
│   ├── OmniFrame.iss
│   ├── OmniFrame_MinGW.iss
│   ├── OmniFrame_MSVC.iss
│   └── LISTAGEM/
│       ├── en_US/
│       └── pt_BR/
├── source/
│   └── assets/
│       ├── ABOUT/
│       │   ├── ABOUT_en_US.txt
│       │   ├── ABOUT_pt_BR.txt
│       │   ├── History_APP_en_US.txt
│       │   └── History_APP_pt_BR.txt
│       ├── CLC/
│       │   ├── CLC_en_US.txt
│       │   └── CLC_pt_BR.txt
│       ├── COPYRIGHT/
│       │   ├── AVISO DE COPYRIGHT E MARCA REGISTRADA_pt_BR.txt
│       │   └── COPYRIGHT AND TRADEMARK NOTICE_en_US.txt
│       ├── EULA/
│       │   ├── EULA_en_US.txt
│       │   └── EULA_pt_BR.txt
│       ├── LICENSES/
│       │   ├── Apache License Version 2.0.txt
│       │   ├── BSD License Clause-2.txt
│       │   ├── BSD License Clause-3.txt
│       │   ├── GNU AFFERO GENERAL PUBLIC LICENSE Version3.txt
│       │   ├── GNU GENERAL PUBLIC LICENSE Version 3.txt
│       │   ├── GNU LESSER GENERAL PUBLIC LICENSE Version 2.1.txt
│       │   ├── GNU LESSER GENERAL PUBLIC LICENSE Version 3.txt
│       │   ├── ISC LICENSE.txt
│       │   ├── Khronos OpenGL License.txt
│       │   ├── MIT License.txt
│       │   ├── Mozilla Public License Version 2.0.txt
│       │   ├── PYTHON SOFTWARE FOUNDATION LICENSE.txt
│       │   └── SQLite Is Public Domain.txt
│       ├── NOTICES/
│       │   ├── NOTICE_en_US.txt
│       │   └── NOTICE_pt_BR.txt
│       ├── PRIVACY_POLICY/
│       │   ├── Privacy_Policy_en_US.txt
│       │   └── Privacy_Policy_pt_BR.txt
│       ├── RELEASE/
│       │   ├── RELEASE NOTES_en_US.txt
│       │   └── RELEASE NOTES_pt_BR.txt
│       └── icons/
├── source/
│   ├── main.cpp
│   ├── assets/
│   ├── core/
│   │   ├── CrossSection.cpp
│   │   ├── CrossSection.hpp
│   │   ├── Element.cpp
│   │   ├── Element.hpp
│   │   ├── Material.hpp
│   │   ├── Node.hpp
│   │   ├── StructuralModel.cpp
│   │   ├── StructuralModel.hpp
│   │   └── Types.hpp
│   ├── io/
│   │   ├── ProjectSerializer.cpp
│   │   └── ProjectSerializer.hpp
│   ├── render/
│   │   ├── Camera3D.cpp
│   │   ├── Camera3D.hpp
│   │   ├── ShaderCollection.hpp
│   │   ├── Viewport3D.cpp
│   │   └── Viewport3D.hpp
│   ├── solver/
│   │   ├── FemSolver3D.cpp
│   │   └── FemSolver3D.hpp
│   └── ui/
│       ├── Dialogs.cpp
│       ├── Dialogs.hpp
│       ├── MainWindow.cpp
│       └── MainWindow.hpp
└── tests/
    ├── CMakeLists.txt
    ├── TestHelper.hpp
    ├── test_CantileverBenchmark.cpp
    ├── test_CoordinateTransform.cpp
    ├── test_CrossSection.cpp
    ├── test_Element3D.cpp
    ├── test_EquilibriumReactions.cpp
    ├── test_Material.cpp
    └── test_ProjectSerializer.cpp
```

</details>
