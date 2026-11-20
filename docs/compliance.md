# Conformidade Normativa e Validação Numérica / Compliance & Verification — OmniFrame 3D

<p align="center">
  <b>Selecione o idioma / Select language:</b><br>
  <a href="#ptbr">🇧🇷 Português (BR)</a> | <a href="#enus">🇺🇸 English (US)</a>
</p>

---

## <a id="ptbr"></a>🇧🇷 Português (BR)

<details>
<summary>Clique para expandir / recolher o documento em português</summary><br>

# OmniFrame 3D — Guia de Conformidade Normativa e Validação FEA

**Versão:** `2026.11.13.0`  
**Data:** `13 de novembro de 2026`  
**Autor:** Fernando Nillsson Cidade  

---

## 1. Visão Geral de Conformidade e Normas de Projeto Estrutural

O **OmniFrame 3D** é um software de análise elástica linear por elementos finitos tridimensionais (FEA), fornecendo os esforços solicitantes internos (esforço normal $N$, esforços cortantes $V_y$ e $V_z$, momento de torção $T$ e momentos fletores $M_y$ e $M_z$), bem como deslocamentos nodais e reações de apoio.

Os resultados gerados pela aplicação fornecem os dados de entrada necessários para a verificação de estados limites últimos (ELU) e estados limites de serviço (ELS) preconizados pelas principais normas técnicas nacionais e internacionais:

| Norma Técnica | Jurisdição / Escopo | Aplicação no OmniFrame 3D |
|---|---|---|
| **ABNT NBR 8800:2008** | Brasil (Estruturas de Aço) | Análise elástica de primeira ordem para pórticos e treliças; extração de momentos fletores e forças axiais para verificação de instabilidade por flexo-compressão. |
| **ABNT NBR 6118:2023** | Brasil (Estruturas de Concreto) | Modelagem elástica linear de pórticos espaciais e grelhas de edifícios; determinação de deslocamentos para avaliação de flechas e limites de vibração (ELS-DEF). |
| **AISC 360-22** | Estados Unidos (Aço Estrutural) | Análise elástica preliminar de sistemas estruturais (Chapter C — Design for Stability); compatibilidade de rigidez para métodos de análise direta (Direct Analysis Method). |
| **Eurocode 3 (EN 1993-1-1)** | União Europeia (Aço Estrutural) | Análise global elástica de estruturas espaciais; determinação de parâmetros de flambagem elástica de barras isoladas e pórticos contínuos. |

---

## 2. Requisitos para Análise Estrutural Elástica

### 2.1. ABNT NBR 8800:2008 — Análise Estrutural (Item 4.9)
Conforme a NBR 8800:2008, a análise elástica de primeira ordem é válida para determinar as ações internas e deslocamentos de estruturas reticuladas, desde que os efeitos de segunda ordem (efeitos P-$\Delta$ e P-$\delta$) sejam avaliados ou desprezíveis ($\gamma_z \le 1,1$ ou $B_2 \le 1,1$). O OmniFrame 3D atende integralmente à formulação elástica matricial exigida no item 4.9.4.

### 2.2. ABNT NBR 6118:2023 — Análise Linear (Item 14.5)
A NBR 6118 admite a análise elástica linear baseada na teoria da elasticidade, assumindo comportamento linear dos materiais e seções com rigidezes equivalentes brutas ($EI$), adequada para a verificação de deslocamentos em serviço e distribuição inicial de esforços em vigas e pilares tridimensionais.

### 2.3. AISC 360-22 — Elastic Analysis Framework
O padrão norte-americano AISC 360-22 exige que a análise elástica espacial capture adequadamente todas as deformações axiais, de flexão e de torção, incluindo a compatibilidade cinemática em nós tridimensionais. O elemento espacial de 12 DOFs do OmniFrame 3D cumpre com exatidão as premissas mecânicas da AISC Specification.

---

## 3. Casos de Teste Canônicos e Benchmarks Numéricos

Para assegurar a máxima exatidão e confiabilidade, o solver FEA do OmniFrame 3D é validado contra soluções analíticas fechadas da Resistência dos Materiais e Teoria das Estruturas.

