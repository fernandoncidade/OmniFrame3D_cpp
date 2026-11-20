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

**Versão:** `2026.11.15.0`  
**Data:** `15 de novembro de 2027`  
**Autor:** Fernando Nillsson Cidade  

---

## 1. Visão Geral de Conformidade e Normas de Projeto Estrutural

O **OmniFrame 3D** é um software de análise elástica linear por elementos finitos tridimensionais (FEA), fornecendo os esforços solicitantes internos (esforço normal $N$, esforços cortantes $V_y$ e $V_z$, momento de torção $T$ e momentos fletores $M_y$ e $M_z$), bem como deslocamentos nodais e reações de apoio.

Os resultados gerados pela aplicação fornecem os dados de entrada necessários para a verificação de estados limites últimos (ELU) e estados limites de serviço (ELS) preconizados pelas principais normas técnicas nacionais e internacionais:

| Norma Técnica | Jurisdição / Escopo | Aplicação no OmniFrame 3D |
|---|---|---|
| **ABNT NBR 6118:2023** | Brasil (`LocalPdfVerified` — Concreto) | Modelagem elástica linear de pórticos espaciais e grelhas; auxiliares de flexão ELU, verificação ELS (abertura de fissura / flecha) e combinações ELU/ELS verificados em PDF local. |
| **ABNT NBR 6120:2019** | Brasil (`LocalPdfVerified` — Ações) | Pesos específicos, cargas acidentais de ocupação e parâmetros de ações variáveis verificados em PDF local. |
| **ABNT NBR 6122:2022** | Brasil (`LocalPdfVerified` — Fundações) | Comparação de tensões em sapatas superficiais frente à tensão admissível do solo ($\sigma_{\text{adm}}$) verificada em PDF local. |
| **ABNT NBR 6123:2023** | Brasil (`LocalPdfVerified` — Vento) | Velocidade básica $V_0$, fatores $S_1, S_2, S_3$, velocidade característica $V_k$ e pressão dinâmica $q$ verificados em PDF local. |
| **ABNT NBR 7480:2022** | Brasil (`LocalPdfVerified` — Aço CA-50/CA-60) | Propriedades nominais de escoamento e massa linear de armaduras verificadas em PDF local. |
| **ABNT NBR 8800:2024** | Brasil (`LocalPdfVerified` — Aço e Mistas) | Análise elástica/flambagem, coeficiente $\gamma_z$ / $B_2$, ponderações de resistência e checagens de tração/compressão/flexão/cortante verificadas em PDF local. |
| **ABNT NBR 8681 / 14762 / 15421, AISC 360, EC3** | `OutOfLocalPdfScope` | Marcadas explicitamente como `OutOfLocalPdfScope` no relatório e em `NbrComplianceDialog` por ausência dos PDFs locais correspondentes; motores elásticos/dinâmicos/espectrais mantêm notas transparentes de rastreabilidade sem fabricar cláusulas não verificadas. |

---

## 2. Escopo da Análise Estrutural e Auditoria Normativa

### 2.1. ABNT NBR 8800:2024 e NBR 6118:2023 — Linear, Segunda Ordem e Não Linearidade
- **ABNT NBR 8800:2024:** Suporta análise elástica de primeira ordem, segunda ordem P-Delta / corrotacional geométrica, autovalores de flambagem linear ($\lambda_{cr}$) e plasticidade J2, além de verificações automáticas derivadas estritamente do PDF local da NBR 8800:2024.
- **ABNT NBR 6118:2023:** Suporta análise espacial linear, parâmetros de concreto armado, multiplicadores de combinações ELU/ELS e checagens baseadas exclusivamente no PDF local da NBR 6118:2023.

### 2.2. Vento, Cargas e Fundações (`NBR 6123:2023`, `NBR 6120:2019`, `NBR 6122:2022`, `NBR 7480:2022`)
- `NbrCodeChecks` avalia pressão dinâmica de vento $q = 0,613\,V_k^2$ (`NBR 6123:2023`), tensão de contato no solo frente a $\sigma_{\text{adm}}$ (`NBR 6122:2022`), parâmetros de carga (`NBR 6120:2019`) e propriedades do aço CA-50/CA-60 (`NBR 7480:2022`), sinalizando como **`OutOfLocalPdfScope`** qualquer norma ausente da biblioteca PDF local (`NBR 8681`, `NBR 14762`, `NBR 15421`, `AISC 360-22`, `Eurocode 3`).
- A mesma auditoria normativa consolidada (`evaluateModelNbrCompliance`) é executável em lote no Windows 10/11 via `omniframe-cli --nbr-audit --json`, emitindo `passedAll`, `maxDcr`, `governingSummary`, `elsDisplacement`, `wind6123` e `coverageMatrix`.

