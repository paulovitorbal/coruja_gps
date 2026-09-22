# ADR 0010 — Sem card detect: cartão ausente e cartão ilegível são o mesmo caso

**Data:** 2026-09-22
**Status:** aceito — **supera** a detecção de presença descrita no RNF03 e no RF07

## Contexto

O projeto reservava o **GPIO 14** para o pino `DET` do leitor microSD. A
justificativa, escrita em 2026-09-17, era permitir ao RF07 distinguir **cartão
ausente** — que o motorista resolve inserindo o cartão — de **cartão
ilegível**, que ele não resolve dirigindo.

Duas coisas derrubaram isso.

**A distinção não é usada.** Por decisão do autor em 2026-09-22: para este
projeto não importa se o cartão está fora ou se não deu para ler. A reação é a
mesma — operar sem base e avisar. Uma distinção que não muda comportamento é
código, fio e GPIO gastos para nada.

**E a chave não era confiável.** Medido na bancada:

| Medição | Valor | Esperado |
| :--- | :--- | :--- |
| Fio GPIO 14 ↔ `DET` do módulo | **0,07 Ω** | ~0 Ω ✅ |
| `DET` ↔ `3V` no módulo, com cartão | **4,82 kΩ** | 4,7 kΩ ✅ |
| Tensão no GPIO 14, **sem** cartão | **0 V** | 0 V ✅ |
| Tensão no GPIO 14, **com** cartão | **1,13 V** | ~3,0 V ❌ |

O fio está perfeito e o pull-up existe. O que não acontece é a chave do
soquete **abrir por completo** quando o cartão entra: ela mantém ~2,5 kΩ para
o GND, e o divisor com os 4,82 kΩ dá

```
3,3 V × 2500 / (2500 + 4820) = 1,127 V
```

contra os 1,13 V medidos. Dois números independentes, duas casas decimais.

E 1,13 V cai **na zona indeterminada** da lógica de 3,3 V — acima do limiar de
nível baixo (~0,99 V) e bem abaixo do de nível alto (~2,31 V). O pino não era
alto nem baixo: a leitura dependia de ruído e de qual pull interno estava
ativo. O mesmo código dava respostas opostas conforme o momento.

## Decisão

**Remover o card detect por completo.** Some o `kSdDet` do mapa de pinos, o
`presente()`, o `nivel_bruto()` e o `diagnostica_det()` do `CartaoSd`, a rede
`SD_DET` do gerador e o fio do `.fzz`. O `use_card_detect` da biblioteca vai a
`false`, e o pino `DET` do módulo fica desconectado.

Os erros `Ausente` e `NaoMontou` viram **um só**: `SemCartaoLegivel`, descrito
como *"cartao ausente ou ilegivel"*. A presença passa a ser **inferida da
montagem**: se monta, existe.

**O GPIO 14 fica livre** — pela segunda vez. Antes ele era o LED de Wi-Fi
(R-25). Há teste que falha se alguém o reocupar sem atualizar a contagem.

## Consequências

* Um GPIO, um fio e um bloco de diagnóstico a menos, e uma mensagem em vez de
  duas.
* O RF07 perde a capacidade de dizer ao motorista *"insira o cartão"* em vez de
  *"cartão ilegível"*. **É perda real, e aceita de propósito** — melhor perdê-la
  explicitamente do que ter um firmware que às vezes afirma "sem cartão" com o
  cartão dentro, que era o comportamento medido.
* O RNF03 e o RF07 precisam ser emendados; este ADR os supera nesse ponto.

## O que esta investigação custou, e o que ela ensina

O `DET` foi criado como **instrumento de diagnóstico** — o único fio do chicote
cuja integridade o firmware conseguia medir sozinho. Ele acabou produzindo
**quatro diagnósticos errados seguidos**, cada um coerente e cada um derrubado
pela medição seguinte: polaridade invertida (R-41, retratado), pino flutuante
(R-42), contato intermitente (R-43), contato de alta resistência.

O padrão é sempre o mesmo: uma leitura, um mecanismo plausível construído em
cima dela, e a conclusão fechada antes de perguntar *o que mais produziria este
número*. Três vezes a resposta veio de medir com um multímetro no ponto certo,
e nenhuma veio de raciocinar melhor sobre o que o firmware conseguia observar.

Vale como regra: **um instrumento que só o software enxerga não é confiável
para diagnosticar o hardware que o alimenta.**
