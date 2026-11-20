# Modelo de Segurança e Resiliência / Security Model — OmniFrame 3D

<p align="center">
  <b>Selecione o idioma / Select language:</b><br>
  <a href="#ptbr">🇧🇷 Português (BR)</a> | <a href="#enus">🇺🇸 English (US)</a>
</p>

---

## <a id="ptbr"></a>🇧🇷 Português (BR)

<details>
<summary>Clique para expandir / recolher o modelo de segurança em português</summary><br>

# OmniFrame 3D — Modelo de Segurança, Integridade de Dados e Resiliência

**Versão:** `2026.11.17.0`  
**Data:** `17 de novembro de 2026`  
**Autor:** Fernando Nillsson Cidade  

---

## 1. Princípios de Segurança e Filosofia de Design

O **OmniFrame 3D** é projetado para atuar em ambientes de engenharia de missão crítica, estações de trabalho industriais e computadores acadêmicos. Sua arquitetura de segurança é fundamentada em quatro garantias estruturais:

1. **Soberania Absoluta e Arquitetura 100% Local-First:** O software opera em regime estrito de isolamento (*air-gapped*). Não há conexões de rede, portas de escuta TCP/UDP, telemetria em segundo plano ou dependência de servidores em nuvem.
2. **Segurança de Memória e RAII em C++23:** Uso sistemático de semântica de movimentação (*move semantics*), ponteiros inteligentes (`std::unique_ptr`, `std::shared_ptr`), coleções com contagem de referências do Qt e ausência de ponteiros proprietários manuais com risco de vazamento (*memory leak*) ou uso pós-liberação (*use-after-free*).
3. **Resiliência Numérica e Tratamento de Exceções:** Proteção matemática contra matrizes de rigidez singulares, comprimentos de barra nulos ($L \le 0$) e propagação de valores não finitos (`NaN` ou `Inf`).
4. **Isolamento de Renderização Gráfica:** Todos os shaders OpenGL 3.3 Core são embutidos e validados internamente no binário, eliminando vetores de injeção de código ou scripts em GPU.

---

## 2. Superfície de Ataque e Modelo de Ameaças

| Vetor Potencial | Nível de Risco | Medida de Mitigação Implementada |
|---|:---:|---|
| **Arquivos de Projeto e Interoperabilidade Maliciosos (`.omniframe`, `.dxf`, `.ifc`)** | Médio | Desserializador JSON com validação estrita de tipos e parsers DXF/IFC defensivos, verificação de índices nodais existentes e checagem de números finitos (`std::isfinite`). |
| **Singularidade Estrutural (Mecanismos)** | Baixo | Solver de Cholesky com pivotamento simétrico que detecta pivôs nulos ou autovalores negativos, emitindo aviso sem travar o processo. |
| **Vazamento de Dados / Espionagem Industrial** | Inexistente | Nenhuma chamada de rede existe no executável; nenhuma coordenada ou projeto é enviado para a internet. |
| **Injeção de Shaders / Falhas de Driver Gráfico** | Baixo | Shaders GLSL 330 com código estático fixo compilado em tempo de execução com verificação de logs de compilação/link. |
| **CLI Windows 10/11 & Caminhos UTF-8 (`HKCU`)** | Baixo | Decodificação UTF-16 `CommandLineToArgvW`, aberturas de arquivo via `pathFromUtf8`, registro opcional restrito a `HKCU` (sem elevação de administrador) e remoção completa via `--uninstall-windows-integration`. |

---

## 3. Validação Estrita de Entrada e Sanitização de Modelos

Ao importar ou abrir qualquer arquivo `.omniframe`, `.dxf` ou `.ifc`:
- **Comprimento de Barras:** É verificado se a distância euclidiana entre os nós inicial e final é superior a $10^{-6}\text{ m}$. Barras com comprimento zero são rejeitadas preventivamente para evitar divisões por zero nas parcelas $EA/L$ e $EI/L^3$.
- **Propriedades Físicas:** Módulo de elasticidade ($E > 0$), inércias ($I_y > 0, I_z > 0$), área ($A > 0$) e constante de torção ($J > 0$) são checados contra limites estritamente positivos.
- **Topologia Consistente:** Referências a nós inexistentes são interceptadas antes de iniciar a alocação da matriz de rigidez global.

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details>
<summary>Click to expand / collapse security model in English</summary><br>

# OmniFrame 3D — Security Model, Data Integrity & Resilience

**Version:** `2026.11.17.0`  
**Date:** `November 17, 2026`  
**Author:** Fernando Nillsson Cidade  

---

## 1. Security Principles & Architectural Guarantees

OmniFrame 3D is designed to operate in mission-critical engineering environments, industrial workstations, and academic computers. Its security architecture rests on four structural guarantees:

1. **Absolute Sovereignty & 100% Local-First Architecture:** Operates under strict isolation (*air-gapped*). There are zero network connections, zero TCP/UDP listening ports, zero background telemetry, and zero cloud server dependencies.
2. **Memory Safety & RAII in C++23:** Systematic use of move semantics, smart pointers (`std::unique_ptr`, `std::shared_ptr`), Qt reference-counted containers, and complete avoidance of manual owning raw pointers that risk memory leaks or use-after-free defects.
3. **Numerical Resilience & Exception Handling:** Mathematical guards against singular stiffness matrices, zero-length members ($L \le 0$), and propagation of non-finite values (`NaN` or `Inf`).
4. **Graphics Rendering Isolation:** All OpenGL 3.3 Core shaders are embedded and validated internally inside the binary, eliminating code or GPU script injection vectors.

---

## 2. Attack Surface & Threat Model

| Threat Vector | Risk Level | Implemented Mitigation |
|---|:---:|---|
| **Malicious Project & Interoperability Files (`.omniframe`, `.dxf`, `.ifc`)** | Medium | JSON deserializer with strict type validation and defensive DXF/IFC parsers, verification of existing node indices, and numerical finiteness checks (`std::isfinite`). |
| **Structural Singularity (Mechanisms)** | Low | Cholesky solver with symmetric pivoting that detects zero pivots or negative eigenvalues, issuing a clear diagnostic without crashing the process. |
| **Data Exfiltration / Industrial Espionage** | None | Zero network calls exist in the executable; no coordinate or structural project ever leaves the local workstation over the internet. |
| **Shader Injection / Graphics Driver Faults** | Low | Static fixed-source GLSL 330 shaders compiled at runtime with strict compile/link log verification. |
| **Windows 10/11 CLI & UTF-8 Paths (`HKCU`)** | Low | UTF-16 `CommandLineToArgvW` decoding, Unicode `pathFromUtf8` stream opening, optional `HKCU`-only user registry integration (zero admin rights), and clean reversal via `--uninstall-windows-integration`. |

---

## 3. Strict Input Validation & Model Sanitization

When importing or opening any `.omniframe`, `.dxf`, or `.ifc` file:
- **Member Length:** Euclidean distance between start and end nodes is verified to exceed $10^{-6}\text{ m}$. Zero-length members are preemptively rejected to avoid division by zero in $EA/L$ and $EI/L^3$ stiffness terms.
- **Physical Properties:** Modulus of elasticity ($E > 0$), moments of inertia ($I_y > 0, I_z > 0$), cross-sectional area ($A > 0$), and torsion constant ($J > 0$) are checked against strictly positive bounds.
- **Consistent Topology:** References to nonexistent nodes are intercepted before global stiffness matrix memory allocation begins.

</details>
