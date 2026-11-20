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

**Versão:** `2026.11.13.0`  
**Data:** `13 de novembro de 2026`  
**Autor:** Fernando Nillsson Cidade  

---

## 1. Visão Geral da Interface (`MainWindow`)

A interface do **OmniFrame 3D** organiza-se em cinco áreas principais:

1. **Barra de Menus:**
   - **Arquivo:** Novo Projeto (`Ctrl+N`), Abrir `.omniframe` (`Ctrl+O`), Salvar `.omniframe` (`Ctrl+S`), Exportar Relatório Técnico (HTML / Markdown) e Sair (`Ctrl+Q` / `Alt+F4`).
   - **Exemplos:** 3 modelos prontos integrados (Pórtico Espacial de Edifício de 2 Pavimentos, Balanço Espacial em L com Flexo-Torção e Treliça Espacial / Passarela Metálica 3D).
   - **Estrutura:** Materiais, Seções Transversais, Condições de Apoio, Rótulas / Liberações, Cargas Nodais Concentradas, Cargas Uniformemente Distribuídas, Ângulo de Roll ($\gamma$) e **Calcular Estrutura (`F5`)**.
   - **Exibir:** Vistas 3D Isométrica, Frontal (`XZ`), Planta (`XY`), Lateral (`YZ`) e Enquadrar Tudo (`F`).
   - **Idioma:** Alternância dinâmica entre **Português (Brasil)** e **English (US)**.
   - **Ajuda:** Diálogo **Sobre o OmniFrame 3D** (inclui Política de Privacidade, EULA, CLC, Avisos e Licenças).
2. **Barras de Ferramentas Superiores:**
   - **Barra 1 (Interação e Plano de Trabalho):** Modos `Selecionar (S)`, `Inserir Nó (N)`, `Inserir Barra (B)`, `Medir (M)`, `Excluir (Del)`, seletor de Plano (`XY`, `XZ`, `YZ`), Cota, `Snap Grid`, Passo, `Eixos Locais` e botões rápidos de vista (`3D Iso`, `Frontal`, `Planta`, `Lateral`, `Enquadrar (F)`).
   - **Barra 2 (Engenharia e Resultados):** Atalhos para os diálogos de engenharia, botão **Calcular Estrutura (`F5`)**, seletor de **Diagrama** (`Nenhum`, `Deformada 3D`, `Normal N`, `Cortante Vy`, `Cortante Vz`, `Torsor Tx`, `Fletor My`, `Fletor Mz`), alternância de **Reações** e controle deslizante **Escala** (`1` a `100`, padrão `30`, aplicando ampliação `1.5x` a `150x` na deformada 3D).
3. **Viewport Gráfica 3D Central (OpenGL 3.3 Core):** Renderização interativa da estrutura, eixos, apoios, cargas, deformada e diagramas de esforços.
4. **Dock Esquerdo (Inspetor e Entrada Rápida):** Entrada numérica exata de nós $(X, Y, Z)$ em metros e Inspetor dinâmico do nó ou barra selecionada.
5. **Dock Inferior (Tabelas de Resultados e Diagnóstico Estrutural):** Abas sincronizadas para **Deslocamentos Nodais**, **Reações de Apoio** e **Esforços nas Barras**.

---

## 2. Modelagem Estrutural Passo a Passo

### 2.1. Materiais e Seções Transversais
1. Acesse **Estrutura -> Materiais** (ou botão na Barra 2) para escolher presets (**Aço Estrutural**, **Concreto Armado**, **Alumínio**, **Madeira**) ou definir $E$, $\nu$, $\alpha$ e $\gamma$ customizados.
2. Acesse **Estrutura -> Seções Transversais** para configurar seções **Retangular Maciça**, **Circular Maciça**, **Tubo Circular**, **Perfil I / W** ou **Genérica** ($A, I_y, I_z, J$).

### 2.2. Criando Nós e Barras
- **Via Viewport:** Selecione o modo **`Inserir Nó (N)`** e clique no plano de trabalho (com ou sem *Snap Grid*); selecione **`Inserir Barra (B)`** e clique nos nós inicial e final em cadeia (pressione `Esc` para encerrar a cadeia).
- **Via Entrada Numérica:** No Dock Esquerdo (**Adicionar Nó por Coordenadas**), informe $X, Y, Z$ (em metros) e clique em **Adicionar Nó**.
- **Rótulas e Roll:** Com barras selecionadas, abra **Rótulas...** ou **Roll ($\gamma$)...** na Barra 2 ou no menu **Estrutura**.

