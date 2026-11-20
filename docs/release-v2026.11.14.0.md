# Release Notes — OmniFrame 3D v2026.11.14.0

<p align="center">
  <b>Selecione o idioma / Select language:</b><br>
  <a href="#ptbr">🇧🇷 Português (BR)</a> | <a href="#enus">🇺🇸 English (US)</a>
</p>

---

## <a id="ptbr"></a>🇧🇷 Português (BR)

<details>
<summary>Clique para expandir / recolher notas de lançamento em português</summary><br>

# OmniFrame 3D — Versão 2026.11.14.0 (Release Inicial)

**Data de Lançamento:** 14 de novembro de 2026  
**Autor:** Fernando Nillsson Cidade (`linceu_lighthouse@outlook.com`)  
**Repositório:** `fernandoncidade/OmniFrame_cpp`  
**Licenciamento:** GNU AGPLv3 (Código Aberto) / EULA (Microsoft Store)  

---

## Principais Recursos e Entregas

### 1. Núcleo Mecânico (`OmniFrameKernel`), Multi-Física e Normas Técnicas
- **Elementos Finitos 1D, 2D e 3D:** Viga-coluna espacial Euler-Bernoulli (12 DOFs/barra) e formulação de **Timoshenko** com deformação por cisalhamento, **corrotacional 3D** para grandes rotações, elementos de **casca/placa 2D** (DKQ/DSG/MITC4) e **sólidos 3D** hexaédricos (8 e 20 nós).
- **Materiais Lineares e Elastoplásticos:** Modelos elásticos lineares isotrópicos com expansão térmica ($\Delta T$) e **Plasticidade J2** com encruamento isotrópico e cinemático ($\sigma_y, H_{iso}, H_{kin}$).
- **Carregamentos, Combinações e Geração de Malha:** Forças/momentos nodais, cargas distribuídas globais/locais, ações **térmicas ($\Delta T$)**, molas elásticas, deslocamentos prescritos, gerador de malha paramétrica 1D/2D/3D (incluindo triangulação de Delaunay) e assistentes de combinações normativas (**ABNT NBR 8681 / 6120 / 6123**).
- **Suíte Completa de Solvers:** Estático linear (`LDLT`, `LLT`, `SparseLU`, `CG`, `BiCGSTAB`), **Flambagem Linear**, **Análise Modal** (autovalores e modos de vibrar), **Harmônica (FRF)**, **Espectro de Resposta Sísmica** e **Não Linear / P-Delta** (Newton-Raphson com controle de carga e Arco-Comprimento / Arc-Length).
- **Auditoria Normativa ABNT NBR:** Verificações automatizadas **NBR 8800**, **NBR 6118**, **NBR 8681**, **NBR 6120** e **NBR 6123** com rastreabilidade explícita de cobertura (`LocalPdfVerified` / `OutOfLocalPdfScope`).

### 2. Viewport 3D Interativa, Pipeline OpenGL 3.3 Core e Pós-Processamento
- **Shaders GLSL 330 e Perfis Gráficos:** Pipeline acelerado por hardware com iluminação difusa/especular, perfis de qualidade persistidos em `graphics.json` e suporte dinâmico a temas visuais (Claro, Escuro e Alto Contraste em `theme.json`).
- **Navegação Orbital e Raycasting 3D:** Arraste com botão direito/meio para orbitar, `Shift` + arraste para pan, roda do mouse para zoom suave e seleção espacial precisa de nós/barras.
- **Pós-Processamento Avançado:** Deformada 3D amplificada em tempo real (`1`–`100`, padrão `30`), diagramas espectrais (`N`, `Vy`, `Vz`, `Tx`, `My`, `Mz`), reações de apoio, **Planos de Corte 3D (Clip Planes)** em X/Y/Z, **Mapa de Calor DCR** (*Demand-to-Capacity Ratio*) e **animação** de modos de vibração e deformadas.

