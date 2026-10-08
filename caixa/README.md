# Caixa impressa do coruja_gps

Modelo **paramétrico em OpenSCAD**. Caixa com batente interno e tampa que desce
por cima, presa por atrito — sem parafuso.

> **O que está versionado aqui é a fonte `.scad`.** Os STLs são artefato: cada
> render muda o arquivo inteiro e o git guarda cada versão por completo. São
> ~2 MB por corpo — a mesma razão pela qual o `.uf2` do firmware fica em
> *release* e não no repositório.

---

## Gerar os STLs

```sh
./gera_stl.sh                    # gera neste diretório
./gera_stl.sh ~/Downloads        # ou onde você quiser
```

Se o OpenSCAD não estiver no caminho padrão do macOS:

```sh
OPENSCAD=/usr/bin/openscad ./gera_stl.sh
```

Leva alguns minutos por peça — a geometria usa muita operação booleana.

O script **verifica e recusa** o resultado se houver material dentro de um vão
da traseira. Ver *"A verificação, e por que ela existe"* abaixo.

---

## As três peças

| Arquivo gerado | O que é |
| :--- | :--- |
| `caixa.stl` | corpo, **modelo A** — encoder · display · buzzer |
| `caixa_destro.stl` | corpo, **modelo B** — buzzer · display · encoder |
| `tampa.stl` | tampa, **a mesma para os dois**. Imprima uma só |

Externo 130 × 130 × 67 mm, parede e piso de 3 mm.

### Qual dos dois corpos

A geometria é a mesma; muda de que lado fica o encoder. **Não existe modelo
"certo" em abstrato** — depende de onde a caixa é montada no painel e de qual
mão alcança o botão. Simule o gesto antes de imprimir.

O `coruja_caixa_destro.scad` não duplica nada: ele inclui o principal e
sobrepõe um parâmetro.

### Material é decisão térmica, não estética

O painel do carro mediu **92 °C**. Isso elimina quase tudo — ver `montagem.md`
§2 na raiz do repositório.

---

## O conector de alimentação

Furo de **⌀12,5 mm na traseira, centrado**, para um **GX12-2** ("conector
aviação"). É o único conector externo do aparelho e traz os 12 V do pós-chave;
o conversor fica dentro do gabinete.

**Por que GX12 e não jack P4.** O TVS da entrada é bidirecional: ele clampa
transiente mas **não bloqueia inversão de polaridade**, que chega direta ao
conversor. A propriedade que importa é o **chaveamento mecânico** — só entrar
de um jeito. Barril 5,5 × 2,1 falha nisso, porque centro-positivo é convenção e
não garantia, e ainda não retém contra vibração.

**Centrado** porque existem as versões canhota e destra: no meio, a traseira
fica igual nas duas e a aparência não depende do lado de montagem.

**Acima** do rasgo do cartão e da abertura do USB, e não ao lado: o rasgo
atravessa o centro da parede, e naquela altura não havia como centralizar.

**Montagem:** pinos **machos na caixa**, fêmea no cabo. O cabo do carro fica
energizado com a chave ligada; a caixa desligada está morta, e contato exposto
pertence ao lado morto.

### ⚠️ Duas medidas ainda não conferidas

| | valor | o que é |
| :--- | ---: | :--- |
| `GX12_FURO` | 12,5 mm | ⌀12 nominal + 0,5 de folga de impressão |
| `GX12_VAO_PORCA` | 18,0 mm | diâmetro que permanece com 3 mm de parede, para a porca assentar |

Generosos de propósito enquanto não houver paquímetro sobre o conector
comprado: errar para mais custa folga, errar para menos arruína a peça depois
de impressa.

🔴 **E há uma terceira cota, que não tem valor aqui e pode reprovar o
desenho: o comprimento da rosca.** A espessura máxima de painel é ele menos a
altura da porca. Se sobrar pouco, os 3 mm de parede não fecham — e aí as outras
duas medidas não importam.

As três estão registradas como pendência **M-14** no `montagem.md`.

O reforço em volta do furo é um **anel**, e não um disco, por causa do segundo
valor — engrossar a parede sob a porca consome o comprimento útil de rosca.
Quem limita a espessura aqui não é a impressão, é a peça comprada.

---

## A verificação, e por que ela existe

Em **08/10/2026** o furo do conector foi acrescentado e a peça saiu com o rasgo
do cartão **tapado**: o anel de reforço, somado depois do `difference`, descia
por trás do recorte e o preenchia.

O que falhou não foi o render. Ele deu `Simple: yes`. Uma conferência de malha
procurou o furo e **o encontrou**, no lugar certo, com o diâmetro certo. As
duas verificações respondiam *"o furo existe?"* quando a pergunta era **"alguma
coisa sólida está passando por onde devia haver vão?"**. O defeito apareceu
quando alguém abriu o modelo e olhou.

Daí as duas guardas, que cobrem portas diferentes:

| guarda | onde | pega |
| :--- | :--- | :--- |
| `assert()` | no `.scad` | parâmetro que põe o anel em cima de um recorte, ou contra o batente |
| `verifica_caixa.py` | sobre o STL | material dentro de um vão, qualquer que seja a causa |

```sh
python3 verifica_caixa.py caixa.stl
```

O `gera_stl.sh` roda a segunda automaticamente e falha se ela reprovar.

> ⚠️ O verificador repete, em Python, as coordenadas dos recortes que o `.scad`
> calcula. Se elas mudarem lá e não aqui, ele passa a medir outra coisa — é o
> preço de verificar a **malha** em vez do modelo, e vale porque a malha é o
> que vai para a impressora.
