# Guia do Usuário / User Guide — OmniFrame 3D

<p align="center">
  <b>Selecione o idioma / Select language:</b><br>
  <a href="#ptbr">🇧🇷 Português (BR)</a> | <a href="#enus">🇺🇸 English (US)</a>
</p>

---

## <a id="ptbr"></a>🇧🇷 Português (BR)

<details open>
<summary>Clique para expandir / recolher o guia do usuário em português</summary><br>

# OmniFrame 3D — Guia do Usuário e Manual de Operações

**Versão:** `2026.11.17.0`  
**Data:** `17 de novembro de 2026`  
**Autor:** Fernando Nillsson Cidade  

---

## 1. Visão Geral da Interface (`MainWindow`)

A interface do **OmniFrame 3D** organiza-se em cinco áreas principais:

1. **Barra de Menus:**
   - **Arquivo:** Novo Projeto (`Ctrl+N`), Abrir `.omniframe` (`Ctrl+O`), Salvar `.omniframe` (`Ctrl+S`), Importar / Exportar **DXF** e **BIM IFC4**, Exportar Relatório Técnico (**HTML / Markdown / PDF A4 Executivo**) e Sair (`Ctrl+Q` / `Alt+F4`).
   - **Editar:** **`Desfazer` (`Ctrl+Z`)** e **`Refazer` (`Ctrl+Y`)** cobrindo modelagem, edição de propriedades e alterações nos exemplos.
   - **Exemplos:** 3 modelos prontos integrados (Pórtico Espacial de Edifício de 2 Pavimentos, Balanço Espacial em L com Flexo-Torção e Treliça Espacial / Passarela Metálica 3D), todos abrindo por padrão no plano **`XY (Planta)`** e totalmente abertos para reedição livre, troca de propriedades e recálculo pelo usuário.
   - **Estrutura / Análise:** Materiais (elástico linear e **Plasticidade J2** $\sigma_y, H_{iso}, H_{kin}$), Seções Transversais, Condições de Apoio, Rótulas / Liberações, Cargas Nodais Concentradas, Cargas Uniformemente Distribuídas, **Cargas Térmicas ($\Delta T$)...**, Ângulo de Roll ($\gamma$), **Combinações de Carregamento...**, **Gerador de Malha 1D/2D/3D...** (com material, espessura, contorno e pressão integrados), **Análise Avançada...** (Estática, Modal, Flambagem com imperfeição inicial, Harmônica FRF, Espectro de Resposta, Sísmica/Transiente Newmark-Beta/HHT-Alpha com $F(t)$ e P-Delta / Não Linear Corrotacional 3D), **Pós-Processamento 3D...** (*Clip Plane*, **Tensão de von Mises (Casca/Sólido)**, contorno DCR, animação), **Auditoria / Verificação ABNT NBR...** e **Calcular Estrutura (`F5`)** em thread assíncrona.
   - **Exibir:** Vistas 3D Isométrica, Frontal (`XZ`), Planta (`XY`), Lateral (`YZ`), Enquadrar Tudo (`F`), alternância dinâmica de **Tema** (Claro / Escuro / Alto Contraste) e **Qualidade Gráfica**.
   - **Idioma / Configurações:** Alternância dinâmica em tempo de execução entre **Português (Brasil)** e **English (US)**, perfil visual e **`Configurações > CLI Windows (CMD / PowerShell)`** (registro não administrativo no Windows, verificação de status e abertura direta de terminal CMD/PowerShell com `omniframe-cli`).
   - **Ajuda:** Manual de Operações (`F1`, com botão de atalho **`Comandos CLI (CMD/PowerShell)`**), **`Sessão Guiada...` (`F2`)** e diálogo **Sobre o OmniFrame 3D** (inclui Notas de Lançamento, Política de Privacidade, EULA, CLC, Avisos e Licenças).
