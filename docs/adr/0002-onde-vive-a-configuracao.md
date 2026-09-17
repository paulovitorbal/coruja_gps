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

Dividir pela vida útil do parâmetro:

* **`coruja.cfg`, texto simples na raiz do cartão**, lido em tempo de execução:
  Wi-Fi, fuso, brilho inicial. Ausente ou ilegível, o firmware assume padrões e
  registra em `warning` — nunca deixa de funcionar por causa disso.
* **`Calibracao`, struct com nominais embutidos**: as constantes do R-05 têm
  valor padrão no código, e o `ConfigCalibracao.h` gerado apenas os
  **sobrescreve**. O struct carrega um `kMedido` que diz se veio da bancada; o
  firmware registra `warning` no boot quando ainda está nos nominais.

  A primeira versão deste ADR dizia que sem o header o projeto não deveria
  compilar. Estava errado, e a contradição só apareceu ao implementar: isso
  quebraria a suíte num clone limpo, que é justamente o que o ADR 0004 protege.
  Nominal com aviso alto entrega a mesma proteção — ninguém esquece que não
  calibrou — sem tornar o repositório inutilizável para quem só quer rodar os
  testes.

O script é `scripts/gera_config.py`. Ele pede a senha por `getpass`, sem eco, e
grava o `.cfg` com permissão `600`.

## Consequências

* Trocar de rede Wi-Fi é editar um arquivo de texto no cartão, sem recompilar
  nem regravar.
* A senha **não** entra no binário. Mas entra no cartão, e **cartão é removível
  e legível por qualquer um** — isso não é mitigável por software. Portanto:
  **use uma rede de convidados ou de IoT para o OTA, nunca a rede principal.**
  Está registrado no README e o script avisa na hora de gravar.
* `coruja.cfg` e `ConfigCalibracao.h` ficam no `.gitignore`. O script gera
  também um `coruja.cfg.exemplo`, sem segredo, que **é** versionado.
* O preço dos nominais é que um firmware não calibrado **funciona**, e portanto
  pode ser esquecido nesse estado. Daí o `warning` no boot e o `kMedido`
  consultável: quem quiser pode recusar operar sem calibração, mas a decisão
  fica com o consumidor, não com o compilador.
* O firmware ganha um parser de texto, que é código a mais e a testar. Aceito:
  ele é lógica pura, roda no host, e o custo é pequeno perto de regravar o
  firmware a cada troca de rede.
