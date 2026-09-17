# 0003 — Domínio em português, infraestrutura em inglês

*Data: 2026-09-17 · Estado: aceita*

## Contexto

Toda a especificação do projeto está em português, e o contrato compartilhado
com o conversor — `formato_radares.py` — já expõe `TipoPonto`, `Sentido`,
`Ponto`, `limite`, `rumo`, `empacota`, `escreve`, `le`. Ao mesmo tempo, o Pico
SDK, GoogleTest e a convenção usual de C++ são em inglês.

Boa parte do vocabulário do domínio é **regulatório brasileiro** e não tem
tradução estável: *limiar de infração*, *semáforo com radar*, *trecho
controlado*, *rumo*. Traduzir gera termos que não existem em nenhuma norma e
quebra o rastro entre o código e o `requirements.md`.

## Decisão

* **Domínio em português**, acompanhando a especificação sem tradução:
  `TipoPonto`, `Sentido`, `Ponto`, `ZonaAlerta`, `BaseRadares`,
  `velocidade_infracao`, `limite`, `rumo`, `sentido`.
* **Infraestrutura em inglês**, acompanhando a plataforma: `Logger`, `Nivel` —
  este último ficou em português por ser lido junto de `Logger`, ver abaixo —,
  e verbos de ciclo de vida de hardware (`begin`, `read`, `write`).
* Convenções das regras globais de C++ valem em qualquer idioma: `PascalCase`
  para tipos, `snake_case` para funções e métodos, `kPascalCase` para
  constantes, `snake_case_` com sublinhado final para membros.

## Consequências

* `TipoPonto::SemaforoComRadar` mapeia direto para a linha da matriz de IHM e
  para o `TYPE=2` do `formato_dados.md` §0.2. Nenhum glossário no meio.
* A fronteira não é perfeitamente nítida — `Nivel` acabou em português dentro de
  uma classe chamada `Logger`. Preferi a inconsistência pequena a um
  `LogLevel` no meio de código de domínio em português.
* Identificadores levam acento? **Não.** Sem diacrítico em nome de símbolo:
  `velocidade_infracao`, não `velocidade_infração`. Comentários e strings de
  mensagem levam acento normalmente.
* Quem chegar de fora do Brasil vai estranhar. O projeto é de estudo pessoal e
  a especificação é o documento primário; o rastro para ela vale mais.