2. **Barras de Ferramentas Superiores:**
   - **Barra 1 (Interação e Plano de Trabalho):** Modos `Selecionar (S)`, `Inserir Nó (N)`, `Inserir Barra (B)`, `Medir (M)`, `Excluir (Del)`, seletor de Plano (`XY (Planta)` como padrão inicial e nos 3 exemplos, `XZ (Frontal)`, `YZ (Lateral)`), Cota, `Snap Grid`, Passo, `Eixos Locais` e botões rápidos de vista (`3D Iso`, `Frontal`, `Planta`, `Lateral`, `Enquadrar (F)`).
   - **Barra 2 (Engenharia e Resultados):** Atalhos diretos (`Materiais...`, `Seções...`, `Apoios...`, `Rótulas...`, `Nodais...`, `Distribuídas...`, `Térmica (ΔT)...`, `Roll (γ)...`, `Combinações...`, `Malha 2D/3D...`, `Avançadas...`, `Corte/Contornos...`, `Auditoria NBR...`), botão **Calcular Estrutura (`F5`)**, seletor de **Diagrama** (`Nenhum`, `Deformada 3D`, `Normal N`, `Cortante Vy`, `Cortante Vz`, `Torsor Tx`, `Fletor My`, `Fletor Mz`), alternância de **Reações** e controle deslizante **Escala** (`1` a `100`, padrão `30`, aplicando ampliação `1.5x` a `150x` na deformada 3D). Todas as janelas de engenharia abrem com **clique único**, sem qualquer contorno fantasma transitório, e em modo **não-modal (*modeless*)** com **Z-Order** estável sobre a janela principal.
3. **Viewport Gráfica 3D Central (OpenGL 3.3 Core):** Renderização interativa da estrutura, eixos, apoios, cargas, deformada, malhas 2D/3D, planos de corte (*Clip Plane*), contornos `|u|`, **Tensão de von Mises (Casca/Sólido)** e DCR, animações modais e diagramas de esforços.
4. **Dock Esquerdo (Inspetor e Entrada Rápida):** Entrada numérica exata de nós $(X, Y, Z)$ em metros (com botões *spin* de cantos arredondados acompanhando a moldura dos campos) e painel **`Elemento / Nó Selecionado`** com listas suspensas diretas **`Material:`** e **`Seção Transversal:`** (exibindo nomes limpos, sem prefixo `#ID`), edição direta de propriedades e botões **`Mesmo Material`** / **`Mesma Seção`** para seleção e ajuste em lote. No cabeçalho, a seta estilo *spin button* (**`◀`** para recolher / **`▶`** para expandir) recolhe o painel para a lateral esquerda com rótulo rotacionado **90° no sentido anti-horário**, ampliando a área livre do Viewport 3D (funcionando também em modo flutuante e expandindo automaticamente na Sessão Guiada `F2`).
5. **Dock Inferior (Tabelas de Resultados e Diagnóstico Estrutural):** Abas sincronizadas para **Deslocamentos Nodais**, **Reações de Apoio**, **Esforços nas Barras** e **`Esforços 2D/3D (Casca/Sólido)`** (resultantes de placa e tensões principais/von Mises), além de botão estilo *spin button* no cabeçalho (**`▼`** para recolher / **`▲`** para expandir) para compactar as tabelas na borda inferior (ou em janela flutuante) liberando altura vertical para o modelo 3D.

---

## 2. Modelagem Estrutural Passo a Passo