### 2.3. Apoios e Cargas
- **Condições de Apoio:** Selecione nós e abra **Apoios...** para aplicar **Engaste**, **Apoio Fixo**, **Apoio Móvel Z**, **Livre**, **Molas Elásticas** ($k$) ou **Deslocamentos/Rotações Prescritos**.
- **Carregamentos:** Aplique **Cargas Nodais Concentradas** ($F_x, F_y, F_z, M_x, M_y, M_z$) nos nós selecionados ou **Cargas Uniformemente Distribuídas** ($q_x, q_y, q_z$ em sistema global ou local) nas barras selecionadas.

---

## 3. Executando o Solver e Exportando Resultados

1. Pressione **`F5`** ou clique em **Calcular Estrutura**.
2. Confira o equilíbrio global ($\sum F = 0, \sum M = 0$) na barra de status e inspecione os resultados no seletor **Diagrama** e nas tabelas inferiores.
3. Salve o projeto no formato JSON aberto **`.omniframe`** (`Ctrl+S`) ou exporte memoriais de cálculo completos em **HTML** ou **Markdown** pelo menu **Arquivo**.

---

## 4. Navegação na Viewport 3D e Atalhos de Teclado

### Mouse na Viewport 3D:
- **Orbitar / Rotacionar Câmera:** Arraste com o **Botão Direito** ou **Botão do Meio** do mouse.
- **Mover / Pan Lateral:** Segure **`Shift` + Botão Direito** (ou **`Shift` + Botão do Meio**) e arraste.
- **Zoom:** Gire a **Roda do Mouse**.
- **Interação (Selecionar / Inserir / Medir / Excluir):** **Botão Esquerdo do Mouse** (`Ctrl + Clique Esquerdo` alterna seleção múltipla no modo `Selecionar`).

### Atalhos Implementados:
- `Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Q`: Novo, Abrir `.omniframe`, Salvar `.omniframe`, Sair
- `F5`: Calcular Estrutura
- `F`: Enquadrar Tudo na Câmera (*Fit All*)
- `S` / `N` / `B` / `M`: Modos Selecionar / Inserir Nó / Inserir Barra / Medir
- `Del`: Ativar modo Excluir / Remover entidades selecionadas na viewport
- `Esc`: Cancelar inserção de barra/medição ativa e limpar seleção

---

## 5. Desinstalação Limpa (Windows / Microsoft Store)

Você pode desinstalar e remover o OmniFrame 3D de forma limpa a qualquer momento em **Configurações do Windows → Aplicativos → Aplicativos instalados** (ou clicando com o botão direito no **Menu Iniciar → Desinstalar**). Apenas projetos `.omniframe` e relatórios salvos em pastas escolhidas por você permanecem preservados sob seu controle (além de `%LOCALAPPDATA%\OmniFrame\OmniFrame\language.json` em instalações não empacotadas como MSIX).

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details open>
<summary>Click to expand / collapse user guide in English</summary><br>

# OmniFrame 3D — User Guide & Operations Manual

**Version:** `2026.11.13.0`  
**Date:** `November 13, 2026`  
**Author:** Fernando Nillsson Cidade  

---

## 1. Interface Overview (`MainWindow`)

The **OmniFrame 3D** workspace is organized into five main areas:

1. **Menu Bar:**
   - **File:** New Project (`Ctrl+N`), Open `.omniframe` (`Ctrl+O`), Save `.omniframe` (`Ctrl+S`), Export Technical Report (HTML / Markdown), and Quit (`Ctrl+Q` / `Alt+F4`).
   - **Examples:** 3 built-in models (2-Story 3D Building Space Frame, L-Shaped 3D Cantilever with Bending-Torsion, and 3D Space Truss / Steel Pedestrian Bridge).
   - **Structure:** Materials, Cross-Sections, Support Conditions, End Releases / Hinges, Concentrated Nodal Loads, Uniformly Distributed Loads, Roll Angle ($\gamma$), and **Solve Structure (`F5`)**.
   - **View:** 3D Isometric, Front (`XZ`), Top (`XY`), Side (`YZ`), and Fit All (`F`).
   - **Language:** Dynamic switching between **Português (Brasil)** and **English (US)**.
   - **Help:** **About OmniFrame 3D** dialog (includes Privacy Policy, EULA, CLC, Notices, and Licenses).
