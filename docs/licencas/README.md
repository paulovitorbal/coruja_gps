# Licenças de terceiros

## JetBrains Mono — `OFL.txt`

O firmware embute **mapas de bits rasterizados** da JetBrains Mono SemiBold,
gerados por `scripts/gera_fonte.py` e versionados em
`firmware/src/display/FonteNumero.h` e `FonteTexto.h`.

**Por que a licença está aqui.** A SIL Open Font License 1.1 exige que o aviso
de copyright e o texto da licença acompanhem a fonte **e seus derivados**. Um
mapa de bits rasterizado é derivado da fonte, e este repositório é público,
então a distribuição é real e não hipotética.

O arquivo `.ttf` **não** está no repositório: o gerador o lê de
`~/Library/Fonts`. Quem for regerar precisa instalar a fonte, disponível em
<https://www.jetbrains.com/lp/mono/>.

**O que a OFL permite aqui, sem ambiguidade:** usar, embutir e redistribuir,
inclusive em produto, inclusive rasterizado. O que ela veda é vender a fonte
isolada e usar os Nomes Reservados numa versão modificada — nada disso se
aplica a este uso.

## Twemoji — `CC-BY-4.0-twemoji.txt`

O firmware embute dois ícones de 40×40 em RGB565, gerados por
`scripts/gera_sprites.py` de arte do **Twemoji** e versionados em
`firmware/src/display/Sprites.h`:

| Ícone | Codepoint | Uso |
| :--- | :--- | :--- |
| 🏎 | `U+1F3CE` | radar, fixo e móvel (§4.1) |
| 🚦 | `U+1F6A6` | semáforo, e semáforo com radar |

Os PNG de origem (72×72) estão em `ativos/sprites/`, versionados de
propósito: sem eles a geração não é reproduzível, e uma URL não é garantia
de que o arquivo continue lá.

**Atribuição, que é o que a CC-BY 4.0 exige:** arte do Twemoji,
© Twitter Inc. e colaboradores, licenciada sob CC-BY 4.0. Está no cabeçalho
do `Sprites.h` gerado, e não só aqui — quem ler só o código também vê.