### 3. Interface Desktop Qt 6.11.1, Reedição Completa e Sessão Guiada (`F2`)
- **Janelas Filhas Modeless e Z-Order Estável:** Abertura instantânea por **clique único** sem contornos fantasmas transitórios, comportamento não-modal (*modeless*) que mantém janelas filhas acima da janela principal sem bloquear a viewport e alternância livre de foco.
- **Inspetor Lateral (`Elemento / Nó Selecionado`) e Reedição dos 3 Exemplos:** Listas suspensas diretas **`Material:`** e **`Seção Transversal:`** exibindo nomes limpos (sem prefixo `#ID`), seleção em lote (**`Mesmo Material`** / **`Mesma Seção`**), histórico **`Desfazer`/`Refazer` (`Ctrl+Z`/`Ctrl+Y`)** e permissão total para editar, recalcular e experimentar alterações nos **3 exemplos pré-definidos** como exercício prático.
- **Sessão Guiada Interativa (`Ajuda > Sessão Guiada...`, `F2`):** Tour automático passo a passo em 30 etapas cobrindo modelagem, edição, carregamentos, análise, pós-processamento e exportação sobre qualquer um dos 3 exemplos, com **4 perfis cognitivos de leitura (PPM)**, destaque visual sobre o controle ativo acompanhado de um **balão unificado em duas fases** (amarelo preparatório e verde de ação, ambos proporcionais à contagem de palavras e posicionados fora da área das janelas filhas no canto inferior esquerdo), adaptação automática ao tema/idioma em tempo de execução e **restauração automática** do exemplo ao estado inicial após a conclusão.
- **Interoperabilidade e Relatórios:** Persistência aberta em JSON `.omniframe` (Schema `2.1.0`, retrocompatível com `1.0` e `2.0`), importação/exportação **DXF** e **BIM IFC4**, além de memoriais de cálculo em **HTML**, **Markdown** e **PDF Executivo A4**.