### 2.1. Materiais, Seções Transversais e Inspetor Direto
1. Acesse **Estrutura -> Materiais** (ou botão na Barra 2) para escolher presets (**Aço Estrutural**, **Concreto Armado**, **Alumínio**, **Madeira**) ou definir $E$, $\nu$, $\alpha$, $\gamma$, massa específica $\rho$ e parâmetros opcionais de **Plasticidade J2** ($\sigma_y, H_{iso}, H_{kin}$).
2. Acesse **Estrutura -> Seções Transversais** para configurar seções **Retangular Maciça**, **Circular Maciça**, **Tubo Circular**, **Perfil I / W** ou **Genérica** ($A, I_y, I_z, J$).
3. Ao selecionar qualquer nó ou barra (inclusive após carregar qualquer um dos **3 exemplos pré-definidos** para fins de estudo e exercício), utilize diretamente no Dock Esquerdo (**`Elemento / Nó Selecionado`**) as listas suspensas **`Material:`** e **`Seção Transversal:`** para reatribuir propriedades instantaneamente, ou clique em **`Mesmo Material`** / **`Mesma Seção`** para editar múltiplos elementos juntos. Todas as alterações aceitam **`Desfazer` (`Ctrl+Z`)** e **`Refazer` (`Ctrl+Y`)**.

### 2.2. Criando Nós, Barras e Malhas Paramétricas
- **Via Viewport:** Selecione o modo **`Inserir Nó (N)`** e clique no plano de trabalho (com ou sem *Snap Grid*); selecione **`Inserir Barra (B)`** e clique nos nós inicial e final em cadeia (pressione `Esc` para encerrar a cadeia).
- **Via Entrada Numérica:** No Dock Esquerdo (**Adicionar Nó por Coordenadas**), informe $X, Y, Z$ (em metros) e clique em **Adicionar Nó**.
- **Via Gerador de Malha:** Abra **Malha 2D/3D...** para gerar malhas 1D (`Line2`), placas/cascas 2D (`Quad4`, `Quad8`, `Tri3`), domínios triangulados por Delaunay ou sólidos 3D (`Hex8`, `Hex20`, `Tet4`, `Extrusão 2D -> 3D Hex8`), definindo diretamente **`Material da Malha:`**, **`Espessura (Casca 2D):`**, **`Condição de Contorno:`** e **`Pressão / Carga Z (2D/3D):`** para resolução unificada junto com barras 1D ao pressionar `F5`.
- **Rótulas e Roll:** Com barras selecionadas, abra **Rótulas...** ou **Roll ($\gamma$)...** na Barra 2 ou no menu **Estrutura**.

### 2.3. Apoios, Cargas, Térmica e Combinações
- **Condições de Apoio:** Selecione nós e abra **Apoios...** para aplicar **Engaste**, **Apoio Fixo**, **Apoio Móvel Z**, **Livre**, **Molas Elásticas** ($k$) ou **Deslocamentos/Rotações Prescritos**.
- **Carregamentos e Combinações:** Aplique **Cargas Nodais Concentradas** ($F_x, F_y, F_z, M_x, M_y, M_z$) nos nós selecionados, **Cargas Uniformemente Distribuídas** ($q_x, q_y, q_z$ em sistema global ou local) e **Cargas Térmicas ($\Delta T$)** nas barras selecionadas, e gerencie casos/combinações normativas em **Combinações...**.

### 2.4. Sessão Guiada Interativa (`Ajuda > Sessão Guiada...`, `F2`)
- Ao acionar **Ajuda > Sessão Guiada...** (ou **`F2`**), o diálogo abre dimensionado confortavelmente para exibir integralmente todos os controles, carregando qualquer um dos **3 exemplos pré-definidos** no plano **`XY (Planta)`** conforme o **`Ritmo de Leitura (PPM):`** escolhido (**`Leitor Típico — Fluente (250 PPM)`**, **`Leitura Técnica / Engenharia (150 PPM)`**, **`Leitura em Voz Alta (155 PPM)`** ou **`Dislexia / Acessível (130 PPM + Regressão)`**).
- A janela da **Sessão Guiada** opera como janela **não-modal (*modeless*)** no topo do **Z-Order**, destacando visualmente o botão, menu ou campo em uso (incluindo malhas 2D/3D, contornos von Mises, imperfeições de flambagem e análise transiente) e exibindo um **balão unificado em duas fases** (amarelo antes da ação e verde na execução, com duração proporcional à quantidade de palavras e posicionamento inteligente para não cobrir as janelas filhas no canto inferior esquerdo). O visual adapta-se dinamicamente ao tema e ao idioma ativos e, ao concluir ou encerrar, o modelo selecionado é **restaurado automaticamente ao seu estado inicial**.

