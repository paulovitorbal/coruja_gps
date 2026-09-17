# 0005 — O encoder precisa de ~1 kHz, e isso não cabe no laço de 4 Hz

*Data: 2026-09-17 · Estado: aceita*

## Contexto

O laço de navegação roda a **4 Hz**, que é a taxa do GNSS definida no RF01.4.
Era tentador amostrar o encoder no mesmo laço — um lugar só, nada de
concorrência.

Não funciona. A decodificação por tabela de transição precisa **ver cada uma
das quatro transições** de um detente. A 4 Hz, com 250 ms entre amostras, um
giro normal do KY-040 atravessa vários estados entre duas leituras. O
resultado não é "perder resolução": a tabela vê uma transição de mudança dupla,
classifica como impossível e contribui zero. **O giro simplesmente desaparece**,
e um giro mais rápido pode até ser lido ao contrário.

O KY-040 tem ~20 detentes por volta. Um giro decidido de meia volta em meio
segundo são ~10 detentes em 500 ms, ou 40 transições — 80 Hz só para não perder
nada, e isso sem margem para ruído.

## Decisão

Amostrar o encoder a cada **1 ms** no laço principal, e manter o processamento
de navegação em 4 Hz dentro desse laço.

O `EncoderKy040` é barato o suficiente para isso: três `gpio_get`, uma consulta
ao relógio e uma indexação de tabela. Não há divisão, não há ponto flutuante.

## Consequências

* O laço principal passa a ter **duas cadências**: 1 ms para interface, 250 ms
  para navegação. Não é um laço de 4 Hz com um extra; é um laço rápido que
  dispara o trabalho lento por contagem de tempo.
* Isso **restringe o que pode bloquear** no laço. Uma leitura de cartão SD que
  segure 50 ms come 50 amostras do encoder. Quando o armazenamento entrar, o
  acesso ao cartão vai ter de ficar fora deste laço — provavelmente no core 0,
  junto do GPS, com o core 1 cuidando de interface e renderização, como o
  RNF06 já prevê.
* A alternativa seria **interrupção de borda** nos pinos `CLK` e `DT`. É a
  solução certa se o polling se mostrar insuficiente, e o desenho já está
  pronto para ela: o `DecodificadorQuadratura` é uma função pura que não sabe
  de onde vem a amostra, então mudar de polling para ISR não toca em lógica
  nenhuma. Não foi feito agora por YAGNI — 1 ms de polling tem folga de mais de
  10× sobre a necessidade medida no papel, e ISR traz reentrância e cuidado com
  `volatile` que ainda não se pagam.
* **Ainda não foi medido em hardware.** Os números acima são de cálculo, não de
  bancada. O modo de teste do `main.cpp` existe justamente para confirmar que
  nenhum detente se perde girando depressa.
