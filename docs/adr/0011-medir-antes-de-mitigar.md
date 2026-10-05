# ADR 0011 — Montar com o que está em mãos; as mitigações térmicas são contingentes

**Data:** 2026-10-01
**Status:** aceito — governa o **R-65** e a `montagem.md`

## Contexto

A sessão de 2026-10-01 abriu o **R-65**: a superfície do painel ao sol foi
medida em **92 °C** (termômetro infravermelho, São Paulo, 5 h), e o NEO-M8N
tem **85 °C de operação e de armazenamento**. O aparelho desligado e parado
fica 7 °C fora de especificação, e não existe estado em que a condição seja
segura pelo datasheet.

A conversa produziu uma cadeia de mitigações, todas plausíveis:

| Mitigação | Custo |
| :--- | :--- |
| Antena de cabo longo + bulkhead SMA na parede | compra de duas peças |
| Caixa impressa em ASA ou PC, tampa de parede dupla em colmeia | compra de impressora |
| Rasgos de ventilação, pés, manta isolante refletiva | baixo |
| Dissipador interno, se houver ponto quente | baixo |
| Trocar para NEO-M8M (105 °C de armazenamento) | troca de placa |

**Nenhuma delas tem número.** Os 92 °C são superfície de painel medidos por
terceiro, não temperatura dentro deste gabinete nesta posição de montagem. A
estimativa de dissipação própria — ~3,5 °C para 2 W em 0,056 m² — diz que o
ambiente domina, mas não diz quanto o ambiente entrega aqui dentro.

E duas propostas feitas hoje já morreram por suposição minha sobre a peça
física: caixa na coluna A e antena remota, as duas construídas sobre um
"antena ativa externa SMA" da lista de materiais que, medido a paquímetro, é
**patch cerâmico com rabicho de 8 cm e U.FL** — 8 cm não chegam a lugar
nenhum. Mesma classe de erro do R-63 e do card detect do ADR 0010: mecanismo
plausível montado sobre leitura não conferida.

## Decisão

**Montar e testar com o que já existe**, por decisão do autor:

* caixa **Patola PB-111** comprada;
* módulo **GY-GPS6MV2-NEO M8N** com o **patch cerâmico de 8 cm** direto no U.FL.

**As mitigações acima não são backlog.** Elas são **contingentes**, e o
gatilho é **observar problema** — não a passagem do tempo nem a conclusão de
que seriam boa ideia. Nada se compra, se imprime ou se refaz antes de haver
número.

Os instrumentos existem e estão registrados: **M-08** (temperatura pelo sensor
no die do RP2350, canal 4 do ADC, ocioso hoje, gravando pelo `LoggerCartao`) e
**M-06** (C/N0 e contagem de satélites pela mensagem GSV, cujo parser já está
pronto).

## Consequências

* O **R-65** fica **aberto e aceito**, não aberto e pendente. É risco
  conhecido e medido depois, não defeito a corrigir agora.
* **O aparelho opera fora da especificação do módulo** durante o período de
  teste. É o custo aceito desta decisão, e é defensável porque isto é projeto
  de estudo, o módulo é peça barata e trocável, e limite de datasheet não é
  precipício: 92 °C ocasionais aceleram envelhecimento em vez de matar a peça
  na hora.
* **O modo de falha é também o instrumento.** Degradação do módulo aparece
  como C/N0 caindo e TTFF alongando — exatamente o que o M-06 mede. Se o GPS
  piorar, isso *é* o problema sendo percebido, e o gatilho da decisão dispara
  sozinho.
* **A janela de risco está adiantada.** 1º de outubro entra na primavera
  rumo ao verão, então o período de teste cai na pior estação. Ruim para a
  peça e bom para o experimento: o pior caso é exercitado cedo, em vez de
  esperar um ano.
* O que **não** é contingente, porque não custa nada a mais e já está
  decidido, segue valendo: prateleira nascendo do chassi e não da tampa,
  camadas com a prateleira servindo de escudo, Kapton sob o módulo, e o
  **buzzer levado para perto do ouvido** — esse já tem 2 m de cabo próprio na
  BOM, então é ganho de posição sem compra, e ataca o **R-32**.
* Quando o gatilho disparar, a ordem é **antena de cabo longo primeiro**. Ela
  é a única de primeira ordem, porque com 8 cm a exigência de vista de céu
  prega a caixa no ponto mais ensolarado do carro. Geometria de caixa,
  material, colmeia e ventilação rendem graus; a posição rende dezenas.

## O que esta decisão evita

O risco oposto ao de ignorar o R-65, e menos óbvio: **comprar impressora,
filamento, antena e suporte para resolver um problema cuja magnitude ninguém
mediu.** A sessão produziu cinco mitigações em duas horas de conversa, e esse
é exatamente o momento em que um projeto de estudo vira uma lista de compras.

É a mesma regra do ADR 0010, lida pelo outro lado: lá, um mecanismo plausível
custou quatro diagnósticos errados até alguém medir no ponto certo. Aqui a
medição vem **antes** do gasto.
