# ADR 0009 — 12 V entra no gabinete, e o buzzer é alimentado nele

**Data:** 2026-09-21
**Status:** aceito — **supera** a topologia descrita no `README.md` até 2026-09-20

## Contexto

O **R-32** é a incerteza que sobrou do projeto: com as janelas abertas a 80 km/h, num
Uno Mille 2009 sem ar-condicionado, o ruído ambiente fica na casa dos 87 dB(A). O
projeto já gastou o que tinha em firmware — a faixa 3 deixou de ser bipe contínuo e
virou pulso de 10 Hz, porque contínuo era o padrão *menos* detectável. Não há mais nada
a fazer em software.

O que sobrou é elétrico. O **SFM-20B é especificado para 3–24 V** e estava sendo
operado em ~4,6 V, perto do mínimo da faixa. Para um piezo ativo, a pressão sonora sobe
bastante com a tensão.

A topologia anterior punha o conversor CC **fora** do gabinete, e só 5 V entravam. Ela
existia por espaço, e tinha uma propriedade de segurança valiosa: nada acima de 5 V
chegava perto do `VSYS`.

## Decisão

**O buzzer passa a ser alimentado em 12 V, e o conversor vem para dentro do gabinete.**

Não são duas decisões: alimentar o buzzer em 12 V exige 12 V dentro do aparelho de
qualquer forma — o retorno do buzzer entra pelo coletor do `Q1`, cujo emissor está no
GND do aparelho, então o fio do coletor fica em 12 V sempre que o transistor está
cortado. Com 12 V já dentro, manter o conversor fora só acrescentaria um cabo.

O que entra no gabinete passa a ser **um par de 12 V**, e o cabo externo de 5 V
desaparece.

**O conversor não pode ser isolado.** Um conversor com isolamento galvânico não dá
continuidade entre o negativo de 12 V e o do aparelho, e a corrente do buzzer não
fecha: ele simplesmente não toca. Isso descartou um módulo isolado de 12–72 V que
parecia superior no anúncio.

**A entrada do conversor tem dois limites, e o mínimo importa tanto quanto o máximo:**
≥ 40 V pelo load dump, e **≤ 9 V** porque é onde o trilho cai durante a partida. Um
módulo com mínimo de 12 V reiniciaria o aparelho a cada ignição — e o pós-chave foi
escolhido justamente para que isso não acontecesse.

## Consequências

**O que se ganha.** O buzzer opera no meio da faixa em vez de na borda, que é o único
caminho que restava para o R-32. Some um cabo externo e some o conector de 5 V.

**O que se perde, e é o custo real.** O gabinete passa a ter um nó de 12 V a
centímetros de um trilho cujo máximo absoluto é 5,5 V. Três medidas acompanham:

1. Entrada com **3 vias** e buzzer com **2**. Isso era descrito como "robustez, não
   segurança" enquanto só havia 5 V; voltou a ser **segurança**.
2. **12 V não é vermelho.** A convenção antiga usava vermelho para 5 V e 12 V; agora o
   12 V é magenta no `.fzz` e no `bom_schematic.md`.
3. O **`D2`** mudou de referência: roda-livre referencia a alimentação do buzzer, então
   o catodo saiu do 5 V e foi para o 12 V.

**O conector do buzzer deixou de ser JST**, por padronização visual com os demais
módulos. Ele não é polarizado, ao contrário do JST — o que torna a diferença de
contagem de pinos a única proteção contra troca.

## Alternativas consideradas

| Alternativa | Por que não |
| :--- | :--- |
| **Carregador USB veicular** | Delegaria a conversão a hardware comprovado, mas elimina o 12 V do sistema — e o buzzer em 12 V passaria a exigir um par dedicado, desfazendo a simplificação. Também não permite ajustar a saída. |
| **Conversor isolado 12–72 V** | O isolamento impede o retorno do buzzer. E o mínimo de 12 V reiniciaria o aparelho a cada partida. |
| **Manter o buzzer em 5 V** | Deixa o R-32 sem resposta. Era o estado anterior, e a medição em campo ainda não foi feita — mas entrar nela com o buzzer perto do mínimo da faixa é começar com desvantagem onde a margem já é incerta. |
| **Acionamento pelo lado alto**, referenciado aos 12 V | Permitiria conversor isolado, ao custo de PNP ou P-MOS mais transistor de nível e acoplador óptico. Três peças para um buzzer. |

## O que fica pendente

A medição do R-32 em campo continua pendente — esta decisão melhora as chances, não as
confirma. E o SFM-27, alternativa registrada como trocável a quente pelo conector, tem
faixa de tensão **não registrada** no BOM; antes de considerar a troca segura, é
preciso confirmar que ele também aceita 12 V.
