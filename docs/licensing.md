# Licenciamento e Esclarecimento Comercial / Licensing & Commercial Model — OmniFrame 3D

<p align="center">
  <b>Selecione o idioma / Select language:</b><br>
  <a href="#ptbr">🇧🇷 Português (BR)</a> | <a href="#enus">🇺🇸 English (US)</a>
</p>

---

## <a id="ptbr"></a>🇧🇷 Português (BR)

<details>
<summary>Clique para expandir / recolher o documento de licenciamento em português</summary><br>

# OmniFrame 3D — Licenciamento, Termos de Código Aberto e Esclarecimento Comercial

**Versão:** `2026.11.17.0`  
**Data:** `17 de novembro de 2026`  
**Licença do Código-Fonte:** GNU Affero General Public License v3 (AGPLv3)  
**Autor e Detentor dos Direitos Autorais:** Fernando Nillsson Cidade  

---

## 1. Visão Geral do Modelo de Duplo Licenciamento

O **OmniFrame 3D** adota um modelo aberto transparente que concilia a liberdade do ecossistema de software livre copyleft forte com a distribuição corporativa oficial e homologada:

| Componente | Licença | Usos Permitidos |
|---|---|---|
| **Código-Fonte (Repositório Completo)** | [GNU AGPLv3](../LICENSE) | 100% Livre: Uso, estudo, compilação, modificação, distribuição e contribuição sob termos copyleft da GNU AGPLv3. |
| **Documentação em `source/assets/`** | Autoria do Projeto (Uso Livre para Desenvolvedores) | Criada para atender exigências da Microsoft Store; desenvolvedores possuem liberdade total para alterar, excluir, adicionar e adaptar em seus próprios forks e plataformas. |
| **Binários Compilados por Terceiros (via Código)** | GNU AGPLv3 | Totalmente livres para compilar, executar, empacotar e redistribuir sob a responsabilidade do desenvolvedor, cumprindo integralmente os termos da AGPLv3. |
| **Binário Oficial Compilado na Microsoft Store** | EULA Oficial do Autor | Distribuição gratuita oficial pelo autor. Proibida a revenda comercial paga por terceiros, apropriação indevida da marca ou engenharia reversa do pacote oficial. |
| **Bibliotecas de Terceiros (Qt, Eigen, OpenGL)** | LGPL v3 / MPL 2.0 / Khronos | Cumprem integralmente os requisitos de atribuição de código aberto upstream. |

---

## 2. Direitos de Código Aberto (GNU AGPLv3)

Todo o código-fonte do OmniFrame 3D — abrangendo o modelo estrutural C++23 (`StructuralModel`), o solver esparso (`FemSolver3D`), os shaders OpenGL 3.3 Core, a interface gráfica Qt 6.11.1, as suítes de teste automatizadas e os presets do CMake — está licenciado sob a **GNU Affero General Public License v3 (AGPLv3)**:

```text
OmniFrame 3D — 3D Structural Frame & Truss FEA Suite
Copyright (c) 2025-2026 Fernando Nillsson Cidade

Este programa é um software livre: você pode redistribuí-lo e/ou modificá-lo sob os termos da GNU Affero General Public License conforme publicada pela Free Software Foundation, seja a versão 3 da Licença, ou (a seu critério) qualquer versão posterior.

Este programa é distribuído na esperança de que seja útil, mas SEM QUALQUER GARANTIA; sem mesmo a garantia implícita de COMERCIALIZAÇÃO ou ADEQUAÇÃO A UM DETERMINADO FIM. Veja a GNU Affero General Public License para mais detalhes.

Você deve ter recebido uma cópia da GNU Affero General Public License junto com este programa. Se não, veja em: <https://www.gnu.org/licenses/>.
```

---

## 3. Documentação em `source/assets/` e Liberdade Total para Desenvolvedores

A documentação institucional e regulatória mantida no diretório `source/assets/` (abrangendo os módulos `ABOUT`, `CLC`, `COPYRIGHT`, `EULA`, `LICENSES`, `NOTICES`, `PRIVACY_POLICY` e `RELEASE`) possui status e finalidade específicos:

1. **Finalidade Original:** Estes documentos foram elaborados por Fernando Nillsson Cidade para viabilizar a publicação, conformidade regulatória e disponibilização gratuita da aplicação oficial na **Microsoft Store** (cumprindo exigências de EULA, privacidade, suporte e conformidade LGPD/GDPR para aplicativos desktop Windows).
2. **Autorização Expressa para Desenvolvedores:** O autor **autoriza expressamente e concede total liberdade** para que desenvolvedores, contribuidores e forks realizem quaisquer alterações, exclusões, adições ou modificações gerais nessa documentação e no código associado.
3. **Condição de Responsabilidade:** Os desenvolvedores são livres para adaptar ou remover esses textos em seus próprios forks e distribuições, desde que respeitem as diretrizes, normas e a legislação vigente de seus respectivos países e das plataformas em que decidirem publicar suas próprias compilações.