---

## 3. Casos de Teste Canônicos e Benchmarks Numéricos

Para assegurar a máxima exatidão e confiabilidade, o kernel FEA e a suíte de solvers do OmniFrame 3D são validados contra soluções analíticas fechadas em **20 testes automatizados CTest** (`ctest --preset testes-msvc-release --output-on-failure`).

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

### Benchmark 4: Placa Fina de Navier (`Quad4-MITC4`) e Acoplamento Misto 1D + 2D
- **Descrição:** Placa quadrada simplesmente apoiada ($4\times 4\text{ m}$, $t = 0.20\text{ m}$, $q = -10\text{ kPa}$) e laje 2D apoiada sobre 4 pilares 1D engastados no fluxo unificado `FemSolver3D::solve()`.
- **Validação:** Flecha central comparada à série dupla de Navier, tensões von Mises 2D positivas, compartilhamento exato de DOF em nós coincidentes e equilíbrio global das reações na base dos pilares ($\sum R_z = 192.0\text{ kN}$).

### Benchmark 5: Sólidos 3D (`Hex8-Bbar`, `Hex20` Lamé e Fluxo E2E `mask3`)
- **Descrição:** Cilindro espesso de Lamé em `Hex20`, flexão quase incompressível em `Hex8-Bbar` ($\nu = 0.499$) e barra prismática `Hex8` tracionada ($P = 100\text{ kN}$) via `StructuralModel::surfaceMesh()`.
- **Validação:** Admissão automática de `mask3` (3 DOFs translacionais por nó sólido puro), alongamento axial $u_x(L) = PL/(EA)$ e tensão uniforme $\sigma_{xx} = \sigma_{vM} = P/A = 400\text{ kPa}$ com equilíbrio estático exato.

*(Matriz completa de V&V, ADRs, auditoria técnica e limitações conhecidas documentadas em [`docs/technical-audit-and-roadmap.md`](technical-audit-and-roadmap.md).)*

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details>
<summary>Click to expand / collapse compliance in English</summary><br>

# OmniFrame 3D — Normative Compliance & FEA Verification Benchmarks

**Version:** `2026.11.15.0`  
**Date:** `November 15, 2027`  
**Author:** Fernando Nillsson Cidade  

---

## 1. Overview of Structural Design Compliance & Standards

**OmniFrame 3D** is a three-dimensional linear elastic finite element analysis (FEA) software application providing internal member forces (axial force $N$, shear forces $V_y$ and $V_z$, torsion $T$, and bending moments $M_y$ and $M_z$), as well as nodal displacements and support reactions.

The results generated by the application supply the required input data for checking ultimate limit states (ULS) and serviceability limit states (SLS) prescribed by major national and international engineering standards:

| Technical Standard | Jurisdiction / Scope | Application in OmniFrame 3D |
|---|---|---|
| **ABNT NBR 6118:2023** | `LocalPdfVerified` (Concrete Structures) | Linear elastic frame/grid modeling, flexural ULS equilibrium/design helpers, serviceability crack/deflection checks, and SLS/ULS combination handling verified against local PDF. |
| **ABNT NBR 6120:2019** | `LocalPdfVerified` (Design Loads) | Self-weight unit weights, occupancy live load tables, and variable action parameters verified against local PDF. |
| **ABNT NBR 6122:2022** | `LocalPdfVerified` (Foundations) | Shallow footing bearing pressure comparison against allowable soil stress ($\sigma_{\text{adm}}$) under SLS/characteristic reactions verified against local PDF. |
| **ABNT NBR 6123:2023** | `LocalPdfVerified` (Wind Forces) | Basic wind speed $V_0$, topographic factor $S_1$, roughness/height factor $S_2$, statistical factor $S_3$, characteristic wind velocity $V_k$, and stagnation pressure $q$ verified against local PDF. |
| **ABNT NBR 7480:2022** | `LocalPdfVerified` (Reinforcing Steel) | Rebar CA-50/CA-60 yield/mass specifications verified against local PDF. |
| **ABNT NBR 8800:2024** | `LocalPdfVerified` (Steel & Composite) | Elastic/buckling structural analysis framework, stability index $\gamma_z$ / $B_2$, resistance factors, and member tension/compression/bending/shear checks verified against local PDF. |
| **ABNT NBR 8681 / 14762 / 15421, AISC 360, EC3** | `OutOfLocalPdfScope` | Explicitly flagged as `OutOfLocalPdfScope` in normative audit reports and `NbrComplianceDialog` because reference PDFs for these standards are absent from the local library; elastic/dynamic/spectral engines provide transparent traceability notes without fabricating unverified normative clauses. |