2. **Top Toolbars:**
   - **Toolbar 1 (Interaction & Work Plane):** Modes `Select (S)`, `Add Node (N)`, `Add Member (B)`, `Measure (M)`, `Delete (Del)`, Work Plane selector (`XY`, `XZ`, `YZ`), Elevation, `Snap Grid`, Step, `Local Axes`, and quick view buttons (`3D Iso`, `Front`, `Top`, `Side`, `Fit (F)`).
   - **Toolbar 2 (Engineering & Results):** Shortcuts to engineering dialogs, **Solve Structure (`F5`)** button, **Diagram** selector (`None`, `Deformed 3D`, `Normal N`, `Shear Vy`, `Shear Vz`, `Torsion Tx`, `Moment My`, `Moment Mz`), **Reactions** toggle, and **Scale** slider (`1` to `100`, default `30`, applying `1.5x` to `150x` amplification on the 3D deformed shape).
3. **Central 3D Graphics Viewport (OpenGL 3.3 Core):** Interactive rendering of structure, axes, supports, loads, deformed shape, and internal force diagrams.
4. **Left Dock (Inspector & Quick Entry):** Exact numerical node entry $(X, Y, Z)$ in meters and dynamic Property Inspector for the selected node or member.
5. **Bottom Dock (Results Tables & Structural Diagnostics):** Synchronized tabs for **Nodal Displacements**, **Support Reactions**, and **Member Forces**.

---

## 2. Step-by-Step Structural Modeling

### 2.1. Materials and Cross-Sections
1. Open **Structure -> Materials** (or the Toolbar 2 button) to choose presets (**Structural Steel**, **Reinforced Concrete**, **Aluminum**, **Timber**) or define custom $E$, $\nu$, $\alpha$, and $\gamma$ values.
2. Open **Structure -> Cross-Sections** to configure **Solid Rectangular**, **Solid Circular**, **Circular Pipe**, **I / W Profile**, or **Generic** sections ($A, I_y, I_z, J$).

### 2.2. Creating Nodes and Members
- **Via Viewport:** Select **`Add Node (N)`** mode and click on the work plane (with or without *Snap Grid*); select **`Add Member (B)`** and click start and end nodes in chain (press `Esc` to end the chain).
- **Via Numerical Entry:** In the Left Dock (**Add Node by Coordinates**), enter $X, Y, Z$ (in meters) and click **Add Node**.
- **Hinges & Roll:** With members selected, open **Hinges...** or **Roll ($\gamma$)...** on Toolbar 2 or under the **Structure** menu.

### 2.3. Supports and Loads
- **Support Conditions:** Select nodes and open **Supports...** to apply **Fixed**, **Pinned**, **Z-Roller**, **Free**, **Elastic Springs** ($k$), or **Prescribed Displacements/Rotations**.
- **Loading:** Apply **Concentrated Nodal Loads** ($F_x, F_y, F_z, M_x, M_y, M_z$) to selected nodes or **Uniformly Distributed Loads** ($q_x, q_y, q_z$ in global or local coordinates) to selected members.

---

## 3. Running the Solver and Exporting Results

1. Press **`F5`** or click **Solve Structure**.
2. Check global equilibrium ($\sum F = 0, \sum M = 0$) in the status bar and inspect results in the **Diagram** selector and bottom result tables.
3. Save the project in the open JSON **`.omniframe`** format (`Ctrl+S`) or export complete calculation reports in **HTML** or **Markdown** from the **File** menu.

---

## 4. 3D Viewport Navigation & Keyboard Shortcuts

### Mouse in the 3D Viewport:
- **Orbit / Rotate Camera:** Drag with the **Right Mouse Button** or **Middle Mouse Button**.
- **Move / Lateral Pan:** Hold **`Shift` + Right Button** (or **`Shift` + Middle Button**) and drag.
- **Zoom:** Scroll the **Mouse Wheel**.
- **Interaction (Select / Add / Measure / Delete):** **Left Mouse Button** (`Ctrl + Left-Click` toggles multi-selection in `Select` mode).

### Implemented Shortcuts:
- `Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Q`: New, Open `.omniframe`, Save `.omniframe`, Quit
- `F5`: Solve Structure
- `F`: Frame Structure in Camera (*Fit All*)
- `S` / `N` / `B` / `M`: Select / Add Node / Add Member / Measure modes
- `Del`: Activate Delete mode / Remove selected entities in the viewport
- `Esc`: Cancel active member/measurement operation and clear selection

---

## 5. Clean Uninstallation (Windows / Microsoft Store)

You may cleanly uninstall and remove OmniFrame 3D at any time via **Windows Settings → Apps → Installed Apps** (or by right-clicking in the **Start Menu → Uninstall**). Only `.omniframe` projects and reports saved in folders chosen by you remain preserved under your control (in addition to `%LOCALAPPDATA%\OmniFrame\OmniFrame\language.json` in non-MSIX unpackaged installations).

</details>
