# Política de Segurança / Security Policy — OmniFrame 3D

<p align="center">
  <b>Selecione o idioma / Select language:</b><br>
  <a href="#ptbr">🇧🇷 Português (BR)</a> | <a href="#enus">🇺🇸 English (US)</a>
</p>

---

## <a id="ptbr"></a>🇧🇷 Português (BR)

<details>
<summary>Clique para expandir / recolher a política em português</summary><br>

# Política de Segurança

## 1. Versões com Suporte Oficial

Atualizações e correções de segurança são fornecidas para os releases mais recentes:

| Versão | Suportada |
|:---:|:---:|
| `v2026.11.x` | :white_check_mark: |
| `< v2026.11` | :x: |

---

## 2. Reportando uma Vulnerabilidade

Levamos a integridade, a estabilidade computacional e a segurança do **OmniFrame 3D** com o mais alto rigor técnico.

Se você identificou uma potencial vulnerabilidade de segurança (como estouro de buffer em parsing de modelos estruturais, instabilidade numérica explorável, falhas em shaders OpenGL ou consumo descontrolado de memória ao desserializar arquivos `.omniframe`):

1. **NÃO divulgue publicamente a vulnerabilidade através de issues ou discussões abertas.**
2. Submeta o reporte de forma privada através do recurso **GitHub Security Advisories** na aba [Security](https://github.com/fernandoncidade/OmniFrame_cpp/security/advisories/new) do repositório ou contate o mantenedor diretamente via e-mail (`linceu_lighthouse@outlook.com`).
3. Por favor, inclua em seu relato:
   - Descrição detalhada da vulnerabilidade encontrada;
   - Passo a passo reprodutível, incluindo arquivo de projeto `.omniframe` de demonstração;
   - Vetor de ataque e impacto potencial estimado no processo de cálculo;
   - Sugestões técnicas de correção ou mitigação, caso possua.

### Prazos e Processo de Resposta
- O mantenedor envidará os melhores esforços para analisar e confirmar o recebimento de reportes de vulnerabilidade em tempo hábil, considerando a natureza estritamente voluntária e educacional da manutenção do projeto.
- Realizaremos uma avaliação técnica inicial e estimaremos o prazo de correção.
- Após a correção, um lançamento de segurança será publicado e a divulgação pública e os devidos créditos serão coordenados com o pesquisador.

---

## 3. Compromissos Arquiteturais de Segurança

- **100% Autônomo, Local e Offline:** O software opera com soberania absoluta de dados. Nenhum dado geométrico, propriedade de seção/material, carregamento ou resultado de cálculo estrutural é transmitido pela rede.
- **Validação Rigorosa de Entrada:** Todos os arquivos `.omniframe` (JSON) passam por verificação estrita de esquema, limites numéricos, coordenadas finitas e consistência topológica para evitar problemas de segurança de memória.
- **Isolamento de Renderização Gráfica:** A viewport 3D utiliza shaders OpenGL 3.3 Core Profile validados e compilados internamente no processo, sem carregamento dinâmico de código binário arbitrário ou injeção de shaders não confiáveis na GPU.

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details>
<summary>Click to expand / collapse the policy in English</summary><br>

# Security Policy

## 1. Supported Versions

Security updates are provided for the latest releases:

| Version | Supported |
|:---:|:---:|
| `v2026.11.x` | :white_check_mark: |
| `< v2026.11` | :x: |

---

## 2. Reporting a Vulnerability

We take the integrity, computational stability, and security of **OmniFrame 3D** very seriously.

If you believe you have discovered a security vulnerability in OmniFrame 3D (such as a buffer overflow in model file parsing, exploitable numerical instability, OpenGL shader execution defects, or uncontrolled memory allocation during `.omniframe` deserialization):

1. **Do NOT report security vulnerabilities through public GitHub issues or discussions.**
2. Report the vulnerability privately via **GitHub Security Advisory** under the repository's [Security tab](https://github.com/fernandoncidade/OmniFrame_cpp/security/advisories/new) or contact the maintainer directly via email (`linceu_lighthouse@outlook.com`).
3. Please include:
   - A detailed description of the vulnerability.
   - Exact steps to reproduce, including a sample `.omniframe` project file.
   - The potential impact and attack vector on structural FEA workflows.
   - Any suggested mitigations or patches.

### Response Time & Process
- The maintainer will make best efforts to review and acknowledge receipt of vulnerability reports in a timely manner, given the strictly voluntary and educational nature of project maintenance.
- We will perform an initial technical assessment and estimate a remediation timeline.
- Once fixed, a security release will be published and public disclosure and proper credits will be coordinated with the reporter.

---

## 3. Core Architectural Security Commitments

- **100% Autonomous, Local & Offline:** The application operates with complete data sovereignty. No geometric coordinates, section/material properties, load vectors, or FEA calculation results are transmitted over the network.
- **Strict Input Validation:** All `.omniframe` (JSON) project files undergo rigorous schema validation, numeric boundary checks, coordinate finiteness checks, and topological consistency verifications to prevent memory safety issues.
- **Isolated GPU Rendering:** The 3D viewport utilizes strictly validated OpenGL 3.3 Core Profile shaders compiled in-process, preventing dynamic loading of arbitrary binary code or untrusted GPU shader injection.

</details>