### 4. CLI Headless (`omniframe-cli.exe`), Build, Distribuição e Testes Automatizados
- **Compilação e Empacotamento Completo:** O preset `omniframe-msvc-release` e as tarefas do VS Code (**`Qt6: Compilar omniframe-cli (MSVC)`** e **`Qt6: Compilar e Executar OmniFrame 3D (MSVC)`**) compilam `OmniFrame.exe` e `omniframe-cli.exe` e distribuem ambos via `cmake/gerar_distribuicoes_limpas.ps1` para `D:\MISCELANEAS\VSCode\OmniFrame3D\`.
- **Verificação Automatizada (`20` suítes CTest):** Presets em CMake (`CMakePresets.json`) cobrindo `test_Phase0` a `test_Phase9UiAndWorkflows` via `ctest --preset testes-msvc-release --output-on-failure`.
- **Conformidade Microsoft Store / Desinstalação Limpa:** Manifesto MSIX (`repo/AppxManifest.xml`), instalador Inno Setup (`repo/OmniFrame.iss`), Política de Privacidade 100% offline (`docs/privacy-policy.md`), divulgação de `runFullTrust` e limpeza de preferências locais (`language.json`, `theme.json`, `graphics.json` em `%LOCALAPPDATA%\OmniFrame\OmniFrame\`).

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details>
<summary>Click to expand / collapse release notes in English</summary><br>

# OmniFrame 3D — Version 2026.11.14.0 (Initial Release)

**Release Date:** November 14, 2026  
**Author:** Fernando Nillsson Cidade (`linceu_lighthouse@outlook.com`)  
**Repository:** `fernandoncidade/OmniFrame_cpp`  
**Licensing:** GNU AGPLv3 (Open Source) / EULA (Microsoft Store)  

---

## Major Features & Highlights

### 1. Mechanical Core (`OmniFrameKernel`), Multi-Physics & Engineering Codes
- **1D, 2D & 3D Finite Elements:** 3D Euler-Bernoulli beam-column (12 DOFs/member) and **Timoshenko** shear-deformable formulation, **3D corotational** large-rotation elements, **2D shell/plate** elements (DKQ/DSG/MITC4), and **3D solid** hexahedra (8-node and 20-node).
- **Linear Elastic & Elastoplastic Materials:** Isotropic linear-elastic models with thermal expansion ($\Delta T$) and **J2 Plasticity** with isotropic and kinematic hardening ($\sigma_y, H_{iso}, H_{kin}$).
- **Loads, Combinations & Parametric Meshing:** Nodal forces/moments, global/local distributed member loads, **thermal loads ($\Delta T$)**, elastic springs, prescribed displacements, 1D/2D/3D mesh generation (including Delaunay triangulation), and code-based combination wizards (**ABNT NBR 8681 / 6120 / 6123**).
- **Comprehensive Solver Suite:** Linear static (`LDLT`, `LLT`, `SparseLU`, `CG`, `BiCGSTAB`), **Linear Buckling**, **Modal Analysis** (eigenfrequencies and mode shapes), **Harmonic FRF**, **Seismic Response Spectrum**, and **Nonlinear / P-Delta** (load-controlled Newton-Raphson and Crisfield Arc-Length).
- **ABNT NBR Code Audit:** Automated checks for **NBR 8800**, **NBR 6118**, **NBR 8681**, **NBR 6120**, and **NBR 6123** with explicit coverage traceability (`LocalPdfVerified` / `OutOfLocalPdfScope`).

### 2. Interactive 3D Viewport, OpenGL 3.3 Core Pipeline & Post-Processing
- **GLSL 330 Shaders & Graphics Profiles:** Hardware-accelerated rendering with Phong lighting, persistent quality profiles (`graphics.json`), and runtime visual theme switching (Light, Dark, and High Contrast in `theme.json`).
- **Orbital Camera & 3D Raycasting:** Right/middle drag to orbit, `Shift` + drag to pan, scroll wheel for smooth zoom, and high-precision 3D picking of nodes and members.
- **Advanced Post-Processing:** Real-time amplified 3D deformed shape (`1`–`100`, default `30`), spectral diagrams (`N`, `Vy`, `Vz`, `Tx`, `My`, `Mz`), support reactions, **3D Clip Planes** (X/Y/Z), **DCR Heatmap** (*Demand-to-Capacity Ratio*), and deformed/modal **animations**.

### 3. Qt 6.11.1 Desktop Interface, Live Re-Editing & Guided Session (`F2`)
- **Single-Click Modeless Child Windows & Managed Z-Order:** Instant one-click opening of child dialogs without transient ghost outlines, plus modeless behavior keeping child windows cleanly above the main window without blocking viewport interaction.
- **Side Property Inspector (`Selected Element / Node`) & Free Re-Editing of All 3 Examples:** Direct inline **`Material:`** and **`Cross-Section:`** dropdown lists showing clean names (no `#ID` prefix), batch selection via **`Same Material`** / **`Same Section`**, full **`Undo`/`Redo` (`Ctrl+Z`/`Ctrl+Y`)** history, and unrestricted live editing and re-solving across all **3 built-in examples** for hands-on practice.
- **Interactive Guided Session (`Help > Guided Session...`, `F2`):** Automated 30-step interactive walkthrough covering modeling, editing, loading, analysis, post-processing, and reporting on any of the 3 built-in examples, featuring **4 cognitive reading pace profiles (PPM)**, synchronized visual widget highlighting with a **unified two-phase callout** (yellow prep and green action phases, both scaled proportionally to word count and positioned away from child dialogs in the lower-left area), live theme/language adaptation, and **automatic restoration** of the selected example to its initial state upon completion.
- **Interoperability & Reporting:** Open `.omniframe` JSON persistence (Schema `2.1.0`, backward-compatible with `1.0` & `2.0`), **DXF** and **BIM IFC4** import/export, and technical calculation reports in **HTML**, **Markdown**, and **Executive A4 PDF**.

### 4. Headless CLI (`omniframe-cli.exe`), Build Tooling, Packaging & 20 CTest Suites
- **Build & Complete Packaging:** Release preset `omniframe-msvc-release` and VS Code tasks (**`Qt6: Compilar omniframe-cli (MSVC)`** and **`Qt6: Compilar e Executar OmniFrame 3D (MSVC)`**) compile both `OmniFrame.exe` and `omniframe-cli.exe` and package them via `cmake/gerar_distribuicoes_limpas.ps1` under `D:\MISCELANEAS\VSCode\OmniFrame3D\`.
- **Automated Verification (`20` CTest Suites):** Standardized `CMakePresets.json` covering `test_Phase0` through `test_Phase9UiAndWorkflows` via `ctest --preset testes-msvc-release --output-on-failure`.
- **Microsoft Store Compliance & Clean Uninstall:** MSIX manifest (`repo/AppxManifest.xml`), Inno Setup installer (`repo/OmniFrame.iss`), 100% offline Privacy Policy (`docs/privacy-policy.md`), `runFullTrust` disclosure, and clean preference storage (`language.json`, `theme.json`, `graphics.json` under `%LOCALAPPDATA%\OmniFrame\OmniFrame\`).

</details>