---

## 2. Requirements for Structural Analysis & Normative Audit Scope

### 2.1. ABNT NBR 8800:2024 & NBR 6118:2023 — Linear, Second-Order & Nonlinear Modeling
- **ABNT NBR 8800:2024:** Supports first-order elastic analysis, second-order P-Delta / geometric corotational nonlinearity, linearized buckling eigenvalues ($\lambda_{cr}$), and J2 plasticity material modeling alongside automated member checks derived solely from formulas verified in the local NBR 8800:2024 PDF.
- **ABNT NBR 6118:2023:** Supports linear elastic spatial analysis, reinforced concrete property parameters, ULS/SLS combination multipliers, and automated section/crack/deflection checks grounded strictly in the local NBR 6118:2023 PDF.

### 2.2. Wind, Loads & Foundations (`NBR 6123:2023`, `NBR 6120:2019`, `NBR 6122:2022`, `NBR 7480:2022`)
- `NbrCodeChecks` evaluates characteristic wind pressure $q = 0.613\,V_k^2$ (`NBR 6123:2023`), soil footing contact pressure vs. $\sigma_{\text{adm}}$ (`NBR 6122:2022`), action references (`NBR 6120:2019`), and steel bar properties (`NBR 7480:2022`).
- Any rule from standards not present in the local ABNT PDF bundle (`NBR 8681`, `NBR 14762`, `NBR 15421`, `AISC 360-22`, `Eurocode 3`) is transparently marked **`OutOfLocalPdfScope`** both in `NbrComplianceDialog` and exported HTML/Markdown/PDF reports.
- The identical consolidated normative engine (`evaluateModelNbrCompliance`) runs headlessly in batch scripts via `omniframe-cli --nbr-audit --json`, emitting `passedAll`, `maxDcr`, `governingSummary`, `elsDisplacement`, `wind6123`, and `coverageMatrix`.

---

## 3. Canonical Test Cases & Numerical Benchmarks

To ensure maximum accuracy and reliability, OmniFrame 3D's FEA kernel and solver suite are validated against closed-form analytical solutions across **20 automated CTest binaries** (`ctest --preset testes-msvc-release --output-on-failure`).

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

### Benchmark 4: Navier Square Plate (`Quad4-MITC4`) & Coupled 1D + 2D Frame-Slab Flow
- **Description:** Simply supported $4\times 4\text{ m}$ slab ($t = 0.20\text{ m}$, $q = -10\text{ kPa}$) and a 2D slab coupled to 4 fixed 1D columns through `FemSolver3D::solve()`.
- **Validation:** Central deflection checked against Navier's double series, positive 2D von Mises stresses, exact DOF coupling at coincident corner nodes, and global reaction equilibrium at column bases ($\sum R_z = 192.0\text{ kN}$).

### Benchmark 5: 3D Solids (`Hex8-Bbar`, `Hex20` Lamé & Unified E2E `mask3`)
- **Description:** Thick-walled Lamé cylinder in `Hex20`, nearly incompressible bending in `Hex8-Bbar` ($\nu = 0.499$), and uniaxial `Hex8` bar ($P = 100\text{ kN}$) via `StructuralModel::surfaceMesh()`.
- **Validation:** Pure solid nodes allocate `mask3` translational DOFs ($3$ DOFs/node), recovering exact elongation $u_x(L) = PL/(EA)$ and uniform stress $\sigma_{xx} = \sigma_{vM} = 400\text{ kPa}$ under global static equilibrium.

*(Full V&V matrix, ADRs, technical audit, and known limitations are documented in [`docs/technical-audit-and-roadmap.md`](technical-audit-and-roadmap.md).)*

</details>
