# 0004 — Fixture sintética versionada, em vez de pular testes

*Data: 2026-09-17 · Estado: aceita*

## Contexto

O carregador do `radares.bin` merece ser exercitado contra dados realistas, não
só contra arquivos sintéticos de três registros: é o que pega divergência entre
o conversor e o firmware. E pegou — o primeiro teste contra uma base real
revelou que a implementação havia **assumido** o layout do cabeçalho em vez de
ler a especificação, trocando `versao` u16 por u8. Nenhum teste de campo
corrompido teria encontrado isso, porque todos usavam o mesmo cabeçalho errado.

Mas uma base real não pode ser versionada: é dado de terceiro, e o repositório
público não deve dizer nada sobre a origem dos dados.

A primeira versão deste ADR resolvia isso pulando os testes quando o arquivo
não existia. Funcionava, mas com um defeito sério: num clone limpo — que é o
caso normal — **a cobertura mais valiosa simplesmente não rodava**, e a suíte
ficava verde anunciando sucesso.

## Decisão

Versionar uma **fixture sintética de 100 pontos** em formato iGO8 de texto,
`firmware/test/dados/fixture_igo8.txt`, e convertê-la em tempo de build com o
próprio `converte.py` do projeto.

As combinações de `TYPE`, `SPEED`, `DirType` e `Direction` foram amostradas de
uma base real com semente fixa, então exercitam o que de fato ocorre. As
**coordenadas foram trocadas** por pontos no Atlântico Sul aberto, numa caixa
sem terra emersa e dentro da região que o conversor valida. Nenhum ponto é um
local real de fiscalização.

A base completa continua aceita, agora como **extra opcional** via
`-DCORUJA_BASE_REAL`, para exercitar a carga em escala.

## Consequências

* A suíte roda **completa** num clone limpo, sem dado de terceiro. Um pulado
  agora significa "você não configurou o extra", não "a cobertura principal
  não rodou".
* A conversão em tempo de build faz a suíte exercitar **a cadeia inteira**,
  Python e C++, e não só o lado C++. Divergência de layout entre os dois passa
  a ser erro de build, não surpresa de campo.
* Introduz dependência de `python3` para compilar os testes. Aceito: o
  conversor já é parte do projeto e o CMake falha com mensagem clara.
* A fixture é `igO8` **puro, sem comentário**. O `converte.py` rejeita
  comentário porque o formato não tem, e não se dobra um parser de formato para
  conveniência de teste — a explicação vive em `test/dados/README.md`.
* Os números da fixture estão fixados nos testes (43/20/17/20 por tipo, 14 sem
  limite, 3 câmeras com limite). Regenerar a fixture quebra esses testes de
  propósito: é a forma de notar que os dados de apoio mudaram.
