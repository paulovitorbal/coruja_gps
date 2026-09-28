# Coruja GPS — cartão de referência

> Para imprimir e colar na carcaça. Gerado do código, não do requisito — se
> divergir do aparelho, o errado é este papel.

## LED

**A piscada é chave tanto quanto a cor.** Impresso em preto e branco, ou visto
por quem confunde vermelho e verde, o ritmo ainda distingue todos os estados.

| Cor | Piscada | Significa | O que fazer |
| :--- | :--- | :--- | :--- |
| 🔵 **azul** | fixo | vivo, **sem proteção** — sem sinal de GPS, sem base, ou atualizando | nada; não há alerta agora |
| 🟢 **verde** | fixo | via livre: nenhum ponto a menos de 300 m | seguir |
| 🟡 **amarelo** | fixo | ponto à frente, **dentro do limite** | nada; só saber que vem |
| 🩷 **rosa** | **1 ×/s** | acima do limite, **ainda sem multa** | aliviar |
| 🔴 **vermelho** | **4 ×/s** | acima do limiar de infração | **reduzir** |
| 🟡🔴 amarelo↔vermelho | 2 ×/s | semáforo à frente | atenção ao sinal |
| ⚫ apagado | — | **sem energia** | verificar fusível e conexão |

O escuro não é estado de operação: se o aparelho está ligado, alguma cor está
acesa. LED apagado com o carro ligado quer dizer problema elétrico.

## Buzzer

**Só a zona vermelha soa.** Silêncio não quer dizer que está tudo bem — quer
dizer que não há multa em curso. Amarelo e rosa são silenciosos de propósito.

| Padrão | Ritmo | Quando |
| :--- | :--- | :--- |
| bipe lento | 1 ×/s | acabou de passar do limiar |
| bipe rápido | ~3 ×/s | 10% acima do limiar |
| pulso | 10 ×/s | 20% acima do limiar |

## Velocidade em que cada coisa acontece

O aparelho alerta **antes** da multa: o limiar é o limite **+ 6 km/h** (ou
+ 5% acima de 100 km/h), mais conservador que os 7 km/h da lei.

| Limite | 🟡 até | 🩷 rosa | 🔴 bipe lento | 🔴 rápido | 🔴 pulso |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 40 | 40 | 40–46 | 46–50 | 50–55 | > 55 |
| 60 | 60 | 60–66 | 66–72 | 72–79 | > 79 |
| 80 | 80 | 80–86 | 86–94 | 94–103 | > 103 |
| 110 | 110 | 110–115 | 115–127 | 127–138 | > 138 |

## Tela

| O que mostra | Quer dizer |
| :--- | :--- |
| `75` | sua velocidade; **não há ponto perto**, e o limite da via é desconhecido |
| `75/60` | sua velocidade sobre o limite **daquele radar** |
| `- -` | sem sinal de GPS |
| 🏎 | radar de velocidade (fixo ou móvel) |
| 🚦 | semáforo com câmera |
| 🚦🏎 | semáforo que também mede velocidade |
| barra | quanto mais cheia, mais perto; a cor é a do LED |
| faixa de baixo vazia | nada à frente — é o estado normal |

O relógio vem do GPS. Fica em `--/--/--` até o primeiro sinal, o que pode
levar até 26 s depois de ligar.

## Encoder

| Ação | Efeito |
| :--- | :--- |
| girar | brilho da tela, de 5% a 100% |
| clicar **com o carro parado** | atualizar a base por Wi-Fi |
| clicar em movimento | ignorado |

---

*Gerado em 2026-09-28 · projeto de estudo, sem garantia · confira sempre a
sinalização da via.*