---

## 4. Diferenciação dos Binários Oficiais Compilados pelo Autor

O duplo licenciamento estabelece uma fronteira legal clara entre duas formas de distribuição:

1. **Compilações Independentes a partir do Código-Fonte:** Qualquer usuário ou empresa tem o direito irrevogável de clonar o repositório, inspecionar cada rotina de cálculo, compilar com MSVC ou MinGW e rodar localmente. Caso redistribua o software modificado ou o forneça como serviço de rede, deve disponibilizar o código-fonte correspondente sob a GNU AGPLv3.
2. **Pacote Binário Oficial da Microsoft Store:** O instalador MSIX empacotado, assinado digitalmente e publicado pelo autor na loja oficial é distribuído gratuitamente sob o [EULA oficial do autor](../source/assets/EULA/EULA_pt_BR.txt) para conveniência de engenheiros e estudantes que buscam instalação com 1 clique, desinstalação limpa (Política 10.2.7) e atualizações gerenciadas pela Microsoft Store. O EULA opera em conformidade com os Termos de Licença Padrão para Aplicativos da Microsoft Store; terceiros não têm autorização para republicar esse pacote binário oficial assinado como produto comercial próprio pago.

---

## 5. Checklist de Conformidade e Submissão Microsoft Store (MSIX / Partner Center)

Este checklist resume o mapeamento documental exigido pelas diretrizes oficiais da Microsoft para aplicativos MSIX (**Create App Submission**, **Support Info**, **Privacy Statement** e **Microsoft Store Policies v7.20**):

| Item do Partner Center | Status Documental / Referência no Repositório | Observação Operacional |
|---|---|---|
| **Categoria (Obrigatório)** | Sugerido: *Utilitários e ferramentas* / *Educação* (Subcategoria Engenharia/Científico) | Configurar na seção *Propriedades*. |
| **URL da Política de Privacidade (Obrigatório para Desktop Bridge / `runFullTrust`)** | [`docs/privacy-policy.md`](privacy-policy.md) e [`source/assets/PRIVACY_POLICY/Privacy_Policy_pt_BR.txt`](../source/assets/PRIVACY_POLICY/Privacy_Policy_pt_BR.txt) | Apps Win32/`runFullTrust` exigem política de privacidade (Política 10.5.1). **A Microsoft não fornece política padrão e não cobre este app sob a Privacy Statement da Microsoft.** |
| **Site (Recomendado)** | `https://github.com/fernandoncidade/OmniFrame_cpp` | Deve apontar para o site próprio do desenvolvedor — nunca para a listagem da Store. |
| **Suporte ao Cliente (Recomendado)** | `linceu_lighthouse@outlook.com` / GitHub Issues | **A Microsoft não presta suporte ao cliente para este aplicativo**; o autor (desenvolvedor individual) oferece suporte voluntário best-effort. |
| **Telefone e Endereço** | Desenvolvedor individual (`Fernando Nillsson Cidade`) | Obrigatório apenas para contas comerciais/corporativas que ofertam produtos na França; opcional para contas individuais. |
| **Capacidades Restritas (`runFullTrust`)** | Declarada em [`repo/AppxManifest.xml`](../repo/AppxManifest.xml) | Declara ainda `AppExecutionAlias` para `omniframe.exe` e `omniframe-cli.exe` (`desktop4:Subsystem="console"`) e associação `.omniframe`, tudo 100% offline. Justificativa pronta abaixo. |
| **Desinstalação Limpa (Política 10.2.7)** | Declarada em `README.md`, `privacy-policy.md` e `EULA` | Remoção limpa via Configurações do Windows / Menu Iniciar sem serviços em segundo plano. |
| **Listagens da Store (`pt-BR` e `en-US`)** | Diretórios [`repo/LISTAGEM/pt_BR/`](../repo/LISTAGEM/pt_BR) e [`repo/LISTAGEM/en_US/`](../repo/LISTAGEM/en_US) | Fornecer descrição precisa (Política 10.1), mínimo 1 captura de tela por idioma (4+ recomendadas) e logotipos MSIX. |
| **Palavras-chave / Termos de Busca** | Máximo **7 termos** exclusivos por idioma | Sem termos de preço e sem nomes de terceiros (Política 10.1.3). |