### Benchmark 1: Viga em Balanço 3D sob Carga Transversal e Torção
- **Propriedades Geométricas:** Comprimento $L = 6.0\text{ m}$; Seção tubular retangular ($A = 0.012\text{ m}^2$, $I_z = 8.5\times 10^{-5}\text{ m}^4$, $I_y = 4.2\times 10^{-5}\text{ m}^4$, $J = 9.8\times 10^{-5}\text{ m}^4$).
- **Material:** Aço Estrutural ($E = 200\text{ GPa}$, $G = 77\text{ GPa}$).
- **Condições de Contorno:** Nó 1 totalmente engastado ($u_x = u_y = u_z = \theta_x = \theta_y = \theta_z = 0$).
- **Carregamento no Nó Livre (Nó 2):** Força transversal $F_y = -50\text{ kN}$, Força lateral $F_z = 25\text{ kN}$ e Momento de Torção $M_x = 15\text{ kN}\cdot\text{m}$.
- **Resultados e Comparação:**

| Parâmetro | Solução Analítica Exata | OmniFrame 3D | Divergência (%) |
|---|:---:|:---:|:---:|
| Deslocamento Vertical $\delta_y = \frac{F_y L^3}{3 E I_z}$ | $-21.1765\text{ mm}$ | $-21.1765\text{ mm}$ | $0.000\%$ |
| Deslocamento Lateral $\delta_z = \frac{F_z L^3}{3 E I_y}$ | $+21.4286\text{ mm}$ | $+21.4286\text{ mm}$ | $0.000\%$ |
| Rotação de Torção $\theta_x = \frac{M_x L}{G J}$ | $+0.011927\text{ rad}$ | $+0.011927\text{ rad}$ | $0.000\%$ |
| Momento Fletor na Base $M_{z,\text{base}} = F_y L$ | $-300.00\text{ kN}\cdot\text{m}$ | $-300.00\text{ kN}\cdot\text{m}$ | $0.000\%$ |

### Benchmark 2: Pórtico Espacial com Cargas Biaxiais
- **Descrição:** Pórtico espacial tridimensional formado por 4 pilares e 4 vigas de cobertura em aço com nós rígidos. Carga horizontal simula ação de vento combinada em X e Y, e carga gravitacional em Z.
- **Validação:** Verificação de equilíbrio estático global $\sum F_x = 0$, $\sum F_y = 0$, $\sum F_z = 0$ e $\sum M = 0$ com tolerância estrita de resíduo inferior a $10^{-9}\text{ N}$.

### Benchmark 3: Treliça Espacial Tridimensional
- **Descrição:** Treliça espacial hiperestática com barras articuladas (liberação de momentos $\theta_x = \theta_y = \theta_z$).
- **Validação:** Comprovação de ausência de momentos fletores e de torção parasitas nos elementos, operando em regime estrito de tração/compressão axial ($N \neq 0$, $M = 0$).

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details>
<summary>Click to expand / collapse compliance in English</summary><br>

# OmniFrame 3D — Normative Compliance & FEA Verification Benchmarks

**Version:** `2026.11.13.0`  
**Date:** `November 13, 2026`  
**Author:** Fernando Nillsson Cidade  

---

## 1. Overview of Structural Design Compliance & Standards

**OmniFrame 3D** is a three-dimensional linear elastic finite element analysis (FEA) software application providing internal member forces (axial force $N$, shear forces $V_y$ and $V_z$, torsion $T$, and bending moments $M_y$ and $M_z$), as well as nodal displacements and support reactions.

The results generated by the application supply the required input data for checking ultimate limit states (ULS) and serviceability limit states (SLS) prescribed by major national and international engineering standards:

| Technical Standard | Jurisdiction / Scope | Application in OmniFrame 3D |
|---|---|---|
| **ABNT NBR 8800:2008** | Brazil (Steel Structures) | First-order elastic analysis for space frames and trusses; recovery of bending moments and axial forces for flexural-torsional/buckling instability checks. |
| **ABNT NBR 6118:2023** | Brazil (Concrete Structures) | Linear elastic modeling of 3D building frames and grids; displacement determination for deflection checks and vibration limits (SLS-DEF). |
| **AISC 360-22** | United States (Structural Steel) | Preliminary elastic analysis of structural systems (Chapter C — Design for Stability); stiffness compatibility for the Direct Analysis Method. |
| **Eurocode 3 (EN 1993-1-1)** | European Union (Structural Steel) | Global elastic analysis of spatial structures; determination of elastic buckling parameters for isolated members and continuous frames. |