---

## 3. Executando o Solver e Exportando Resultados

1. Pressione **`F5`** ou clique em **Calcular Estrutura** (execução assíncrona sem bloqueio da interface), ou abra **Avançadas...** para selecionar o **`Solver:`** linear (`Auto`, `SimplicialLDLT`, `SparseLU`, `ConjugateGradient`), rodar estudos **Modais**, **Flambagem** (com **`Imperfeição Inicial (amp):`** e botão **`Aplicar Imperfeição à Geometria`**), **Harmônico (FRF)**, **Espectro de Resposta**, **Sísmica / Transiente** (`Newmark-Beta` ou `HHT-Alpha (α=-0,05)` com **`Acelerograma Sísmico (a_g)`** ou **`Histórico de Força F(t)`**) ou **P-Delta / Não Linear** (`Corrotacional 3D`, **`Tangente Completa`**, **`Line Search`**, NR / Arc-Length).
2. Confira o equilíbrio global ($\sum F = 0, \sum M = 0$) na barra de status e inspecione os resultados no seletor **Diagrama**, em **Corte/Contornos...** (*Clip Plane*, mapa **`Tensão de von Mises (Casca/Sólido)`** e DCR, animação modal/deformada), em **Auditoria NBR...** e nas 4 abas das tabelas inferiores (incluindo **`Esforços 2D/3D (Casca/Sólido)`**).
3. Salve o projeto no formato JSON **`.omniframe`** (Schema `2.1.0`, retrocompatível com `1.0` e `2.0`), troque geometria em **DXF / BIM IFC4** ou exporte memoriais de cálculo completos em **HTML**, **Markdown** ou **PDF (A4 executivo)**.

---

## 4. Navegação na Viewport 3D e Atalhos de Teclado

### Mouse na Viewport 3D:
- **Orbitar / Rotacionar Câmera:** Arraste com o **Botão Direito** ou **Botão do Meio** do mouse.
- **Mover / Pan Lateral:** Segure **`Shift` + Botão Direito** (ou **`Shift` + Botão do Meio**) e arraste.
- **Zoom:** Gire a **Roda do Mouse**.
- **Interação (Selecionar / Inserir / Medir / Excluir):** **Botão Esquerdo do Mouse** (`Ctrl + Clique Esquerdo` alterna seleção múltipla no modo `Selecionar`).

### Atalhos Implementados:
- `Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Q`: Novo, Abrir `.omniframe`, Salvar `.omniframe`, Sair
- `Ctrl+Z`, `Ctrl+Y`: Desfazer / Refazer alteração de modelagem ou propriedade
- `F1` / `F2`: Abrir Manual de Operações / Iniciar Sessão Guiada
- `F5`: Calcular Estrutura
- `F`: Enquadrar Tudo na Câmera (*Fit All*)
- `S` / `N` / `B` / `M`: Modos Selecionar / Inserir Nó / Inserir Barra / Medir
- `Del`: Ativar modo Excluir / Remover entidades selecionadas na viewport
- `Esc`: Cancelar inserção de barra/medição ativa e limpar seleção

---

## 5. Desinstalação Limpa (Windows / Microsoft Store)

