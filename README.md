# OmniFrame 3D — Análise Estrutural Tridimensional por Elementos Finitos (3D Space Frame & Truss FEA Suite)

**Plataforma desktop nativa de engenharia para análise estrutural tridimensional (FEA — estática linear, modal, flambagem, harmônica, espectro de resposta e não linear geométrica/J2) em Windows, desenvolvida em C++23 moderno (`OmniFrameKernel` livre de Qt), Qt 6.11.1, Eigen 3.4.0 e OpenGL 3.3 Core Profile. Oferece elementos 1D/2D/3D, gerador de malhas (`MeshGenerator`), combinações ELU/ELS (`LoadSystem`), suíte de solvers esparsos (`LinearSolverSuite`), interpolação contínua de Hermite, viewport de alta definição (SSAA/MSAA/SDF, Clip Planes e contornos DCR), interoperabilidade `.omniframe` Schema 2.1.0 / DXF / IFC4, auditoria ABNT NBR em PDF local, relatórios PDF A4 e CLI headless `omniframe-cli.exe` (`pt-BR` / `en-US`).**

[![Latest Release](https://img.shields.io/badge/Release-v2026.11.17.0-blue.svg)](https://github.com/fernandoncidade/OmniFrame_cpp/releases/tag/v2026.11.17.0)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=c%2B%2B)](CMakeLists.txt)
[![Qt 6.11.1](https://img.shields.io/badge/Qt-6.11.1-41CD52?logo=qt)](CMakeLists.txt)
[![Eigen 3.4.0](https://img.shields.io/badge/Eigen-3.4.0-d9534f)](https://eigen.tuxfamily.org)
[![OpenGL 3.3 Core](https://img.shields.io/badge/OpenGL-3.3_Core-5586A4?logo=opengl)](docs/architecture.md)
[![Referências Normativas](https://img.shields.io/badge/Referências-NBR_8800_%7C_NBR_6118_%7C_AISC_360_%7C_EC3-2ea44f.svg)](docs/compliance.md)
[![Platform Windows](https://img.shields.io/badge/Platform-Windows_11_%2F_10-0078D4?logo=windows)](https://apps.microsoft.com/detail/OmniFrame3D)
[![Microsoft Store](https://img.shields.io/badge/Microsoft_Store-Available-0078D4?logo=microsoft)](https://apps.microsoft.com/detail/OmniFrame3D)
[![License GNU AGPLv3](https://img.shields.io/badge/License-GNU_AGPLv3-blue.svg)](LICENSE)
[![Official Store EULA](https://img.shields.io/badge/License-Official_Store_EULA_%2B_CLC-purple.svg)](docs/licensing.md)
[![100% Offline](https://img.shields.io/badge/Security-100%25_Offline-success.svg)](docs/security-model.md)

<p align="center">
  <a href="docs/user-guide.md">📖 Guia do Usuário / User Guide</a> •
  <a href="docs/architecture.md">🏗️ Arquitetura / Architecture</a> •
  <a href="docs/compliance.md">📋 Conformidade / Compliance</a> •
  <a href="docs/security-model.md">🛡️ Privacidade &amp; Segurança</a> •
  <a href="docs/cli-reference.md">💻 CLI</a> •
  <a href="docs/licensing.md">⚖️ Duplo Licenciamento / Dual Licensing</a> •
  <a href="https://apps.microsoft.com/detail/OmniFrame3D">🪟 Microsoft Store</a><br>
  <b>Selecione o idioma / Select language:</b><br>
  <a href="#ptbr">🇧🇷 Português (BR)</a> •
  <a href="#enus">🇺🇸 English (US)</a>
</p>

---

<p align="center">
  <img src="docs/images/banner.png" alt="OmniFrame 3D Banner" width="300">
</p>

---

## <a id="ptbr"></a><a id="português-br"></a>🇧🇷 Português (BR)

> **Observação:** Este repositório contém o código-fonte aberto e a documentação de conformidade do **OmniFrame 3D** (implementação nativa em **C++23**, **Qt 6.11.1**, **Eigen 3.4.0** e **OpenGL 3.3 Core**), disponibilizado sob arquitetura transparente de **Duplo Licenciamento** (**GNU AGPLv3** para o código-fonte público no GitHub e **EULA Oficial + CLC** para o pacote binário assinado distribuído gratuitamente na Microsoft Store, com execução 100% local e offline). Instale a edição oficial assinada digitalmente através do link: [Instalar via Microsoft Store](https://apps.microsoft.com/detail/OmniFrame3D).

<details>
<summary><b>Clique para expandir / recolher a documentação em Português (BR)</b></summary><br>

O **OmniFrame 3D** (Versão 2026.11.17.0) é uma plataforma nativa desktop de engenharia para simulação estrutural no Windows, desenvolvida em **C++23 moderno** e **Qt 6.11.1** para engenheiros estruturais, engenheiros civis, projetistas mecânicos, pesquisadores e estudantes.

Diferente de suítes comerciais de FEA em caixa-preta que bloqueiam cálculos atrás de assinaturas caras, armazenamento obrigatório em nuvem e solvers opacos, o **OmniFrame 3D** foi concebido para entregar transparência matemática integral, soberania absoluta de dados locais (`Local-First`) e alto desempenho computacional em álgebra matricial esparsa:

> **“Quais são os deslocamentos nodais, as reações de apoio e os diagramas contínuos de esforços internos ($N, V_y, V_z, T_x, M_y, M_z$) ao longo de cada elemento tridimensional sob combinações reais de carregamento?”**

```text
Elemento Espacial de Viga-Coluna 3D (Nós i -> j)
 ├── 12 Graus de Liberdade: (ux, uy, uz, θx, θy, θz) por nó
 │    ├── Rigidez Axial: EA/L
 │    ├── Rigidez Torcional (Saint-Venant): GJ/L
 │    └── Flexão Biaxial e Cisalhamento: 12EI/L³, 6EI/L², 4EI/L, 2EI/L
 ├── Transformação Espacial [T] com Ângulo Roll (γ / ψ) e Condensação de Rótulas
 ├── Solver Esparso Direto de Cholesky: Eigen SimplicialLDLT (Ordenação AMD)
 ├── Recuperação Contínua de Esforços: Polinômios Cúbicos de Hermite + FEF
 └── Viewport OpenGL 3.3 Core (GLSL 330 Phong, SSAA até 200%, MSAA 16×, Shader SDF)
```

---

### ⚖️ Manifesto e Autorização Legal do Titular dos Direitos Autorais

> **Declaração Expressa de Fernando Nillsson Cidade:**  
> Eu, **Fernando Nillsson Cidade**, na qualidade de único autor, criador original e titular exclusivo dos direitos autorais do projeto **OmniFrame 3D**, **autorizo o uso, estudo, cópia, modificação, compilação e distribuição do código-fonte público deste repositório** nos exatos termos do arquivo **[LICENSE](LICENSE)** (**GNU Affero General Public License v3 — GNU AGPLv3**, identificador SPDX `AGPL-3.0-only`), ressalvada a proteção nominal de nome e marca sob a Seção 7(e) (`"OmniFrame 3D"`).  
> Qualquer versão modificada, extensão ou obra derivada do código-fonte público deve preservar a atribuição da autoria original e disponibilizar o código-fonte correspondente sob a mesma licença **GNU AGPLv3**.  
> **Liberdade para Documentação Institucional em `source/assets/`:** Os textos de conformidade contidos em [`source/assets/`](source/assets/COPYRIGHT/AVISO%20DE%20COPYRIGHT%20E%20MARCA%20REGISTRADA_pt_BR.txt) (`ABOUT`, `CLC`, `COPYRIGHT`, `EULA`, `LICENSES`, `NOTICES`, `PRIVACY_POLICY` e `RELEASE`) foram elaborados para viabilizar a distribuição oficial na Microsoft Store; o autor **autoriza expressamente** que desenvolvedores e forks alterem, excluam, adicionem ou adaptem tais documentos em suas próprias distribuições, observada a legislação de suas jurisdições e diretrizes de cada plataforma.  
> **Limite do Binário Oficial Assinado (Microsoft Store):** A edição empacotada em MSIX e assinada digitalmente pelo autor é distribuída **gratuitamente** na Microsoft Store sob o respectivo **[EULA Oficial](source/assets/EULA/EULA_pt_BR.txt)** (em conformidade com os Termos de Licença Padrão para Aplicativos da Microsoft Store) e **[Esclarecimento Comercial — CLC](source/assets/CLC/CLC_pt_BR.txt)**. É expressamente vedada a revenda comercial paga, a apropriação indevida da marca ou a republicação não autorizada por terceiros do pacote binário oficial assinado pelo autor.  
> **Canal de Suporte, Independência da Microsoft e Isenção Legal:** A **Microsoft NÃO presta suporte ao cliente nem cobre este software sob a Política de Privacidade da Microsoft**. Todo eventual suporte técnico do autor é prestado **exclusivamente por e-mail (`linceu_lighthouse@outlook.com`) e pelas abas Issues/Discussions do GitHub**, **em regime voluntário de melhores esforços (best-effort), conforme disponibilidade e sem obrigação de urgência, prazo ou SLA**. O software constitui ferramenta auxiliar de cálculo elástico e estudo fornecida **"NO ESTADO EM QUE SE ENCONTRA" ("AS IS")**, sem garantias e com integral isenção legal do autor quanto à aplicação definitiva em obras reais. Consulte [`docs/licensing.md`](docs/licensing.md#ptbr).

---

### 📌 Ficha Técnica do Projeto

<p align="center">
  <img src="docs/images/logo.png" alt="Ícone OmniFrame 3D" width="128">
</p>

- **Nome Oficial:** OmniFrame 3D (`3D Structural Frame & Truss FEA Suite`)
- **Autor e Mantenedor:** Fernando Nillsson Cidade *(Desenvolvedor Individual / Pessoa Física)*
- **E-mail Oficial de Suporte (exclusivamente por disponibilidade, sem urgência/SLA):** `linceu_lighthouse@outlook.com`
- **Repositório / Site Oficial:** [https://github.com/fernandoncidade/OmniFrame_cpp](https://github.com/fernandoncidade/OmniFrame_cpp)
- **Download Oficial (Microsoft Store — Edição Gratuita Assinada):** [Instalar via Microsoft Store](https://apps.microsoft.com/detail/OmniFrame3D)
- **Identidade MSIX e Aliases no Windows:** `fernandoncidade.OmniFrame3D` · Aliases de Execução `omniframe.exe` e `omniframe-cli.exe` (`desktop4:Subsystem="console"`) + associação `.omniframe` ([`repo/AppxManifest.xml`](repo/AppxManifest.xml))
- **Escopo Normativo Auditável (`ABNT NBR`):** Verificações locais verificadas em PDF para `NBR 6118:2023`, `NBR 6120:2019`, `NBR 6122:2022`, `NBR 6123:2023`, `NBR 7480:2022` e `NBR 8800:2024` (`LocalPdfVerified`), com marcação explícita `OutOfLocalPdfScope` para normas fora do pacote local (`NBR 8681`, `NBR 14762`, `NBR 15421`, `AISC 360-22` e `Eurocode 3` — [`docs/compliance.md`](docs/compliance.md#ptbr))
- **Idiomas Suportados Dinamicamente em Tempo de Execução:** Português (Brasil — `pt-BR`) · English (United States — `en-US`)
- **Requisitos de Sistema:** Windows 10 (build `17763+`) / Windows 11 x64, GPU compatível com **OpenGL 3.3 Core Profile**
- **Executáveis Gerados pelos Presets CMake (`CMakePresets.json`):**
  - `OmniFrame.exe` e `omniframe-cli.exe` (`build/OmniFrame3D/` e distribuídos juntos no pacote externo com `register_cli_windows.ps1`): Aplicativo desktop GUI principal em C++23 / Qt 6.11.1 / OpenGL 3.3 Core ([`source/main.cpp`](source/main.cpp)) com abertura direta de modelos via argumento e delegação CLI transparente para o executável console nativo `omniframe-cli.exe` ([`source/cli/main_cli.cpp`](source/cli/main_cli.cpp)) integrado ao Windows 10/11 (`CMD` / `PowerShell` via [`source/os/WindowsCliIntegration.cpp`](source/os/WindowsCliIntegration.cpp))
  - `VersionEditor.exe` (`build/Version/`): Utilitário GUI e CLI (`--check` / `--apply`) de auditoria e sincronização de versão/data ([`mocks/VersionEditor.cpp`](mocks/VersionEditor.cpp))
  - `CompileTranslationsGUI.exe` e `CompileTranslationsTerminal.exe` (`build/CompileLanguage/`): Ferramentas de compilação e validação dos catálogos `.ts` → `.qm`
  - **Suíte Modular CTest (`build/Testes/`):** **20 suítes independentes** de validação numérica, solvers, normas e fluxos de UI (`test_Material`..`test_SpringsSettlementsAndExamples` + `test_Phase0ValidationAndBenchmarks`..`test_Phase9UiAndWorkflows`)

---

### 🌟 Principais Recursos e Módulos Implementados

1. **Formulação Mecânica Exata de Euler-Bernoulli 3D (12 DOFs por Elemento):**
   - Matriz de rigidez local $12 \times 12$ ($[k_e]$) abrangendo rigidez axial ($EA/L$), torção pura de Saint-Venant ($GJ/L$), flexão biaxial acoplada ao cortante ($12EI/L^3$, $6EI/L^2$, $4EI/L$, $2EI/L$), matriz de transformação ortogonal $[T]$ com ângulo de rolamento dos eixos principais (**Roll $\gamma / \psi$**) e condensação estática automática de **rótulas** nas extremidades das barras;
   - Apoios elásticos nodais (**molas $k$**), vínculos rígidos e articulados (**Engastado**, **Articulado Fixo**, **Móvel/Rolete**), **deslocamentos e rotações prescritos**, forças/momentos nodais concentrados e carregamentos distribuídos uniformes com vetores consistentes de forças de engastamento perfeito (*Fixed-End Forces*).
2. **`OmniFrameKernel` em C++23 Puro, Suíte Linear & Solvers Multifísica:**
   - Núcleo FEA estritamente livre de Qt (`OmniFrameKernel` — C++23 + Eigen 3.4.0) compartilhado por `OmniFrameCore`, `OmniFrame.exe` e `omniframe-cli.exe`;
   - Montagem esparsa em precisão dupla via `LinearSolverSuite` (`SimplicialLDLT` AMD, `SimplicialLLT`, `SparseLU`, `ConjugateGradient`, `BiCGSTAB`) com política explícita e rastreável de fallback (`allowFallback` + diagnóstico) executada assincronamente em *worker threads* na UI;
   - Recursos multianálise unificados para 1D/2D/3D: **Estática Linear**, **Modal (autovalores)**, **Flambagem Linear** (com aplicação direta de **`Imperfeição Inicial`** à geometria), **Varredura Harmônica FRF**, **Espectro de Resposta**, **Dinâmica Transiente (`Newmark-Beta` / `HHT-Alpha α=-0,05`, acelerograma `a_g` ou `Histórico de Força F(t)`) e Sísmica**, além de **Não Linear Geométrica (`P-Delta` / `Corrotacional 3D` com `Tangente Completa`, `Line Search`, `NR` & `Arc-Length` com critério energético)** e **Plasticidade Material J2** ($\sigma_y, H_{\text{iso}}, H_{\text{kin}}$) em barras 1D;
   - Pré-validação automática estruturada (`ModelValidation::issues` cobrindo conectividade BFS, finitude e malhas 2D/3D), detecção automática de **mecanismos cinemáticos** / singularidades e verificação rigorosa do **equilíbrio estático global** ($\sum F = 0$, $\sum M = 0$).
3. **Elementos 1D/2D/3D Unificados, Recuperação Contínua & Pós-Processamento Avançado:**
   - Biblioteca unificada em `FemSolver3D`: pórticos/treliças 3D Euler-Bernoulli / Timoshenko (`BeamTruss1D`, `Corotational1D`), cascas Mindlin/DKT (`ShellElements`: `Tri3`, `Quad4`, `Quad8`) e sólidos 3D hexa/tetra (`SolidElements`: `Tet4`, `Tet10`, `Hex8`, `Hex20` com DOFs `mask3` em nós sólidos puros e acoplamento automático em nós coincidentes), além de geração paramétrica (`MeshGenerator` 1D/2D/3D & Delaunay);
   - Reconstrução contínua exata por **polinômios cúbicos de Hermite** (`Nenhum`, `Deformada 3D`, `Normal N`, `Cortante Vy`, `Cortante Vz`, `Torção Tx`, `Fletor My`, `Fletor Mz`), **Mapa de Tensão de von Mises (`VonMisesStress`)** e aba `"Esforços 2D/3D (Casca/Sólido)"` com tensões principais e resultantes de casca, vetores de reações, controle de ampliação (`1`–`100`, escala `1.5x`–`150x`), **Planos de Corte 3D (Clip Planes)**, **contornos DCR** e **animação modal/deformada**.
4. **Viewport OpenGL 3.3 Core com Pipeline de Alta Definição (`SSAA`, `MSAA` e `Shader SDF`):**
   - Pipeline acelerado por GPU em **GLSL 330** com iluminação Phong, câmera orbital fluida (Botão Direito/Meio para orbitar, `Shift` + arraste para pan, roda do mouse para zoom), atalhos instantâneos (`Vista 3D Isométrica`, `Frontal XZ`, `Planta XY`, `Lateral YZ`, `Enquadrar Tudo [F]`) e seleção espacial exata via **Raycasting 3D** (incluindo multisseleção com `Ctrl`);
   - Menu **Configurações → Qualidade Gráfica e Resolução** permitindo configurar predefinições (`Máximo Automático`, `Ultra Qualidade`, etc.), superamostragem interna **SSAA até 200% (`2.00×`)**, **MSAA por GPU até `16×`**, downsampling **Catmull-Rom 16-tap com nitidez CAS**, suavização analítica subpixel de linhas por **Shader SDF**, VSync, contraste de grade e paletas dedicadas para o **Plano Ativo** (`ObsidianDark`, `PureNight`, `GraphiteDark`, `CleanLight`).
5. **Modelagem Paramétrica, Combinações, Interoperabilidade & Relatórios Técnicos:**
   - Ferramentas em plano ativo (`XY`/`XZ`/`YZ`, Snap Grid, Eixos Locais), casos e combinações ELU/ELS (`LoadSystem`) e auditoria normativa **ABNT NBR** (`LocalPdfVerified`: `NBR 6118:2023`, `6120:2019`, `6122:2022`, `6123:2023`, `7480:2022`, `8800:2024`; `OutOfLocalPdfScope` explícito para normas ausentes como `NBR 8681`, `14762`, `15421`, `AISC 360` e `Eurocode 3`);
   - Editores para materiais elásticos/J2 ($E, \nu, \alpha, \gamma, \rho$), seções paramétricas (`Retangular`, `Circular`, `Perfil I/W`, `Tubular`, `Personalizada`), **3 exemplos estruturais prontos abrindo no plano padrão `XY (Planta)` e totalmente reeditáveis como exercício** (incluindo edição inline por listas suspensas limpas no Inspetor Lateral e `Desfazer`/`Refazer`), intercâmbio **DXF / IFC4 / VTK `UNSTRUCTURED_GRID` / CSV** (com caminhos UTF-8 acentuados via `Utf8Path.hpp`), formato aberto **`.omniframe` JSON Schema `2.1.0`** (com bloco `"surfaceMesh"` retrocompatível com `1.0` e `2.0`), relatórios Seção 9 (itens 1 a 20 e contínuo 2D/3D) em **HTML / Markdown / PDF executivo A4** (`PdfReportGenerator`) e CLI integrada **`omniframe-cli.exe`** (`--example`, `--nbr-audit`, `static|modal|buckling|nonlinear|pdelta|harmonic|spectrum|transient|seismic`, `--scheme newmark|hht`, `--load-factor`, `--modified-newton`, `--no-line-search`, `--imperfection`, `--install-windows-integration` para CMD/PowerShell e `20` suítes CTest).
6. **Interface Qt 6.11.1 Adaptativa, Sessão Guiada (`F2`), Janelas Não-Modais (`Z-Order`) & Soberania 100% Offline:**
   - Alternância dinâmica em tempo de execução de **Idioma** (`Português (BR)` / `English (US)`), **Tema Visual** (`Escuro` / `Claro`) e submenu **`Configurações > CLI Windows (CMD / PowerShell)`** propagados a todos os menus, setas vetoriais nativas de `QComboBox`/`QSpinBox` via `QProxyStyle` (sem gravação de arquivos PNG temporários, com cantos externos arredondados dos *spin buttons* acompanhando exatamente a moldura dos campos numéricos), painéis acopláveis recolhíveis com botões estilo *spin button* (`Inspetor e Entrada Rápida` recolhido para a lateral esquerda com setas `◀`/`▶` e rótulo rotacionado em `-90°`, e `Tabelas de Resultados` com setas `▼`/`▲` na base), tabelas sincronizadas (inclusive aba **`Esforços 2D/3D (Casca/Sólido)`**), gráficos do Plano Ativo, diálogos de engenharia (`AnalysisSuiteDialog`, `LoadCombinationsDialog`, `MeshGeneratorDialog`, `PostProcessControlDialog`, `NbrComplianceDialog`, `PdfReportOptionsDialog`), **Manual de Instruções (`F1`, com botão de acesso rápido ao Capítulo 10 da CLI)** e **Sessão Guiada (`F2` — `GuidedSessionDialog`)**;
   - **Sessão Guiada Passo a Passo (`Ajuda > Sessão Guiada...`, `F2`):** dimensões ampliadas para exibição integral de todos os rótulos, percorrendo **30 etapas** sobre qualquer um dos 3 exemplos pré-definidos no plano padrão `XY (Planta)` com 4 perfis científicos de ritmo de leitura (PPM — Fluente, Técnico, Voz Alta e Acessível/Dislexia), **callout e destaque visual unificados em 2 fases (sem sobreposição sobre janelas filhas)** e restauração automática do exemplo selecionado ao estado inicial ao concluir;
   - Gerenciamento central de janelas **Modeless** (`WindowZOrderManager`) com preparo prévio da alça nativa oculta para abertura em clique único sem contornos fantasmas e elevação automática na pilha de profundidade **`Z-Order`**;
   - Operação **100% Local-First e Offline**: zero telemetria, zero sockets de rede e persistência restrita a projetos do usuário mais 3 arquivos locais de preferência (`language.json`, `theme.json` e `graphics.json`) em `%LOCALAPPDATA%\OmniFrame\OmniFrame\`.

---

### 📸 Capturas de Tela da Interface (`pt-BR`)

Capturas reais do **OmniFrame 3D** localizado em **Português (Brasil)** no repositório [`repo/LISTAGEM/pt_BR/`](repo/LISTAGEM/pt_BR), demonstrando os **3 exemplos estruturais integrados** com renderização **SSAA 2.00× / MSAA 16× / Linhas AA: Shader SDF**, painéis acopláveis recolhíveis (`Inspetor e Entrada Rápida` na lateral esquerda e `Tabelas de Resultados e Diagnóstico Estrutural` na base), alternância de **Plano Ativo** (`XY (Planta)` e `XZ (Frontal)`), escalas de deslocamento (`|u|`), carregamentos e reações de apoio:

<p align="center">
  <strong>Exemplo 1 (pt-BR) — Pórtico Espacial de Edifício (2 Pavimentos, Vento e Sobrecarga) · Painéis Expandidos (3188×1346 px): Inspetor Lateral com Spin Buttons Arredondados, Deformada 3D (7.60 mm), Reações e Tabelas de Resultados (12 nós, 16 barras, 72 DOFs)</strong><br><br>
  <img src="repo/LISTAGEM/pt_BR/Captura%20de%20tela%202026-11-16%20153830.png" alt="OmniFrame 3D Português (BR) Exemplo 1 Pórtico Espacial de Edifício Painéis Expandidos" width="900">
</p>

<p align="center">
  <strong>Exemplo 1 (pt-BR) — Pórtico Espacial de Edifício (2 Pavimentos) · Painéis Recolhidos (3760×1794 px): Inspetor Lateral Rotacionado em -90° (▶) e Base Minimizada (▲) Ampliando o Viewport OpenGL com SSAA 2.00× / MSAA 16× / Shader SDF</strong><br><br>
  <img src="repo/LISTAGEM/pt_BR/Captura%20de%20tela%202026-11-16%20153851.png" alt="OmniFrame 3D Português (BR) Exemplo 1 Pórtico Espacial de Edifício Painéis Recolhidos" width="900">
</p>

<p align="center">
  <strong>Exemplo 2 (pt-BR) — Balanço Espacial em L Sujeito a Flexo-Torção · Plano Ativo XZ (Frontal) e Painéis Recolhidos (3760×1794 px): Deformada 3D (216.2 mm), Carga Concentrada e Reações no Engaste (3 nós, 2 barras, 18 DOFs)</strong><br><br>
  <img src="repo/LISTAGEM/pt_BR/Captura%20de%20tela%202026-11-16%20154256.png" alt="OmniFrame 3D Português (BR) Exemplo 2 Balanço Espacial em L Plano XZ Frontal" width="900">
</p>

<p align="center">
  <strong>Exemplo 3 (pt-BR) — Treliça Espacial / Passarela Metálica 3D · Plano Ativo XY (Planta) e Painéis Recolhidos (3760×1794 px): Deformada 3D (2.75 mm), Cargas Nodais e Reações de Apoio (20 nós, 57 barras, 120 DOFs)</strong><br><br>
  <img src="repo/LISTAGEM/pt_BR/Captura%20de%20tela%202026-11-16%20154339.png" alt="OmniFrame 3D Português (BR) Exemplo 3 Treliça Espacial Passarela Metálica 3D" width="900">
</p>

---

### <a id="microsoft-store-privacidade-suporte"></a>🛡️ Distribuição Microsoft Store, Privacidade e Garantia de Desinstalação Limpa

> [!IMPORTANT]
> **Conformidade Oficial Microsoft Store (Partner Center / MSIX / Políticas v7.20):**
> - **Política de Privacidade Independente (Política 10.5):** **A Microsoft NÃO fornece uma política de privacidade padrão para o OmniFrame 3D, e este software NÃO é coberto pela [Política de Privacidade da Microsoft](https://www.microsoft.com/pt-br/privacy/privacystatement).** A declaração da Microsoft rege exclusivamente os dados da própria loja ou diagnósticos do Windows. Todo o processamento dentro do OmniFrame 3D ocorre localmente na máquina do usuário e é regido estritamente pela [Política de Privacidade do OmniFrame 3D](docs/privacy-policy.md#ptbr).
> - **Suporte do Desenvolvedor Independente:** **A Microsoft NÃO presta suporte ao cliente nem assistência técnica para o OmniFrame 3D.** Todo suporte é oferecido exclusivamente pelo autor (`Fernando Nillsson Cidade`, conta individual) por via eletrônica (`linceu_lighthouse@outlook.com` / [GitHub Issues](https://github.com/fernandoncidade/OmniFrame_cpp/issues)), sem SLA e conforme disponibilidade.
> - **Capacidade Restrita `runFullTrust` (Política 10.6):** Declarada em [`repo/AppxManifest.xml`](repo/AppxManifest.xml) exclusivamente porque o software é uma aplicação Win32 nativa em C++23 / Qt 6.11.1 que utiliza renderização GPU em OpenGL 3.3 Core e diálogos padrão do Windows para salvar/abrir arquivos `.omniframe`, `.html` e `.md`, com zero serviços ocultos, zero drivers de kernel e zero conexões de rede.
> - **Garantia de Desinstalação Limpa (Política 10.2.7):** Remoção completa a qualquer momento via **Configurações do Windows → Aplicativos → Aplicativos instalados** (ou Menu Iniciar → Desinstalar), sem deixar serviços em segundo plano ou modificações residuais no sistema.

| Disposição Regulatória | Referência Oficial no Repositório (`pt-BR`) |
|---|---|
| **Política de Privacidade Própria** | [docs/privacy-policy.md](docs/privacy-policy.md#ptbr) · [Asset Interno (.txt)](source/assets/PRIVACY_POLICY/Privacy_Policy_pt_BR.txt) |
| **Site Oficial do Desenvolvedor** | [https://github.com/fernandoncidade/OmniFrame_cpp](https://github.com/fernandoncidade/OmniFrame_cpp) *(distinto da página da Store)* |
| **Suporte Técnico (Best-Effort)** | `linceu_lighthouse@outlook.com` · [GitHub Issues](https://github.com/fernandoncidade/OmniFrame_cpp/issues) *(sem suporte Microsoft; sem telefone/SLA)* |
| **Licença Oficial do Pacote Store** | [EULA Oficial (PT)](source/assets/EULA/EULA_pt_BR.txt) · [CLC (PT)](source/assets/CLC/CLC_pt_BR.txt) · [GNU AGPLv3 (Código-Fonte)](LICENSE) |
| **Base Legal de Privacidade** | Execução local solicitada pelo usuário — zero coleta de dados pessoais (LGPD Art. 7º, V e IX / GDPR; arquivos locais em `%LOCALAPPDATA%\OmniFrame\OmniFrame\`) |

---

### <a id="quick-start-compilacao"></a>🚀 Compilação, Testes e Execução a partir do Código-Fonte

#### Pré-requisitos
- **Sistema Operacional:** Windows 11 / Windows 10 (build `17763+`) x64;
- **Compilador C++23:** MSVC com suporte integral a C++23 (Visual Studio Build Tools / Community x64);
- **Qt 6.11.1:** Módulos `Core`, `Gui`, `Widgets`, `OpenGL`, `OpenGLWidgets` e `LinguistTools`;
- **Eigen 3.4.0:** Detectado localmente ou obtido automaticamente via `FetchContent` do CMake;
- **Ferramentas de Build:** **CMake 3.28+** e **Ninja**.

#### Compilação via Presets Oficiais (`CMakePresets.json`)
```powershell
# Clonar o repositório oficial
git clone https://github.com/fernandoncidade/OmniFrame_cpp.git
cd OmniFrame_cpp

# Configurar e compilar o aplicativo principal em build/OmniFrame3D/
cmake --preset omniframe-msvc-release
cmake --build --preset omniframe-msvc-release

# Configurar, compilar e executar os 20 testes estruturais CTest em build/Testes/
cmake --preset testes-msvc-release
cmake --build --preset testes-msvc-release
ctest --preset testes-msvc-release --output-on-failure

# (Opcional) Compilar os utilitários independentes de versão e tradução
cmake --preset version-msvc-release && cmake --build --preset version-msvc-release
cmake --preset compile-language-msvc-release && cmake --build --preset compile-language-msvc-release

# Executar o aplicativo gráfico principal (ou abrir diretamente um modelo)
.\build\OmniFrame3D\OmniFrame.exe
.\build\OmniFrame3D\OmniFrame.exe "caminho\modelo.omniframe"

# Registrar e utilizar omniframe-cli no Windows 10/11 (CMD e PowerShell, sem admin)
.\build\OmniFrame3D\omniframe-cli.exe --install-windows-integration --json
.\cmake\register_cli_windows.ps1 -CheckOnly -Json
omniframe-cli --example building --nbr-audit --json
```

#### Utilitários Auxiliares via Linha de Comando (`mocks/`)
```powershell
# Auditar consistência de versões e datas no repositório
.\build\Version\VersionEditor.exe --check --root .

# Verificar e compilar catálogos de tradução (.ts -> .qm)
.\build\CompileLanguage\CompileTranslationsTerminal.exe --check --root .
.\build\CompileLanguage\CompileTranslationsTerminal.exe --root . --verbose
```

*(Para detalhes completos de execução e implantação, consulte [`docs/cli-reference.md`](docs/cli-reference.md#ptbr).)*

---

### 🏛️ Escopo Técnico, Responsabilidade Profissional e Isenção ("AS IS")

O **OmniFrame 3D** é uma ferramenta computacional auxiliar de análise estrutural matricial por Elementos Finitos (**estática linear, modal, flambagem, harmônica, espectro de resposta, segunda ordem $P$-$\Delta$/corrotacional e plasticidade J2**), destinada ao estudo acadêmico, ensino, pesquisa e pré-dimensionamento estrutural, podendo conter aproximações numéricas ou limitações frente a comportamentos não lineares. Os resultados gerados (deslocamentos, reações e diagramas de esforços) **NÃO substituem verificações completas em regime último/serviço, efeitos de segunda ordem ($P$-$\Delta$ e $P$-$\delta$), instabilidades globais ou locais, combinações normativas específicas (`ABNT NBR 8800`, `NBR 6118`, `AISC 360-22`, `Eurocode 3`) e supervisão formal de profissional legalmente habilitado com emissão de ART/RRT perante o CREA/CAU**. O software é fornecido **"NO ESTADO EM QUE SE ENCONTRA" ("AS IS")**, com todas as suas falhas e sem garantias explícitas ou implícitas; **qualquer aplicação definitiva em projetos executivos, obras reais ou engenharia de produção ocorre por conta e risco exclusivos do usuário, com integral isenção civil, criminal e técnica do autor**.

---

### 📚 Documentação Técnica e Conformidade

- 🏛️ [Arquitetura do Sistema e Deduções Matriciais FEA](docs/architecture.md#ptbr)
- 📋 [Conformidade Normativa e Benchmarks Numéricos](docs/compliance.md#ptbr)
- 🔒 [Política de Privacidade e Soberania de Dados](docs/privacy-policy.md#ptbr)
- ⚖️ [Modelo de Duplo Licenciamento e Checklist Microsoft Store MSIX](docs/licensing.md#ptbr)
- 🛡️ [Modelo de Segurança e Resiliência em C++23](docs/security-model.md#ptbr)
- 📖 [Guia do Usuário](docs/user-guide.md#ptbr) · [Manual Técnico Completo (MANUAL.md)](MANUAL.md#parte-1-manual-de-instruções-português---br)
- 💻 [Referência da Linha de Comando e Utilitários (CLI)](docs/cli-reference.md#ptbr)
- 💡 [Sugestões de Evolução e Backlog Técnico](docs/suggested-issues.md#ptbr)
- 🔬 [Auditoria Técnica, ADRs, Matriz V&V e Roadmap](docs/technical-audit-and-roadmap.md)
- 🚀 [Notas de Lançamento da Versão 2026.11.17.0](docs/release-v2026.11.17.0.md#ptbr)

---

### ⚖️ Licenciamento, Suporte e Comunidade

- **Código-Fonte Público (GitHub):** Licenciado sob **GNU Affero General Public License v3 (GNU AGPLv3)** ([`LICENSE`](LICENSE)), protegida sob a Lei nº 9.609/1998, Lei nº 9.610/1998 e tratados internacionais de software livre;
- **Pacote Oficial Microsoft Store (Gratuito):** Distribuído e assinado digitalmente pelo autor sob **[EULA Oficial](source/assets/EULA/EULA_pt_BR.txt)** e **[CLC](source/assets/CLC/CLC_pt_BR.txt)** ([Download Oficial](https://apps.microsoft.com/detail/OmniFrame3D)), respeitando eventuais direitos irrenunciáveis previstos na legislação consumerista aplicável e regras da plataforma;
- **Componentes de Terceiros:** Qt 6.11.1 (`GNU LGPLv3`), Eigen 3.4.0 (`Mozilla Public License 2.0`), Khronos OpenGL/GLSL — atribuições consolidadas em [`source/assets/NOTICES/NOTICE_pt_BR.txt`](source/assets/NOTICES/NOTICE_pt_BR.txt);
- **Suporte e Contribuições:** Atendimento exclusivo por e-mail (`linceu_lighthouse@outlook.com`) e GitHub, voluntário, sem urgência e sem SLA — veja [`CONTRIBUTING.md`](CONTRIBUTING.md#ptbr).

</details>

---

## <a id="enus"></a><a id="english-us"></a>🇺🇸 English (US)

> **Note:** This repository contains the open-source codebase and compliance documentation of **OmniFrame 3D** (native **C++23**, **Qt 6.11.1**, **Eigen 3.4.0**, and **OpenGL 3.3 Core** implementation), distributed under a transparent **Dual-Licensing** framework (**GNU AGPLv3** for public source code on GitHub and the author's **Official EULA + CLC** for the digitally signed binary package distributed free of charge on the Microsoft Store, executing 100% locally and offline). Install the signed official release via: [Install via Microsoft Store](https://apps.microsoft.com/detail/OmniFrame3D).

<details>
<summary><b>Click to expand / collapse English (US) documentation</b></summary><br>

**OmniFrame 3D** (Version 2026.11.17.0) is an engineering-grade, native Windows desktop structural simulation platform designed in **modern C++23** and **Qt 6.11.1** for structural engineers, civil engineers, mechanical designers, researchers, and students.

Unlike commercial black-box FEA suites that lock calculations behind expensive subscription paywalls, forced cloud storage, and opaque solvers, **OmniFrame 3D** is purpose-built to deliver complete mathematical transparency, strict local data sovereignty (`Local-First`), and high-speed sparse matrix computation:

> **“What are the nodal displacements, support reactions, and continuous internal member forces ($N, V_y, V_z, T_x, M_y, M_z$) along each 3D element under realistic load combinations?”**

```text
3D Euler-Bernoulli Beam-Column Element (Nodes i -> j)
 ├── 12 Degrees of Freedom: (ux, uy, uz, θx, θy, θz) per node
 │    ├── Axial Stiffness: EA/L
 │    ├── Torsional Stiffness (Saint-Venant): GJ/L
 │    └── Biaxial Bending & Shear: 12EI/L³, 6EI/L², 4EI/L, 2EI/L
 ├── 3D Spatial Transformation [T] with Roll Angle (γ / ψ) & Hinge Condensation
 ├── Direct Sparse Cholesky Solver: Eigen SimplicialLDLT (AMD Reordering)
 ├── Continuous Internal Force Recovery: Hermite Cubic Polynomials + FEF
 └── OpenGL 3.3 Core Viewport (GLSL 330 Phong, up to 200% SSAA, 16× MSAA, Shader SDF)
```

---

### ⚖️ Copyright Holder Manifesto & Legal Authorization

> **Express Statement by Fernando Nillsson Cidade:**  
> I, **Fernando Nillsson Cidade**, as the sole author, original creator, and exclusive copyright holder of the **OmniFrame 3D** project, **authorize the use, study, copying, modification, compilation, and distribution of the public source code in this repository** strictly under the **[LICENSE](LICENSE)** terms (**GNU Affero General Public License v3 — GNU AGPLv3**, SPDX identifier `AGPL-3.0-only`), reserving name and trademark protection under Section 7(e) (`"OmniFrame 3D"`).  
> Any modified version, extension, or derivative work based on the public source code must retain original authorship attribution and make its corresponding source code available under the same **GNU AGPLv3** license.  
> **Freedom for Institutional Documentation in `source/assets/`:** Compliance texts located under [`source/assets/`](source/assets/COPYRIGHT/COPYRIGHT%20AND%20TRADEMARK%20NOTICE_en_US.txt) (`ABOUT`, `CLC`, `COPYRIGHT`, `EULA`, `LICENSES`, `NOTICES`, `PRIVACY_POLICY`, and `RELEASE`) were authored to satisfy official Microsoft Store submission requirements; the author **expressly grants developers and forks complete freedom** to alter, remove, add, or adapt these documents in their own distributions, provided they comply with applicable laws and platform policies in their respective jurisdictions.  
> **Boundary for the Author's Signed Store Binary (Microsoft Store):** The MSIX package built and digitally signed by the author is distributed **free of charge** on the Microsoft Store under the author's **[Official EULA](source/assets/EULA/EULA_en_US.txt)** (aligned with Microsoft Store Standard Application License Terms) and **[Commercial Licensing Clarification — CLC](source/assets/CLC/CLC_en_US.txt)**. Paid commercial resale, trademark misappropriation, or unauthorized redistribution of the author's official signed binary package is strictly prohibited.  
> **Support Channel, Microsoft Independence & Legal Disclaimer:** **Microsoft does NOT provide customer support and does NOT cover this software under Microsoft's Privacy Statement.** Any developer support is provided **exclusively via email (`linceu_lighthouse@outlook.com`) and GitHub Issues/Discussions**, **on a voluntary best-effort basis according to availability, with no urgency commitment, response guarantee, or SLA**. This software is an auxiliary elastic analysis and educational tool supplied **"AS IS"** without warranties, and the author is wholly exempt from civil, criminal, or technical liability arising from production or construction use. See [`docs/licensing.md`](docs/licensing.md#enus).

---

### 📌 Project Fact Sheet

<p align="center">
  <img src="docs/images/logo.png" alt="OmniFrame 3D Icon" width="128">
</p>

- **Official Name:** OmniFrame 3D (`3D Structural Frame & Truss FEA Suite`)
- **Author & Maintainer:** Fernando Nillsson Cidade *(Individual Developer)*
- **Official Support Email (strictly by availability, no urgency/SLA):** `linceu_lighthouse@outlook.com`
- **Official Repository / Website:** [https://github.com/fernandoncidade/OmniFrame_cpp](https://github.com/fernandoncidade/OmniFrame_cpp)
- **Official Download (Microsoft Store — Signed Free Edition):** [Install via Microsoft Store](https://apps.microsoft.com/detail/OmniFrame3D)
- **Windows MSIX Identity & Execution Aliases:** `fernandoncidade.OmniFrame3D` · Execution Aliases `omniframe.exe` and `omniframe-cli.exe` (`desktop4:Subsystem="console"`) + `.omniframe` file association ([`repo/AppxManifest.xml`](repo/AppxManifest.xml))
- **Auditable Normative Scope (`ABNT NBR`):** Local PDF-verified checks for `NBR 6118:2023`, `NBR 6120:2019`, `NBR 6122:2022`, `NBR 6123:2023`, `NBR 7480:2022`, and `NBR 8800:2024` (`LocalPdfVerified`), with absent standards explicitly flagged `OutOfLocalPdfScope` (`NBR 8681`, `NBR 14762`, `NBR 15421`, `AISC 360-22`, and `Eurocode 3` — [`docs/compliance.md`](docs/compliance.md#enus))
- **Runtime Dynamic Languages:** English (United States — `en-US`) · Português (Brasil — `pt-BR`)
- **System Requirements:** Windows 10 (build `17763+`) / Windows 11 x64, GPU supporting **OpenGL 3.3 Core Profile**
- **Executables Built by CMake Presets (`CMakePresets.json`):**
  - `OmniFrame.exe` and `omniframe-cli.exe` (`build/OmniFrame3D/` and packaged together in external distributions with `register_cli_windows.ps1`): Primary C++23 / Qt 6.11.1 / OpenGL 3.3 Core desktop GUI ([`source/main.cpp`](source/main.cpp)) supporting direct file launch plus transparent forwarding to the native console binary `omniframe-cli.exe` ([`source/cli/main_cli.cpp`](source/cli/main_cli.cpp)) integrated into Windows 10/11 (`CMD` / `PowerShell` via [`source/os/WindowsCliIntegration.cpp`](source/os/WindowsCliIntegration.cpp))
  - `VersionEditor.exe` (`build/Version/`): GUI & CLI (`--check` / `--apply`) version and date synchronization/auditing utility ([`mocks/VersionEditor.cpp`](mocks/VersionEditor.cpp))
  - `CompileTranslationsGUI.exe` and `CompileTranslationsTerminal.exe` (`build/CompileLanguage/`): Translation compilation and verification utilities (`.ts` → `.qm`)
  - **Modular CTest Suite (`build/Testes/`):** **20 standalone suites** covering mechanics, solvers, mesh/interop, NBR compliance, and UI workflows (`test_Material`..`test_SpringsSettlementsAndExamples` + `test_Phase0ValidationAndBenchmarks`..`test_Phase9UiAndWorkflows`)

---

### 🌟 Key Capabilities & Implemented Modules

1. **Exact 3D Euler-Bernoulli Beam-Column Formulation (12 DOFs per Member):**
   - $12 \times 12$ local stiffness matrix ($[k_e]$) covering axial response ($EA/L$), Saint-Venant pure torsion ($GJ/L$), biaxial bending coupled with shear ($12EI/L^3$, $6EI/L^2$, $4EI/L$, $2EI/L$), orthogonal 3D transformation $[T]$ with principal axis orientation (**Roll Angle $\gamma / \psi$**), and automatic static condensation of member **end releases (hinges)**;
   - Elastic nodal **spring supports ($k$)**, **Fixed**, **Pinned**, and **Roller** restraints, **prescribed displacements/rotations**, concentrated nodal forces/moments, and uniform distributed member loads using consistent Fixed-End Forces.
2. **Pure C++23 `OmniFrameKernel`, Sparse Linear Suite & Multi-Physics Solvers:**
   - Strictly Qt-free FEA kernel (`OmniFrameKernel` — C++23 + Eigen 3.4.0) powering `OmniFrameCore`, `OmniFrame.exe`, and `omniframe-cli.exe`;
   - Double-precision (64-bit) sparse assembly via `LinearSolverSuite` (`SimplicialLDLT` with AMD ordering, `SimplicialLLT`, `SparseLU`, `ConjugateGradient`, `BiCGSTAB`) with explicit tracked fallback policy (`allowFallback` + diagnostic message) executed asynchronously on worker threads in the UI;
   - Multi-analysis capabilities unified across 1D/2D/3D: **Linear Static**, **Modal Eigenvalue**, **Linear Buckling** (with direct **`Initial Imperfection`** seeding onto geometry), **Harmonic FRF Sweep**, **Response Spectrum**, **Transient (`Newmark-Beta` / `HHT-Alpha α=-0.05`, accelerogram `a_g` or `Force Time History F(t)`) & Seismic Time-History**, alongside **Geometric Nonlinear (`P-Delta` / `Corotational 3D` with `Full Tangent`, `Line Search`, `NR` & `Arc-Length` with energy criterion)** and **Material J2 Plasticity** ($\sigma_y, H_{\text{iso}}, H_{\text{kin}}$) on 1D frame members;
   - Structured pre-validation (`ModelValidation::issues` covering BFS connectivity, coordinate finiteness, and 2D/3D meshes), automatic **kinematic mechanism** / singularity detection, and strict global static equilibrium verification ($\sum F = 0$, $\sum M = 0$).
3. **Unified 1D/2D/3D Elements, Continuous Recovery & Post-Processing Controls:**
   - Element library unified in `FemSolver3D`: 3D Euler-Bernoulli / Timoshenko beams & trusses (`BeamTruss1D`, `Corotational1D`), Mindlin/DKT shells (`ShellElements`: `Tri3`, `Quad4`, `Quad8`), and 3D tetra/hexa solids (`SolidElements`: `Tet4`, `Tet10`, `Hex8`, `Hex20` allocating translational `mask3` DOFs on pure solid nodes and auto-merging coincident 1D/2D/3D nodes), plus parametric mesh generation (`MeshGenerator` 1D/2D/3D & Delaunay);
   - Exact continuous diagram recovery via **cubic Hermite interpolation** (`None`, `Deformed 3D`, `Normal N`, `Shear Vy`, `Shear Vz`, `Torsion Tx`, `Moment My`, `Moment Mz`), **von Mises Stress Contour (`VonMisesStress`)** and dedicated **2D/3D Shell/Solid Stresses tab**, nodal reaction arrows, adjustable amplification slider (`1`–`100`, scaling `1.5x`–`150x`), **3D Clip Planes**, **DCR utilization contours**, and **modal/deformed animation**.
4. **OpenGL 3.3 Core Viewport with High-Definition Rendering (`SSAA`, `MSAA` & `Shader SDF`):**
   - Hardware-accelerated **GLSL 330** Phong lighting pipeline, orbital camera (Right/Middle drag to orbit, `Shift` + drag to pan, scroll wheel to zoom), instant preset views (`3D Iso`, `Front XZ`, `Plan XY`, `Side YZ`, `Fit View [F]`), and exact **3D Raycasting** selection (including `Ctrl` multi-selection);
   - **Settings → Graphics Quality & Resolution** dialog with presets (`Auto Max`, `Ultra Quality`, etc.), internal supersampling **SSAA up to 200% (`2.00×`)**, **GPU MSAA up to `16×`**, **Catmull-Rom 16-tap downsampling with CAS sharpening**, analytical subpixel **Shader SDF** line anti-aliasing, VSync toggle, grid contrast control, and dedicated **Active Plane** palettes (`ObsidianDark`, `PureNight`, `GraphiteDark`, `CleanLight`).
5. **Parametric Modeling, Load Combinations, Interoperability & Engineering Reports:**
   - Modeling tools (`Select [S]`, `Add Node [N]`, `Add Member [B]`, `Measure [M]`, `Delete [Del]`) projected onto configurable `XY`, `XZ`, or `YZ` working planes with **Snap Grid**, local triad axes display, `LoadSystem` load cases/combinations, and **ABNT NBR** code audit (`LocalPdfVerified`: `NBR 6118:2023`, `NBR 6120:2019`, `NBR 6122:2022`, `NBR 6123:2023`, `NBR 7480:2022`, `NBR 8800:2024`; `OutOfLocalPdfScope` explicitly flagged for absent standards such as `NBR 8681`, `NBR 14762`, `NBR 15421`, `AISC 360`, and `Eurocode 3`);
   - Built-in editors for linear/J2 materials ($E, \nu, \alpha, \gamma, \rho$), parametric cross-sections (`Rectangular`, `Circular`, `I-Beam`, `Tubular`, and `Custom`), **3 ready-to-solve built-in structural examples opening in the default `XY (Plan)` work plane and remaining freely re-editable** (including clean inline dropdown editing in the Side Property Inspector and `Undo`/`Redo`), **DXF / IFC4 / VTK `UNSTRUCTURED_GRID` / CSV** interoperability (supporting accented UTF-8 paths via `Utf8Path.hpp`), open **`.omniframe` JSON Schema `2.1.0`** (with `"surfaceMesh"` backward-compatible with `1.0` & `2.0`), Section-9 **HTML / Markdown / A4 PDF** (`PdfReportGenerator`, items 1–20 + 2D/3D continuum) calculation report export, and integrated CLI **`omniframe-cli.exe`** (`--example`, `--nbr-audit`, `static|modal|buckling|nonlinear|pdelta|harmonic|spectrum|transient|seismic`, `--scheme newmark|hht`, `--load-factor`, `--modified-newton`, `--no-line-search`, `--imperfection`, `--install-windows-integration` for CMD/PowerShell, and `20` modular CTest targets).
6. **Adaptive Qt 6.11.1 Workspace, Guided Session (`F2`), Modeless Windows (`Z-Order`) & 100% Offline Privacy:**
   - Dynamic runtime switching for **Language** (`English (US)` / `Português (BR)`), **GUI Theme** (`Dark` / `Light`), and **`Settings > Windows CLI (CMD / PowerShell)`** propagated across all menus, native vector `QComboBox`/`QSpinBox` arrows rendered via `QProxyStyle` (without emitting temporary PNG files on disk, featuring flush outer rounded spin-button corners aligned with the numeric frame), collapsible dock panels with spin-button arrow toggles (`Inspector & Quick Input` collapsing horizontally to the left edge with `◀`/`▶` arrows and a `-90°` rotated label, and `Result Tables` with bottom `▼`/`▲` arrows), synchronized result tables (including the **`2D/3D Forces (Shell/Solid)`** tab), Active Plane palettes, engineering dialogs (`AnalysisSuiteDialog`, `LoadCombinationsDialog`, `MeshGeneratorDialog`, `PostProcessControlDialog`, `NbrComplianceDialog`, `PdfReportOptionsDialog`), the searchable **Operations Manual (`F1`, with quick-jump button to Chapter 10 CLI reference)**, and the **Guided Session (`F2` — `GuidedSessionDialog`)**;
   - **Step-by-Step Guided Session (`Help > Guided Session...`, `F2`):** enlarged layout displaying full button labels across **30 automated steps** on any chosen preset example defaulting to the `XY (Plan)` work plane across 4 scientific cognitive reading-speed profiles in WPM (Fluent, Technical, Read Aloud, and Dyslexia/Accessible), featuring a **unified two-phase spotlight & non-overlapping callout** and automatic restoration of the selected example to its initial state upon completion;
   - Centralized **Modeless** child-window stack management (`WindowZOrderManager`) preparing hidden native window handles prior to mapping to prevent transient ghost frames on single-click opening alongside managed **`Z-Order`** elevation;
   - Strictly **100% Local-First & Offline**: zero telemetry, zero network sockets, and local persistence restricted to user-saved project files plus three preference files (`language.json`, `theme.json`, and `graphics.json`) inside `%LOCALAPPDATA%\OmniFrame\OmniFrame\`.

---

### 📸 Interface Screenshots (`en-US`)

Actual captures of **OmniFrame 3D** localized in **English (US)** stored under [`repo/LISTAGEM/en_US/`](repo/LISTAGEM/en_US), showcasing all **3 built-in structural examples** rendered with **SSAA 2.00× / MSAA 16× / Line AA: Shader SDF**, collapsible dock panels (`Inspector &amp; Quick Input` on the left sidebar and `Result Tables &amp; Structural Diagnostics` at the bottom), **Active Plane** switching (`XY (Plan)` and `XZ (Front)`), displacement colorbars (`|u|`), applied loads, and support reactions:

<p align="center">
  <strong>Example 1 (en-US) — 3D Building Space Frame (2 Stories, Wind and Live Load) · Expanded Panels (3188×1346 px): Left Inspector with Rounded Spin Buttons, 3D Deformed Shape (7.60 mm), Support Reactions &amp; Result Tables (12 nodes, 16 members, 72 DOFs)</strong><br><br>
  <img src="repo/LISTAGEM/en_US/Captura%20de%20tela%202026-11-16%20154444.png" alt="OmniFrame 3D English (US) Example 1 3D Building Space Frame Expanded Panels" width="900">
</p>

<p align="center">
  <strong>Example 1 (en-US) — 3D Building Space Frame (2 Stories) · Collapsed Panels (3760×1794 px): Side-Collapsed Inspector (-90° Rotated Title, ▶) &amp; Minimized Bottom Dock (▲) Expanding the OpenGL Viewport with SSAA 2.00× / MSAA 16× / Shader SDF</strong><br><br>
  <img src="repo/LISTAGEM/en_US/Captura%20de%20tela%202026-11-16%20154459.png" alt="OmniFrame 3D English (US) Example 1 3D Building Space Frame Collapsed Panels" width="900">
</p>

<p align="center">
  <strong>Example 2 (en-US) — L-Shaped 3D Cantilever under Bending-Torsion · XZ (Front) Active Plane &amp; Collapsed Panels (3760×1794 px): 3D Deformed Shape (216.2 mm), Nodal Load &amp; Fixed-End Reactions (3 nodes, 2 members, 18 DOFs)</strong><br><br>
  <img src="repo/LISTAGEM/en_US/Captura%20de%20tela%202026-11-16%20154539.png" alt="OmniFrame 3D English (US) Example 2 L-Shaped 3D Cantilever XZ Front Plane" width="900">
</p>

<p align="center">
  <strong>Example 3 (en-US) — 3D Space Truss / Steel Footbridge · XY (Plan) Active Plane &amp; Collapsed Panels (3760×1794 px): 3D Deformed Shape (2.75 mm), Nodal Loads &amp; Support Reactions (20 nodes, 57 members, 120 DOFs)</strong><br><br>
  <img src="repo/LISTAGEM/en_US/Captura%20de%20tela%202026-11-16%20154558.png" alt="OmniFrame 3D English (US) Example 3 3D Space Truss Steel Footbridge" width="900">
</p>

---

### <a id="microsoft-store-privacy-support"></a>🛡️ Microsoft Store Distribution, Privacy, Support & Clean Uninstall

> [!IMPORTANT]
> **Microsoft Store Compliance Summary (Partner Center / MSIX / Policies v7.20):**
> - **Independent Privacy Policy (Policy 10.5):** **Microsoft does NOT provide a default privacy policy for OmniFrame 3D, and this software is NOT covered by the [Microsoft Privacy Statement](https://www.microsoft.com/pt-br/privacy/privacystatement).** Microsoft's Privacy Statement governs only data collected by the Microsoft Store platform or Windows OS. All computation inside OmniFrame 3D runs locally on the user's machine under its own [OmniFrame 3D Privacy Policy](docs/privacy-policy.md#enus).
> - **Independent Developer Support:** **Microsoft does NOT provide customer or technical support for OmniFrame 3D.** All support is offered exclusively by the individual developer (`Fernando Nillsson Cidade`) via email (`linceu_lighthouse@outlook.com` / [GitHub Issues](https://github.com/fernandoncidade/OmniFrame_cpp/issues)), best-effort and with no SLA.
> - **Restricted Capability `runFullTrust` (Policy 10.6):** Declared in [`repo/AppxManifest.xml`](repo/AppxManifest.xml) solely because OmniFrame 3D is a native Win32 C++23 / Qt 6.11.1 desktop application requiring hardware OpenGL 3.3 Core rendering and standard Windows Open/Save dialogs for local `.omniframe`, `.html`, and `.md` files, registering zero background services, zero kernel drivers, and zero network connections.
> - **Clean Uninstall Guarantee (Policy 10.2.7):** Full clean removal at any time via **Windows Settings → Apps → Installed Apps** (or Start Menu → Uninstall), leaving zero background processes or system modifications.

| Regulatory Provision | Official Repository Reference (`en-US`) |
|---|---|
| **Independent Privacy Policy** | [docs/privacy-policy.md](docs/privacy-policy.md#enus) · [In-App Asset (.txt)](source/assets/PRIVACY_POLICY/Privacy_Policy_en_US.txt) |
| **Developer Official Website** | [https://github.com/fernandoncidade/OmniFrame_cpp](https://github.com/fernandoncidade/OmniFrame_cpp) *(distinct from Microsoft Store listing)* |
| **Support Contact (Best-Effort)** | `linceu_lighthouse@outlook.com` · [GitHub Issues](https://github.com/fernandoncidade/OmniFrame_cpp/issues) *(no Microsoft support; no phone/SLA)* |
| **Store Official Package License** | [Official EULA (EN)](source/assets/EULA/EULA_en_US.txt) · [CLC (EN)](source/assets/CLC/CLC_en_US.txt) · [GNU AGPLv3 (Source)](LICENSE) |
| **Privacy Legal Basis** | User-requested local computation — zero personal data collection (LGPD Art. 7, V & IX / GDPR; local preferences in `%LOCALAPPDATA%\OmniFrame\OmniFrame\`) |

---

### <a id="quick-start"></a>🚀 Building, Testing & Running from Source

#### Prerequisites
- **Operating System:** Windows 11 / Windows 10 (build `17763+`) x64;
- **C++23 Compiler:** MSVC with full C++23 support (Visual Studio Build Tools / Community x64);
- **Qt 6.11.1:** `Core`, `Gui`, `Widgets`, `OpenGL`, `OpenGLWidgets`, and `LinguistTools`;
- **Eigen 3.4.0:** Locally detected or automatically fetched via CMake `FetchContent`;
- **Build Tools:** **CMake 3.28+** and **Ninja**.

#### Building with Official Presets (`CMakePresets.json`)
```powershell
# Clone the official repository
git clone https://github.com/fernandoncidade/OmniFrame_cpp.git
cd OmniFrame_cpp

# Configure and build the main application in build/OmniFrame3D/
cmake --preset omniframe-msvc-release
cmake --build --preset omniframe-msvc-release

# Configure, build, and run the 20 structural CTest checks in build/Testes/
cmake --preset testes-msvc-release
cmake --build --preset testes-msvc-release
ctest --preset testes-msvc-release --output-on-failure

# (Optional) Build standalone version and translation utilities
cmake --preset version-msvc-release && cmake --build --preset version-msvc-release
cmake --preset compile-language-msvc-release && cmake --build --preset compile-language-msvc-release

# Launch the main desktop GUI (or open a model directly from command line)
.\build\OmniFrame3D\OmniFrame.exe
.\build\OmniFrame3D\OmniFrame.exe "path\model.omniframe"

# Register and run omniframe-cli in Windows 10/11 (CMD & PowerShell, zero admin rights)
.\build\OmniFrame3D\omniframe-cli.exe --install-windows-integration --json
.\cmake\register_cli_windows.ps1 -CheckOnly -Json
omniframe-cli --example building --nbr-audit --json
```

#### Companion Command-Line Utilities (`mocks/`)
```powershell
# Audit version and date consistency across repository files
.\build\Version\VersionEditor.exe --check --root .

# Check and compile translation catalogs (.ts -> .qm)
.\build\CompileLanguage\CompileTranslationsTerminal.exe --check --root .
.\build\CompileLanguage\CompileTranslationsTerminal.exe --root . --verbose
```

*(For full execution and deployment details, see [`docs/cli-reference.md`](docs/cli-reference.md#enus).)*

---

### 🏛️ Auxiliary Technical Scope, Professional Responsibility & Exemption ("AS IS")

**OmniFrame 3D** is an auxiliary computational tool for **Finite Element structural analysis (linear static, modal, buckling, harmonic FRF, response spectrum, second-order $P$-$\Delta$/corotational, and J2 plasticity)** intended for academic study, teaching, research, and preliminary modeling, and may contain numerical approximations or scope limitations relative to nonlinear behavior. Outputs (displacements, support reactions, and internal force diagrams) **MUST NOT be used as sole final evidence in construction designs, civil/structural works, or production decisions without complete manual verification, evaluation of second-order ($P$-$\Delta$ and $P$-$\delta$) and stability effects, full normative load combinations (`ABNT NBR 8800`, `NBR 6118`, `AISC 360-22`, `Eurocode 3`), cross-validation, and formal oversight by a legally licensed engineer (including mandatory professional responsibility filings such as ART/RRT before CREA/CAU where applicable)**. The software is provided **"AS IS"** with all faults and without express or implied warranties; **all engineering deployment occurs at the user's sole risk, with complete legal, civil, criminal, and technical exemption of the author**.

---

### 📚 Technical Documentation & Compliance

- 🏛️ [System Architecture & FEA Matrix Derivations](docs/architecture.md#enus)
- 📋 [Standards Compliance & Numerical Benchmarks](docs/compliance.md#enus)
- 🔒 [Privacy Policy & Data Sovereignty](docs/privacy-policy.md#enus)
- ⚖️ [Dual Licensing & Microsoft Store MSIX Checklist](docs/licensing.md#enus)
- 🛡️ [Security & C++23 Resilience Model](docs/security-model.md#enus)
- 📖 [User Guide](docs/user-guide.md#enus) · [Complete Technical Manual (MANUAL.md)](MANUAL.md#part-2-operations-manual--3d-fea-engineering-guide-english---us)
- 💻 [Command-Line & Utilities Reference (CLI)](docs/cli-reference.md#enus)
- 💡 [Suggested Issues & Technical Backlog](docs/suggested-issues.md#enus)
- 🔬 [Technical Audit, ADRs, V&V Matrix & Roadmap](docs/technical-audit-and-roadmap.md)
- 🚀 [Release Notes for Version 2026.11.17.0](docs/release-v2026.11.17.0.md#enus)

---

### ⚖️ Licensing, Support & Community

- **Public Source Code (GitHub):** Licensed under **GNU Affero General Public License v3 (GNU AGPLv3)** ([`LICENSE`](LICENSE)), protected under Brazilian Software Law No. 9.609/1998, Copyright Law No. 9.610/1998, and international open-source treaties;
- **Official Microsoft Store Package (Free):** Digitally signed and distributed by the author under the **[Official EULA](source/assets/EULA/EULA_en_US.txt)** and **[CLC](source/assets/CLC/CLC_en_US.txt)** ([Official Download](https://apps.microsoft.com/detail/OmniFrame3D)), preserving any non-waivable statutory consumer and store-platform rights;
- **Third-Party Libraries:** Qt 6.11.1 (`GNU LGPLv3`), Eigen 3.4.0 (`Mozilla Public License 2.0`), Khronos OpenGL/GLSL — consolidated attributions in [`source/assets/NOTICES/NOTICE_en_US.txt`](source/assets/NOTICES/NOTICE_en_US.txt);
- **Support & Contributions:** Handled exclusively via email (`linceu_lighthouse@outlook.com`) and GitHub on a voluntary, non-urgent, best-effort basis with no SLA — see [`CONTRIBUTING.md`](CONTRIBUTING.md#enus).

</details>