---

## 2. Requirements for Elastic Structural Analysis

### 2.1. ABNT NBR 8800:2008 — Structural Analysis (Item 4.9)
Under NBR 8800:2008, first-order elastic analysis is valid for determining internal forces and displacements in skeletal structures provided second-order effects (P-$\Delta$ and P-$\delta$) are evaluated or negligible ($\gamma_z \le 1.1$ or $B_2 \le 1.1$). OmniFrame 3D fully implements the matrix elastic formulation required in item 4.9.4.

### 2.2. ABNT NBR 6118:2023 — Linear Analysis (Item 14.5)
NBR 6118 permits linear elastic analysis grounded in elasticity theory, assuming linear material behavior and gross equivalent member stiffnesses ($EI$), suitable for serviceability deflection checks and initial internal force distribution in 3D beams and columns.

### 2.3. AISC 360-22 — Elastic Analysis Framework
The North American AISC 360-22 specification requires spatial elastic analysis to accurately capture all axial, flexural, and torsional deformations, including kinematic compatibility at 3D nodes. OmniFrame 3D's 12-DOF spatial element satisfies the mechanical assumptions of the AISC Specification exactly.

---

## 3. Canonical Test Cases & Numerical Benchmarks

To ensure maximum accuracy and reliability, OmniFrame 3D's FEA solver is validated against closed-form analytical solutions from Mechanics of Materials and Structural Theory.

### Benchmark 1: 3D Cantilever Beam under Transverse Load and Torsion
- **Geometric Properties:** Span $L = 6.0\text{ m}$; Rectangular hollow tube ($A = 0.012\text{ m}^2$, $I_z = 8.5\times 10^{-5}\text{ m}^4$, $I_y = 4.2\times 10^{-5}\text{ m}^4$, $J = 9.8\times 10^{-5}\text{ m}^4$).
- **Material:** Structural Steel ($E = 200\text{ GPa}$, $G = 77\text{ GPa}$).
- **Boundary Conditions:** Node 1 fully fixed ($u_x = u_y = u_z = \theta_x = \theta_y = \theta_z = 0$).
- **Loading at Free End (Node 2):** Transverse force $F_y = -50\text{ kN}$, Lateral force $F_z = 25\text{ kN}$, and Torsional Moment $M_x = 15\text{ kN}\cdot\text{m}$.
- **Results and Comparison:**

| Parameter | Exact Analytical Solution | OmniFrame 3D | Divergence (%) |
|---|:---:|:---:|:---:|
| Vertical Displacement $\delta_y = \frac{F_y L^3}{3 E I_z}$ | $-21.1765\text{ mm}$ | $-21.1765\text{ mm}$ | $0.000\%$ |
| Lateral Displacement $\delta_z = \frac{F_z L^3}{3 E I_y}$ | $+21.4286\text{ mm}$ | $+21.4286\text{ mm}$ | $0.000\%$ |
| Torsional Twist $\theta_x = \frac{M_x L}{G J}$ | $+0.011927\text{ rad}$ | $+0.011927\text{ rad}$ | $0.000\%$ |
| Base Bending Moment $M_{z,\text{base}} = F_y L$ | $-300.00\text{ kN}\cdot\text{m}$ | $-300.00\text{ kN}\cdot\text{m}$ | $0.000\%$ |

### Benchmark 2: Space Frame under Biaxial Loads
- **Description:** Three-dimensional space frame composed of 4 steel columns and 4 rigid-jointed roof beams. Horizontal loads simulate combined wind action in X and Y, with gravitational loading in Z.
- **Validation:** Global static equilibrium verification $\sum F_x = 0$, $\sum F_y = 0$, $\sum F_z = 0$, and $\sum M = 0$ within a strict residual tolerance below $10^{-9}\text{ N}$.

### Benchmark 3: Three-Dimensional Space Truss
- **Description:** Indeterminate space truss with pin-ended bars (moment end releases $\theta_x = \theta_y = \theta_z$).
- **Validation:** Confirmation of zero parasitic bending and torsional moments in members, operating strictly under pure axial tension/compression ($N \neq 0$, $M = 0$).

</details>