### Textos Prontos para Preenchimento no Partner Center

- **Justificativa para Capacidade Restrita (`runFullTrust` — Opções de Envio / Notas para Certificação):**
  > *OmniFrame 3D is a native Win32 C++23 / Qt 6.11.1 desktop 3D Finite Element Analysis (FEA) application using hardware OpenGL 3.3 Core rendering and standard Windows file Open/Save dialogs so users can save and open their local `.omniframe` structural models and `.html`/`.md` technical calculation reports. It operates 100% offline with zero background services, zero drivers, and zero telemetry.*

- **Direitos Autorais e Marcas Registradas (`pt-BR`, máx. 200 caracteres):**
  > `© 2025-2026 Fernando Nillsson Cidade. Código-fonte aberto sob GNU AGPLv3; pacote oficial Microsoft Store sob EULA do autor. Qt (LGPLv3) e Eigen (MPL 2.0).` *(151 caracteres)*

- **Sugestão de Palavras-chave (`pt-BR`, até 7 termos — Política 10.1.3):**
  `Analise Estrutural`, `Elementos Finitos`, `FEA 3D`, `Porticos Espaciais`, `Trelicas 3D`, `Engenharia Civil`, `OpenGL`

</details>

---

## <a id="enus"></a>🇺🇸 English (US)

<details>
<summary>Click to expand / collapse licensing in English</summary><br>

# OmniFrame 3D — Licensing, Open Source Terms & Commercial Clarification

**Version:** `2026.11.17.0`  
**Date:** `November 17, 2026`  
**Source Code License:** GNU Affero General Public License v3 (AGPLv3)  
**Author and Copyright Holder:** Fernando Nillsson Cidade  

---

## 1. Dual-Licensing Architecture Overview

**OmniFrame 3D** adopts a transparent open model balancing the freedom of a strong-copyleft free software ecosystem with official signed Store distribution:

| Component | License | Permitted Uses |
|---|---|---|
| **Source Code (Full Repository)** | [GNU AGPLv3](../LICENSE) | 100% Free: Use, study, compile, modify, distribute, and contribute under GNU AGPLv3 copyleft terms. |
| **Documentation in `source/assets/`** | Project Authorship (Free for Developers) | Created to satisfy Microsoft Store requirements; developers have complete freedom to modify, remove, add, and adapt them for their own forks and platforms. |
| **Third-Party Builds from Source** | GNU AGPLv3 | Fully free to compile, run, package, and redistribute under developer responsibility in full compliance with AGPLv3 terms. |
| **Author's Official Microsoft Store Binary** | Author's Official EULA | Free official distribution by the author. Paid commercial resale, trademark hijacking, or reverse engineering of the official binary package is prohibited. |
| **Third-Party Libraries (Qt, Eigen, OpenGL)** | LGPL v3 / MPL 2.0 / Khronos | Fully compliant with upstream open-source attribution requirements. |

---

## 2. Open Source Rights (GNU AGPLv3)

All OmniFrame 3D source code — covering the C++23 structural model (`StructuralModel`), sparse solver (`FemSolver3D`), OpenGL 3.3 Core shaders, Qt 6.11.1 GUI, automated test suites, and CMake presets — is licensed under the **GNU Affero General Public License v3 (AGPLv3)**:

```text
OmniFrame 3D — 3D Structural Frame & Truss FEA Suite
Copyright (c) 2025-2026 Fernando Nillsson Cidade

This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
```

---

## 3. Documentation in `source/assets/` and Full Freedom for Developers

Institutional and regulatory documentation maintained in `source/assets/` (`ABOUT`, `CLC`, `COPYRIGHT`, `EULA`, `LICENSES`, `NOTICES`, `PRIVACY_POLICY`, and `RELEASE`) carries specific purpose and permission:

1. **Original Purpose:** Prepared by Fernando Nillsson Cidade to enable official publishing, regulatory compliance, and free availability on the **Microsoft Store** (fulfilling EULA, privacy, support, and LGPD/GDPR expectations for Windows desktop apps).
2. **Explicit Authorization for Developers:** The author **expressly authorizes and grants full freedom** for developers, contributors, and forks to make any changes, deletions, additions, or modifications to these documents and associated code.
3. **Condition of Responsibility:** Developers are free to adapt or remove these texts in their own forks and distributions, provided they comply with guidelines, standards, and applicable laws of their respective countries and target distribution platforms.

---

## 4. Distinct Boundary for Official Store Binaries

