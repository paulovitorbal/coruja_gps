# 0002 — Credenciais no cartão, calibração compilada

*Data: 2026-09-17 · Estado: aceita*

## Contexto

A regra 8 pede um script que gere os arquivos de configuração perguntando ao
usuário. O firmware precisa de duas famílias de parâmetro, e elas têm ciclos de
vida opostos:

* **Muda sem aviso:** SSID e senha do Wi-Fi da atualização OTA, fuso horário,
  brilho inicial.
* **Muda uma vez, na bancada:** resistores reais dos canais do LED RGB e as
  razões de PWM que produzem o âmbar e o rosa, saídas do R-05.

Compilar tudo num header significa regravar o firmware para trocar de rede
Wi-Fi, e coloca a senha dentro do `.uf2` — de onde ela sai com `picotool save`.
Pôr tudo no cartão cria dependência inversa: sem cartão válido o firmware não
saberia acender o LED na cor certa, e isso colide com o RF07, que exige operar
em modo velocímetro sem base.

## Decisão

**Revisada em 2026-09-20**, depois que o autor definiu o escopo. A primeira
versão punha no cartão também brilho, fuso e credenciais; a segunda mantinha a
calibração num header gerado. Ambas erravam o critério.

**O critério é: configuração é o que varia por instalação. Especificação é o
que define o que o aparelho é.** Se mudar o valor muda o comportamento de
segurança, é especificação — e especificação não fica num arquivo de texto que
qualquer um edita.

### No cartão, em `coruja.cfg`

| Chave | Por que varia por instalação |
| :--- | :--- |
| `wifi_ssid_N`, `wifi_senha_N` | até 5 redes, **em ordem de prioridade** |
| `url_versao` | onde consultar a versão disponível |
| `url_base` | de onde baixar o `radares.bin` |

As **URLs fecham uma lacuna que existia desde que o repositório virou
público**: o RF05 mandava "baixar a versão atualizada" e nunca dizia de onde.
Num repositório público não *poderia* dizer — quem clona aponta para a própria
origem. Ver **R-37**.

As **múltiplas redes** vêm do uso real: ao clicar no encoder o aparelho varre
e conecta na primeira da lista que estiver visível. A prioridade é a **ordem do
arquivo**, não o sinal mais forte — é explícita, previsível, e o log consegue
dizer por que escolheu.

### No código, versionado

Tudo o mais. Brilho inicial, fuso, tolerâncias do `V_infra`, raio de alerta,
faixas do buzzer, e a **calibração do LED** medida em 19/09.

A calibração é o caso que mais ensina. Ela esteve num header **gerado** por
duas versões deste ADR, e nesse tempo **nenhum arquivo do firmware a incluía** —
seis menções, todas em comentário. Um valor gerado que ninguém consome é pior
que um valor errado: não há como perceber. Hoje ela vive em
`led/Calibracao.h`, versionada e com teste que fixa os números medidos.

## Consequências

* Trocar de rede Wi-Fi é editar um arquivo de texto no cartão, sem recompilar
  nem regravar. Levar o aparelho para outro lugar é acrescentar uma linha.
* A senha **não** entra no binário. Mas entra no cartão, e **cartão é removível
  e legível por qualquer um** — isso não é mitigável por software. Portanto:
  **use uma rede de convidados ou de IoT para o OTA, nunca a principal.** O
  script avisa ao gravar, e o `.cfg` sai com permissão `600`.
* O firmware ganhou um parser, que é código a mais — mas é **lógica pura**,
  roda no host e tem 26 testes, incluindo um que garante que **a senha nunca
  aparece no log**.
* **Valor longo demais é rejeitado, nunca truncado.** Truncar uma URL produz um
  valor que parece válido e falha em campo com sintoma obscuro.
* Gerador e leitor estão em linguagens diferentes e evoluem em lados diferentes
  do projeto. Dois testes os amarram: o C++ lê o `coruja.cfg.exemplo`
  versionado e falha se o formato divergir; o Python compara suas constantes de
  limite com as do `Configuracao.h`. Ambos foram verificados quebrando o
  arquivo de propósito.
* Quem quiser mudar brilho ou tolerância agora **precisa recompilar**, e isso é
  intencional: são decisões de projeto, não preferências. Alguém que zerasse o
  desconto do `V_infra` teria um aparelho que avisa tarde demais e não saberia.
