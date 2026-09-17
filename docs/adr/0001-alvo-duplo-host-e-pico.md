# 0001 — Um CMake com alvo duplo: testes no host, firmware no RP2350

*Data: 2026-09-17 · Estado: aceita*

## Contexto

A regra 1 exige que todo código de *user land* seja testado, e a regra 2 exige
que cada componente tenha interface com implementação de mock. Mas o alvo é um
RP2350 sem sistema operacional: rodar GoogleTest na placa exigiria gravar o
firmware a cada iteração, não há como afirmar sobre saída de teste sem um canal
serial, e a suíte competiria com os 520 KB de SRAM que o projeto já usa quase
inteiros.

Ao mesmo tempo, a parte do código que mais precisa de teste **não toca em
hardware**: validação do `radares.bin`, busca geográfica em dois estágios,
limiar de infração, máquina de estados de zona e parsing de NMEA são funções
puras. O que toca hardware é fino por natureza — escrever num registrador de
PWM, ler um pino.

## Decisão

Um único `CMakeLists.txt` com a opção `CORUJA_TESTES`:

* **`-DCORUJA_TESTES=ON`** compila `coruja_nucleo` e `coruja_log` com o
  compilador do host e liga a suíte GoogleTest. Não carrega o Pico SDK.
* **sem a opção**, importa o Pico SDK, compila as mesmas bibliotecas para ARM e
  acrescenta `coruja_portes`, que é onde vivem as implementações de hardware.

A regra de pertencimento é estrita: **se compila no host, vai em
`coruja_nucleo` ou `coruja_log`; se precisa de um cabeçalho do SDK, vai em
`coruja_portes`.** É isso que mantém a lógica testável por construção, e não
por disciplina.

## Consequências

* A lógica de domínio roda em milissegundos no host, com cobertura medida por
  `gcov`. A suíte atual fecha 53 casos em 41 ms.
* `coruja_portes` fica **fora** da medição de cobertura, e isso é deliberado:
  ela não tem lógica a cobrir, só tradução para registrador. O que ela
  implementa é validado pelos testes dos seus consumidores, contra os mocks.
* Uma classe que precise de hardware **e** tenha lógica tem de ser dividida em
  duas. O custo é mais arquivos; o ganho é que a lógica não fica refém da placa.
* O `LoggerConsole` ficou no alvo portável, não em `coruja_portes`, porque
  `stdout` existe nos dois mundos: no Pico é o USB-CDC, no host é o terminal.