Você pode desinstalar e remover o OmniFrame 3D de forma limpa a qualquer momento em **Configurações do Windows → Aplicativos → Aplicativos instalados** (ou clicando com o botão direito no **Menu Iniciar → Desinstalar**). Apenas projetos `.omniframe`, arquivos DXF/IFC e relatórios salvos em pastas escolhidas por você permanecem preservados sob seu controle (além de `language.json`, `theme.json` e `graphics.json` em `%LOCALAPPDATA%\OmniFrame\OmniFrame\` em instalações não empacotadas como MSIX).

---

## 6. Linha de Comando no Windows 10/11 (`omniframe-cli.exe` e `OmniFrame.exe`)

- **Acesso Rápido pela Interface:** Use **`Configurações > CLI Windows (CMD / PowerShell)`** para registrar o comando `omniframe-cli` no Windows sem privilégios de administrador, checar a associação (`.omniframe`, `App Paths`, `PATH`) ou abrir imediatamente um Prompt de Comando (`CMD`) ou `PowerShell` pronto.
- **Abertura Direta pela Linha de Comando:** Execute `OmniFrame.exe "modelo.omniframe"` (ou `.dxf`/`.ifc`) para abrir o projeto na interface 3D, ou use `omniframe-cli` no CMD/PowerShell para validação (`--validate`), modelos integrados (`--example building|cantilever|truss`), auditoria (`--nbr-audit`), análises (`--analysis`), imperfeições (`--imperfection`), integradores (`--scheme hht|newmark`) e exportações/memoriais (`--export`, `--report`). Veja a referência completa em [`docs/cli-reference.md`](cli-reference.md#ptbr) e no Capítulo 10 do Manual (`F1`).

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details open>
<summary>Click to expand / collapse user guide in English</summary><br>

# OmniFrame 3D — User Guide & Operations Manual

**Version:** `2026.11.17.0`  
**Date:** `November 17, 2026`  
**Author:** Fernando Nillsson Cidade  

---

## 1. Interface Overview (`MainWindow`)

The **OmniFrame 3D** workspace is organized into five main areas:

1. **Menu Bar:**
   - **File:** New Project (`Ctrl+N`), Open `.omniframe` (`Ctrl+O`), Save `.omniframe` (`Ctrl+S`), Import / Export **DXF** and **BIM IFC4**, Export Technical Report (**HTML / Markdown / Executive A4 PDF**), and Quit (`Ctrl+Q` / `Alt+F4`).
   - **Edit:** **`Undo` (`Ctrl+Z`)** and **`Redo` (`Ctrl+Y`)** covering geometry edits, property changes, and modifications made on examples.
   - **Examples:** 3 built-in models (2-Story 3D Building Space Frame, L-Shaped 3D Cantilever with Bending-Torsion, and 3D Space Truss / Steel Pedestrian Bridge), all opening in the **`XY (Plan)`** work plane by default and freely open for live re-editing, property adjustments, and re-solving as practice exercises.
   - **Structure / Analysis:** Materials (linear elastic & **J2 Plasticity** $\sigma_y, H_{iso}, H_{kin}$), Cross-Sections, Support Conditions, End Releases / Hinges, Concentrated Nodal Loads, Uniformly Distributed Loads, **Thermal Loads ($\Delta T$)...**, Roll Angle ($\gamma$), **Load Combinations...**, **Mesh Generator 1D/2D/3D...** (with integrated material, thickness, boundary condition, and pressure inputs), **Advanced Analysis...** (Static, Modal, Buckling with initial imperfection seeding, Harmonic FRF, Response Spectrum, Newmark-Beta / HHT-Alpha Transient & Force Time History $F(t)$, and P-Delta / Nonlinear 3D Corotational), **3D Post-Processing...** (*Clip Plane*, **von Mises Stress (Shell/Solid)** contour, DCR contour, animation), **ABNT NBR Audit / Compliance...**, and **Solve Structure (`F5`)** asynchronously on worker threads.
   - **View:** 3D Isometric, Front (`XZ`), Top (`XY`), Side (`YZ`), Fit All (`F`), dynamic **Theme** switching (Light / Dark / High Contrast), and **Graphics Quality**.
   - **Language / Settings:** Runtime switching between **Português (Brasil)** and **English (US)**, graphics/theme preferences, and **`Settings > Windows CLI (CMD / PowerShell)`** (non-admin Windows registration, status audit, and instant CMD/PowerShell terminal launch).
   - **Help:** Operations Manual (`F1`, including the **`CLI Commands (CMD/PowerShell)`** jump button), **`Guided Session...` (`F2`)**, and **About OmniFrame 3D** dialog (includes Release Notes, Privacy Policy, EULA, CLC, Notices, and Licenses).
2. **Top Toolbars:**
   - **Toolbar 1 (Interaction & Work Plane):** Modes `Select (S)`, `Add Node (N)`, `Add Member (B)`, `Measure (M)`, `Delete (Del)`, Work Plane selector (`XY (Plan)` as startup and built-in example default, `XZ (Front)`, `YZ (Side)`), Elevation, `Snap Grid`, Step, `Local Axes`, and quick view buttons (`3D Iso`, `Front`, `Top`, `Side`, `Fit (F)`).
   - **Toolbar 2 (Engineering & Results):** Direct dialog shortcuts (`Materials...`, `Sections...`, `Supports...`, `Hinges...`, `Nodal...`, `Distributed...`, `Thermal (ΔT)...`, `Roll (γ)...`, `Combinations...`, `Mesh 2D/3D...`, `Advanced...`, `Clip/Contours...`, `NBR Audit...`), **Solve Structure (`F5`)** button, **Diagram** selector (`None`, `Deformed 3D`, `Normal N`, `Shear Vy`, `Shear Vz`, `Torsion Tx`, `Moment My`, `Moment Mz`), **Reactions** toggle, and **Scale** slider (`1` to `100`, default `30`, applying `1.5x` to `150x` amplification on the 3D deformed shape). All engineering dialogs open on a **single click**, without any transient ghost window outline, and run as **modeless** windows with managed **Z-Order** above the main window.
3. **Central 3D Graphics Viewport (OpenGL 3.3 Core):** Interactive rendering of structure, axes, supports, loads, deformed shape, 2D/3D meshes, 3D clip planes, `|u|`, **von Mises Stress (Shell/Solid)**, and DCR contours, modal animations, and internal force diagrams.
4. **Left Dock (Inspector & Quick Entry):** Exact numerical node entry $(X, Y, Z)$ in meters (featuring rounded outer corners on spin buttons aligned seamlessly to the field frame) and the **`Selected Element / Node`** Inspector with inline **`Material:`** and **`Cross-Section:`** dropdown lists (showing clean names without `#ID` prefixes), direct property fields, and **`Same Material`** / **`Same Section`** batch-selection buttons. In the panel header, the spin-button arrow (**`◀`** to collapse / **`▶`** to expand) collapses the dock horizontally to the left edge with its title rotated **90° counter-clockwise** to maximize 3D Viewport width (also operating in floating mode and auto-expanding during Guided Session `F2`).
5. **Bottom Dock (Results Tables & Structural Diagnostics):** Synchronized tabs for **Nodal Displacements**, **Support Reactions**, **Member Forces**, and **`2D/3D Forces (Shell/Solid)`** (plate resultants and principal/von Mises stresses), paired with a header spin-button arrow (**`▼`** to collapse / **`▲`** to expand) that compacts the tables cleanly along the bottom border (or while floating) to reclaim vertical height for the 3D scene.

---

## 2. Step-by-Step Structural Modeling

### 2.1. Materials, Cross-Sections & Direct Inspector
1. Open **Structure -> Materials** (or the Toolbar 2 button) to choose presets (**Structural Steel**, **Reinforced Concrete**, **Aluminum**, **Timber**) or define custom $E$, $\nu$, $\alpha$, $\gamma$, density $\rho$, and optional **J2 Plasticity** parameters ($\sigma_y, H_{iso}, H_{kin}$).
2. Open **Structure -> Cross-Sections** to configure **Solid Rectangular**, **Solid Circular**, **Circular Pipe**, **I / W Profile**, or **Generic** sections ($A, I_y, I_z, J$).
3. When any node or member is selected (including right after loading any of the **3 built-in examples** for hands-on experimentation), use the inline **`Material:`** and **`Cross-Section:`** dropdowns in **`Selected Element / Node`** to reassign properties immediately, or click **`Same Material`** / **`Same Section`** to select and modify matching members together. Every edit supports **`Undo` (`Ctrl+Z`)** and **`Redo` (`Ctrl+Y`)**.

### 2.2. Creating Nodes, Members & Parametric Meshes
- **Via Viewport:** Select **`Add Node (N)`** mode and click on the work plane (with or without *Snap Grid*); select **`Add Member (B)`** and click start and end nodes in chain (press `Esc` to end the chain).
- **Via Numerical Entry:** In the Left Dock (**Add Node by Coordinates**), enter $X, Y, Z$ (in meters) and click **Add Node**.
- **Via Mesh Generator:** Open **Mesh 2D/3D...** to generate 1D beams (`Line2`), 2D plates/shells (`Quad4`, `Quad8`, `Tri3`), Bowyer-Watson Delaunay triangulations, or 3D solids (`Hex8`, `Hex20`, `Tet4`, `Extrusion 2D -> 3D Hex8`), directly configuring **`Mesh Material:`**, **`Thickness (2D Shell):`**, **`Boundary Condition:`**, and **`Pressure / Z Load (2D/3D):`** for unified 1D/2D/3D solving when pressing `F5`.
- **Hinges & Roll:** With members selected, open **Hinges...** or **Roll ($\gamma$)...** on Toolbar 2 or under the **Structure** menu.

### 2.3. Supports, Loads, Thermal Actions & Combinations
- **Support Conditions:** Select nodes and open **Supports...** to apply **Fixed**, **Pinned**, **Z-Roller**, **Free**, **Elastic Springs** ($k$), or **Prescribed Displacements/Rotations**.
- **Loading & Combinations:** Apply **Concentrated Nodal Loads** ($F_x, F_y, F_z, M_x, M_y, M_z$) to selected nodes, **Uniformly Distributed Loads** ($q_x, q_y, q_z$ in global or local coordinates) and **Thermal Loads ($\Delta T$)** to selected members, and configure ULS/SLS load cases and combinations via **Combinations...**.

### 2.4. Interactive Guided Session (`Help > Guided Session...`, `F2`)
- Launch **Help > Guided Session...** (or press **`F2`**): the controller opens sized comfortably so every button label and input remains unclipped, loading any of the **3 built-in examples** in the default **`XY (Plan)`** work plane at your chosen **`Reading Pace (WPM):`** profile (**`Typical Reader — Fluent (250 WPM)`**, **`Technical / Engineering Reading (150 WPM)`**, **`Reading Aloud (155 WPM)`**, or **`Dyslexia / Accessible (130 WPM + Regression)`**).
- The **Guided Session** controller runs as a **modeless** dialog at the top of the **Z-Order**, highlighting each active button, menu item, or input widget (including 2D/3D meshing, von Mises stress contours, buckling imperfections, and transient analysis) and showing a **unified two-phase callout** (yellow preparation phase and green action phase, both paced proportionally to word count and placed so they never overlap lower-left child dialogs). Its visuals update dynamically on theme or language changes, and upon finishing or closing, the chosen example is **automatically restored to its initial state**.

---

## 3. Running the Solver and Exporting Results

1. Press **`F5`** or click **Solve Structure** (runs asynchronously without blocking the GUI), or open **Advanced...** to pick the linear **`Solver:`** (`Auto`, `SimplicialLDLT`, `SparseLU`, `ConjugateGradient`), run **Modal**, **Buckling** (with **`Initial Imperfection (amp):`** and **`Apply Imperfection to Geometry`**), **Harmonic FRF**, **Response Spectrum**, **Seismic / Transient** (`Newmark-Beta` or `HHT-Alpha (α=-0.05)` using **`Seismic Accelerogram (a_g)`** or **`Force Time History F(t)`**), **P-Delta**, or **Nonlinear** (`Corotational 3D`, **`Full Tangent`**, **`Line Search`**, NR / Arc-Length) runs.
2. Check global equilibrium ($\sum F = 0, \sum M = 0$) in the status bar and inspect results in the **Diagram** selector, **Clip/Contours...** (Clip Plane, **`von Mises Stress (Shell/Solid)`** & DCR heatmaps, modal/deformed animation), **NBR Audit...**, and the 4 bottom result tabs (including **`2D/3D Forces (Shell/Solid)`**).
3. Save the project in the open JSON **`.omniframe`** format (Schema `2.1.0`, backward-compatible with `1.0` & `2.0`), exchange geometry via **DXF / BIM IFC4**, or export executive calculation reports in **HTML**, **Markdown**, or **A4 PDF**.

---

## 4. 3D Viewport Navigation & Keyboard Shortcuts

### Mouse in the 3D Viewport:
- **Orbit / Rotate Camera:** Drag with the **Right Mouse Button** or **Middle Mouse Button**.
- **Move / Lateral Pan:** Hold **`Shift` + Right Button** (or **`Shift` + Middle Button**) and drag.
- **Zoom:** Scroll the **Mouse Wheel**.
- **Interaction (Select / Add / Measure / Delete):** **Left Mouse Button** (`Ctrl + Left-Click` toggles multi-selection in `Select` mode).

### Implemented Shortcuts:
- `Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Q`: New, Open `.omniframe`, Save `.omniframe`, Quit
- `Ctrl+Z`, `Ctrl+Y`: Undo / Redo modeling or property change
- `F1` / `F2`: Open Operations Manual / Start Guided Session
- `F5`: Solve Structure
- `F`: Frame Structure in Camera (*Fit All*)
- `S` / `N` / `B` / `M`: Select / Add Node / Add Member / Measure modes
- `Del`: Activate Delete mode / Remove selected entities in the viewport
- `Esc`: Cancel active member/measurement operation and clear selection

---

## 5. Clean Uninstallation (Windows / Microsoft Store)

You may cleanly uninstall and remove OmniFrame 3D at any time via **Windows Settings → Apps → Installed Apps** (or by right-clicking in the **Start Menu → Uninstall**). Only `.omniframe` projects, DXF/IFC files, and reports saved in folders chosen by you remain preserved under your control (in addition to `language.json`, `theme.json`, and `graphics.json` under `%LOCALAPPDATA%\OmniFrame\OmniFrame\` in non-MSIX unpackaged installations).

---

## 6. Windows 10/11 Command-Line Interface (`omniframe-cli.exe` & `OmniFrame.exe`)

- **Quick Access from the Desktop UI:** Open **`Settings > Windows CLI (CMD / PowerShell)`** to register `omniframe-cli` in Windows without Administrator rights, verify `.omniframe` / `App Paths` / `PATH` registration, or open a preconfigured **CMD** or **PowerShell** session directly.
- **Direct CLI & GUI File Launch:** Run `OmniFrame.exe "model.omniframe"` (or `.dxf`/`.ifc`) to open a model straight into the 3D workspace, or run `omniframe-cli` in CMD/PowerShell for offline validation (`--validate`), built-in examples (`--example building|cantilever|truss`), compliance (`--nbr-audit`), solvers (`--analysis`), imperfections (`--imperfection`), integrators (`--scheme hht|newmark`), and exports/reports (`--export`, `--report`). See [`docs/cli-reference.md`](cli-reference.md#enus) and Chapter 10 of the built-in Manual (`F1`).

</details>