Dual licensing draws a clear legal boundary between two distribution paths:

1. **Independent Builds from Source:** Any individual or organization holds the irrevocable right to clone the repository, inspect every calculation routine, compile with MSVC or MinGW, and run locally. Redistributing modified versions or offering the software as a network service requires supplying the corresponding source code under GNU AGPLv3.
2. **Official Microsoft Store Binary Package:** The MSIX package compiled, digitally signed, and published by the author on the official store is distributed free of charge under the author's [Official EULA](../source/assets/EULA/EULA_en_US.txt) for engineers and students seeking 1-click installation, clean uninstallation (Policy 10.2.7), and Microsoft Store-managed updates. The EULA operates in compliance with Microsoft Store Standard Application License Terms; third parties are not authorized to republish that official signed binary as their own paid commercial product.

---

## 5. Microsoft Store MSIX Submission, Privacy & Support Checklist

This checklist summarizes the documentation mapping required by official Microsoft guidelines for MSIX apps (**Create App Submission**, **Support Info**, **Privacy Statement**, and **Microsoft Store Policies v7.20**):

| Partner Center Field | Repository Reference / Status | Operational Notes |
|---|---|---|
| **Category (Required)** | Suggested: *Utilities & tools* / *Education* (Engineering/Scientific) | Configure under *Properties*. |
| **Privacy Policy URL (Required for Desktop Bridge / `runFullTrust`)** | [`docs/privacy-policy.md`](privacy-policy.md#enus) and [`source/assets/PRIVACY_POLICY/Privacy_Policy_en_US.txt`](../source/assets/PRIVACY_POLICY/Privacy_Policy_en_US.txt) | Win32/`runFullTrust` apps require a privacy policy (Policy 10.5.1). **Microsoft does not provide a default policy and does not cover this app under Microsoft's Privacy Statement.** |
| **Website (Recommended)** | `https://github.com/fernandoncidade/OmniFrame_cpp` | Must point to the developer's own website — never to the Store listing URL. |
| **Customer Support (Recommended)** | `linceu_lighthouse@outlook.com` / GitHub Issues | **Microsoft does not provide customer support for this application**; the author (individual developer) offers voluntary best-effort support. |
| **Phone & Street Address** | Individual Developer (`Fernando Nillsson Cidade`) | Required only for business/company accounts offering products in France; optional for individual accounts. |
| **Restricted Capabilities (`runFullTrust`)** | Declared in [`repo/AppxManifest.xml`](../repo/AppxManifest.xml) | Also registers `AppExecutionAlias` entries for `omniframe.exe` and `omniframe-cli.exe` (`desktop4:Subsystem="console"`) plus `.omniframe` file association, operating 100% offline. Copy-paste justification below. |
| **Clean Uninstall (Policy 10.2.7)** | Declared in `README.md`, `privacy-policy.md`, and `EULA` | Clean removal via Windows Settings / Start Menu without residual background services. |
| **Store Listings (`pt-BR` and `en-US`)** | Directories [`repo/LISTAGEM/pt_BR/`](../repo/LISTAGEM/pt_BR) and [`repo/LISTAGEM/en_US/`](../repo/LISTAGEM/en_US) | Provide accurate description (Policy 10.1), minimum 1 screenshot per language (4+ recommended), and MSIX logos. |
| **Keywords / Search Terms** | Maximum **7 unique terms** per language | Zero pricing claims and zero third-party brand names (Policy 10.1.3). |

### Ready-to-Paste Texts for Partner Center

- **Restricted Capability Justification (`runFullTrust` — Submission Options / Notes for Certification):**
  > *OmniFrame 3D is a native Win32 C++23 / Qt 6.11.1 desktop 3D Finite Element Analysis (FEA) application using hardware OpenGL 3.3 Core rendering and standard Windows file Open/Save dialogs so users can save and open their local `.omniframe` structural models and `.html`/`.md` technical calculation reports. It operates 100% offline with zero background services, zero drivers, and zero telemetry.*

- **Copyright & Trademark Info (`en-US`, max. 200 chars):**
  > `© 2025-2026 Fernando Nillsson Cidade. Open source under GNU AGPLv3; official Microsoft Store binary under author EULA. Qt (LGPLv3), Eigen (MPL 2.0).` *(148 chars)*

- **Suggested Keywords (`en-US`, up to 7 terms — Policy 10.1.3):**
  `Structural Analysis`, `Finite Element Analysis`, `3D FEA`, `Space Frame`, `3D Truss`, `Civil Engineering`, `Euler Bernoulli`

</details>
