# Dados de teste

## `fixture_igo8.txt` — 100 pontos sintéticos

Arquivo iGO8 puro, sem comentário — o `converte.py` rejeita comentário de
propósito, porque o formato não tem, e não se dobra um parser de formato para
conveniência de teste. Daí esta explicação viver aqui e não dentro do arquivo.

### Como foi feito

As combinações de `TYPE`, `SPEED`, `DirType` e `Direction` foram **amostradas
de uma base iGO8 real**, com semente fixa, para que os testes exercitem as
combinações que de fato ocorrem em vez de um produto cartesiano inventado.

As **coordenadas foram substituídas** por pontos no Atlântico Sul aberto:

```
latitude  -32,0 a -22,0
longitude -40,0 a -35,0
```

Essa caixa é ao sul da cadeia Vitória-Trindade, a leste da bacia de Campos, e
longe de Abrolhos, Rocas e Fernando de Noronha — não há terra emersa nela. E
fica **dentro** da caixa que o `converte.py` valida (`lat -34 a 6`,
`lon -75 a -33`), o que é obrigatório: a primeira tentativa usou longitude
`-25 a -18` e o conversor rejeitou os 100 pontos, corretamente.

**Nenhum ponto deste arquivo é um local real de fiscalização.** Ele não serve
para navegar, e não revela nada sobre a origem dos dados.

### O que ele exercita de propósito

| Caso | Presente |
| :--- | :--- |
| As 11 combinações `(TYPE, DirType)` que existem na origem | ✅ |
| Os quatro `TYPE` | 43 · 20 · 17 · 20 |
| Os três `DirType` | 19 · 62 · 19 |
| Todos os limites da base | `0 30 40 50 60 70 80 90 100 110 120` |
| Limite acima de 100 km/h — outro ramo do `V_infra` | 7 pontos |
| Limite zero em câmera de semáforo | 14 pontos |
| Câmera de semáforo **com** limite — a inconsistência dos 22 | 3 pontos |
| Rumos cruzando 0/360 — o wraparound do R-04 | `359 1 358 2` |
| Pares de latitude idêntica — desempate da busca binária | 3 pares |
| Par a ~40 m de distância — vizinhança apertada | 1 par |

### Como os testes a usam

O CMake roda o `converte.py` sobre ela em tempo de build e gera o
`fixture.bin`, que os testes carregam. Isso faz a suíte exercitar **a cadeia
inteira** — conversor em Python e carregador em C++ — e é exatamente a classe
de divergência que apareceu na primeira execução, quando o carregador havia
assumido um layout de cabeçalho diferente do que o conversor escreve.

O binário **não** é versionado: é derivado.
