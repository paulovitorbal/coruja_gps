#!/usr/bin/env python3
"""A zona privada: o pedaço do trajeto que o mapa não desenha.

O aparelho grava a viagem inteira, e a viagem inteira começa na garagem de
casa. Quem abre `/viagens` vê o trajeto do primeiro ponto ao último — e o
primeiro ponto **é o endereço residencial**, com a precisão de alguns metros.

Este módulo recorta: dado um centro e um raio, nenhum ponto de dentro chega
ao navegador. O recorte acontece no SERVIDOR, antes de a página ser montada.
Filtrar no JavaScript esconderia do olho e entregaria no código-fonte, que é
pior que não esconder — dá a sensação de proteção sem a proteção.

⚠️ **O que isto NÃO resolve.** Um trajeto cortado por um círculo continua
apontando para o centro dele: as pontas visíveis ficam todas sobre a mesma
circunferência, e três pontas determinam o círculo — portanto o centro, que é
a casa. É a fraqueza conhecida das "zonas de privacidade" dos aplicativos de
corrida. Por isso o raio é **sorteado por viagem** dentro de uma folga (ver
`Privacidade.raio_de`): as pontas passam a cair num anel, e não numa linha.

E continua havendo o que não dá para esconder sem mentir: a listagem em
`/viagens` publica a distância total de cada viagem, e a diferença entre ela e
o que o mapa mostra diz quantos quilômetros ficaram escondidos. Quem tem o
token, a malha viária e paciência estreita o cerco. A proteção aqui é contra o
olhar casual — uma tela mostrada a alguém, uma captura compartilhada —, e não
contra análise dedicada com acesso à página.
"""

from __future__ import annotations

import hashlib
import math
from dataclasses import dataclass

import viagem

#: O raio pedido quando ninguém diz outro, em metros.
RAIO_PADRAO_M = 1000.0

#: Quanto o raio pode crescer, por sorteio, além do pedido.
#:
#: ⚠️ A folga só ESCONDE MAIS: o sorteio vai de `raio_m` a `raio_m + folga_m`,
#: nunca abaixo. Quem configurou 1 km continua com no mínimo 1 km escondido.
#:
#: 400 m porque a borda precisa variar mais que a largura de um quarteirão
#: para que duas viagens não caiam praticamente no mesmo lugar. Zero desliga
#: o sorteio e volta ao círculo fixo, que é o caso fácil de atacar.
FOLGA_PADRAO_M = 400.0

#: Abaixo disto um fragmento não é trajeto: é um pingo solto na borda do
#: recorte, exatamente onde ele não pode estar.
MINIMO_DE_PONTOS = 2

#: As chaves aceitas em `[privacidade]`.
CHAVES = frozenset({"zona", "raio_m", "folga_m"})

#: As que valem uma vez só. `zona` se repete; estas não.
CHAVES_UNICAS = frozenset({"raio_m", "folga_m"})


class ErroDePrivacidade(Exception):
    """A seção `[privacidade]` não pode ser usada como está.

    ⚠️ **A mensagem diz o motivo e NUNCA o valor.** Ela vai para o stderr,
    daí para o `docker logs` e para o Loki — e a linha com defeito costuma
    ser a da casa, escrita quase certa. Quem precisa achar a linha recebe o
    número dela: `chave` e `ocorrencia` dizem qual das linhas daquela chave
    foi, e o `le_aparelhos` traduz isso para `arquivo:linha`.

    Pelo mesmo motivo nenhum `raise` daqui encadeia a exceção original: a do
    `float()` repete o texto que recebeu, e um traceback a imprimiria.
    """

    def __init__(self, motivo: str, chave: str = "", ocorrencia: int = 0):
        super().__init__(motivo)
        self.chave = chave
        self.ocorrencia = ocorrencia


@dataclass(frozen=True)
class ZonaPrivada:
    """Um centro a esconder. O raio é o mesmo para todas, e vem da config."""

    lat: float
    lon: float


@dataclass(frozen=True)
class Privacidade:
    zonas: tuple[ZonaPrivada, ...] = ()
    raio_m: float = RAIO_PADRAO_M
    folga_m: float = FOLGA_PADRAO_M

    @property
    def ativa(self) -> bool:
        return bool(self.zonas)

    def raio_de(self, zona: int, semente: str) -> float:
        """O raio desta zona NESTA viagem, entre `raio_m` e `+ folga_m`.

        **Sorteado, mas não aleatório.** O valor sai de um resumo do nome da
        viagem com o índice da zona, então a mesma viagem recorta sempre no
        mesmo lugar: recarregar a página não pode mover a borda. Duas
        renderizações com bordas diferentes dariam duas amostras do mesmo
        trajeto — exatamente o que o sorteio existe para negar.

        Entre viagens diferentes, muda. É isso que tira as pontas de cima de
        uma circunferência só e as espalha por um anel de `folga_m` de
        largura.
        """
        if self.folga_m <= 0:
            return self.raio_m
        resumo = hashlib.sha256(f"{semente}|{zona}".encode("utf-8")).digest()
        fracao = int.from_bytes(resumo[:8], "big") / float(1 << 64)
        return self.raio_m + self.folga_m * fracao

    def resumo(self) -> str:
        """Uma linha para o log do servidor. **Sem coordenada nenhuma.**

        ⚠️ O log não é segredo: ele vai para o terminal, para o `docker logs`
        e para o Loki. Pôr o centro da zona aqui publicaria justamente o que o
        módulo inteiro existe para esconder.
        """
        if not self.ativa:
            return "nenhuma zona privada"
        sorteio = (f" mais ate {self.folga_m:.0f} m sorteados por viagem"
                   if self.folga_m > 0 else " (raio fixo)")
        return (f"{len(self.zonas)} zona(s) privada(s), "
                f"raio {self.raio_m:.0f} m{sorteio}")


def monta(bruto: dict[str, list[str]]) -> Privacidade:
    """Valida a seção `[privacidade]` do `aparelhos.cfg`.

    ⚠️ **Toda chave desconhecida levanta**, e o motivo é o mesmo das seções:
    `raio=1000` em vez de `raio_m=1000` cairia num balde ignorado e o mapa
    sairia com o raio padrão sem avisar. Pior ainda, `zonas=` em vez de
    `zona=` desligaria a proteção inteira — e o sintoma seria um mapa que
    parece perfeitamente normal, com a casa no meio.
    """
    for chave in bruto:
        if chave not in CHAVES:
            raise ErroDePrivacidade(
                f"em [privacidade], chave '{chave}' desconhecida "
                f"(conhecidas: {', '.join(sorted(CHAVES))})", chave)
    for chave in CHAVES_UNICAS:
        if len(bruto.get(chave, [])) > 1:
            raise ErroDePrivacidade(
                f"em [privacidade], '{chave}' aparece mais de uma vez; "
                "qual delas valeria?", chave, 1)

    raio_m = _numero(bruto, "raio_m", RAIO_PADRAO_M)
    folga_m = _numero(bruto, "folga_m", FOLGA_PADRAO_M)
    if raio_m <= 0:
        raise ErroDePrivacidade(
            "em [privacidade], 'raio_m' tem de ser maior que zero: raio zero "
            "nao esconde nada, e quem escreveu a secao queria esconder",
            "raio_m")
    if folga_m < 0:
        raise ErroDePrivacidade(
            "em [privacidade], 'folga_m' nao pode ser negativa: ela so pode "
            "AUMENTAR o raio pedido", "folga_m")

    zonas = tuple(_zona(texto, i)
                  for i, texto in enumerate(bruto.get("zona", [])))
    if not zonas and bruto:
        # Ajustar o raio e esquecer a zona deixa a proteção desligada com
        # cara de ligada. É o pior estado possível, e o único que dá para
        # detectar aqui.
        raise ErroDePrivacidade(
            "em [privacidade], nenhuma 'zona=lat,lon': sem centro nao ha o "
            "que esconder, e o raio sozinho nao protege nada")
    return Privacidade(zonas=zonas, raio_m=raio_m, folga_m=folga_m)


def _numero(bruto: dict[str, list[str]], chave: str, padrao: float) -> float:
    """O valor de `chave`, que tem de ser número FINITO.

    Finito porque `nan` passa pelo `<= 0` e o `metros_entre(...) <= nan` é
    falso para todo ponto: a zona ficava ligada no papel e desligada no
    mapa, com a casa no meio e aviso nenhum.
    """
    valores = bruto.get(chave, [])
    if not valores:
        return padrao
    try:
        valor = float(valores[-1])
    except ValueError:
        valor = math.nan
    if not math.isfinite(valor):
        raise ErroDePrivacidade(
            f"em [privacidade], '{chave}' tem de ser um numero finito, em "
            "metros", chave, len(valores) - 1) from None
    return valor


def _zona(texto: str, ocorrencia: int = 0) -> ZonaPrivada:
    """`lat,lon` em graus decimais.

    Graus decimais, e não um endereço escrito: traduzir endereço em
    coordenada exigiria mandar o endereço residencial para um serviço de
    geocodificação de terceiro — publicando, para resolver, justamente o dado
    que se quer esconder. O número sai do próprio mapa, com um clique.

    As mensagens não repetem o texto — ver `ErroDePrivacidade`.
    """
    def erro(motivo: str) -> ErroDePrivacidade:
        return ErroDePrivacidade(f"em [privacidade], 'zona': {motivo}",
                                 "zona", ocorrencia)

    partes = [p.strip() for p in texto.split(",")]
    if len(partes) != 2:
        raise erro("esperava 'lat,lon' em graus decimais, dois numeros "
                   "separados por virgula")
    try:
        lat, lon = float(partes[0]), float(partes[1])
    except ValueError:
        raise erro("lat e lon tem de ser numeros") from None
    if not -90.0 <= lat <= 90.0:
        raise erro("latitude fora de [-90, 90]")
    if not -180.0 <= lon <= 180.0:
        raise erro("longitude fora de [-180, 180]")
    return ZonaPrivada(lat=lat, lon=lon)


def esconde_jornada(j: viagem.Jornada | viagem.Viagem,
                    conf: Privacidade) -> viagem.Jornada:
    """A jornada sem os pontos de dentro das zonas. A mesma, se não houver.

    A semente do sorteio é o NOME da jornada — o arquivo do primeiro trecho,
    que carrega data e hora e nunca se repete. Assim cada viagem recorta no
    seu próprio raio, e sempre no mesmo.

    Aceita um trecho solto como o `desenha` aceita: a página do mapa tem um
    caminho de recuo que lê o arquivo direto quando ele não casa com nenhuma
    jornada, e um `AttributeError` ali trocaria o mapa por um 500.
    """
    if isinstance(j, viagem.Viagem):
        j = viagem.Jornada([j])
    if not conf.ativa:
        return j
    fragmentos: list[viagem.Viagem] = []
    cortou = False
    for n, t in enumerate(j.trechos):
        # O indice do trecho entra na semente: duas travessias da mesma zona
        # em trechos diferentes da MESMA jornada nao podem dividir o raio
        # sorteado -- ver `_recorte`.
        pedacos, perdeu = esconde(t, conf, f"{j.nome}|{n}")
        fragmentos.extend(pedacos)
        cortou = cortou or perdeu
    if not cortou:
        # Nada a esconder nesta viagem: devolver a original evita carregar
        # uma cópia e deixa `cortada` falso, que é o que o painel consulta
        # para não avisar sobre um corte que não houve.
        return j
    return viagem.Jornada(trechos=fragmentos, gravados=list(j.trechos))


def esconde(v: viagem.Viagem, conf: Privacidade,
            semente: str) -> tuple[list[viagem.Viagem], bool]:
    """Os fragmentos exibíveis de um trecho, e se algo foi removido.

    ⚠️ **Fragmentos, e não uma lista filtrada.** Tirar os pontos de dentro e
    manter uma linha só faria a trilha atravessar a zona em reta — ligando os
    dois lados por cima do que se quer esconder, e apontando para o meio dele.
    É o mesmo motivo pelo qual `trilha_da_jornada` desenha um `Feature` por
    trecho em vez de emendar as pausas.
    """
    ocultos, cortes = _recorte(v.pontos, conf, semente)
    if not any(ocultos) and not any(cortes):
        return [v], False

    fragmentos: list[viagem.Viagem] = []
    total = len(v.pontos)
    comeco = None
    for i in range(total):
        if ocultos[i]:
            comeco = _fecha(fragmentos, v, comeco, i, total)
            continue
        if comeco is None:
            comeco = i
        # Lacuna cortada: o ponto seguinte continua visível, mas a reta
        # entre os dois passa por dentro da zona e não pode ser desenhada.
        if i + 1 < total and cortes[i]:
            comeco = _fecha(fragmentos, v, comeco, i + 1, total)
    _fecha(fragmentos, v, comeco, total, total)
    return fragmentos, True


def _fecha(fragmentos: list[viagem.Viagem], v: viagem.Viagem,
           comeco: int | None, fim: int, total: int) -> None:
    """Fecha o fragmento `[comeco, fim)`, se ele tiver trajeto. Devolve `None`.

    Um fragmento de um ponto só é descartado: ele não desenha trajeto, e
    viraria um pingo na borda do recorte — exatamente onde não pode estar.
    """
    if comeco is not None and fim - comeco >= MINIMO_DE_PONTOS:
        fragmentos.append(viagem.Viagem(
            nome=v.nome,
            pontos=v.pontos[comeco:fim],
            inicio_oculto=comeco > 0 or v.inicio_oculto,
            fim_oculto=fim < total or v.fim_oculto,
        ))
    return None


def _recorte(pontos: list[viagem.PontoViagem], conf: Privacidade,
             semente: str) -> tuple[list[bool], list[bool]]:
    """Que pontos somem, e entre que pares a linha não pode ser ligada.

    ⚠️ **Uma aproximação, um raio sorteado.** Antes o raio era um por
    viagem, e a ida e a volta de uma mesma jornada caíam na MESMA
    circunferência: quatro pontas, dois raios e um centro — duas jornadas
    davam equações suficientes para resolver o centro, que é a casa. Com um
    raio por aproximação, cada corte traz uma incógnita nova e a conta deixa
    de fechar.

    A detecção usa o raio MÁXIMO (`raio_m + folga_m`) e o recorte usa o
    sorteado. É isso que mantém a numeração das aproximações estável: ela não
    pode depender do sorteio, senão recarregar a página embaralharia os
    raios e daria duas amostras do mesmo trajeto.
    """
    n = len(pontos)
    ocultos = [False] * n
    cortes = [False] * max(n - 1, 0)
    for j, z in enumerate(conf.zonas):
        # `<=` e não `<`: o pedido é "nenhum ponto dentro de 1 km", e na
        # dúvida esconde. Custa um ponto de trajeto.
        ate_o_ponto = [viagem.metros_entre(p.lat, p.lon, z.lat, z.lon)
                       for p in pontos]
        ate_a_reta = [
            viagem.metros_ate_segmento(z.lat, z.lon,
                                       pontos[i].lat, pontos[i].lon,
                                       pontos[i + 1].lat, pontos[i + 1].lon)
            for i in range(n - 1)]
        maximo = conf.raio_m + max(conf.folga_m, 0.0)
        for k, (a, b) in enumerate(_aproximacoes(ate_o_ponto, ate_a_reta,
                                                 maximo)):
            raio = conf.raio_de(j, f"{semente}|{k}")
            for m in range(a, b + 1):
                if ate_o_ponto[m] <= raio:
                    ocultos[m] = True
            # As lacunas que TOCAM a aproximação. Fora dela a reta já está a
            # mais que o raio máximo, e nenhum raio sorteado a alcança.
            for g in range(max(a - 1, 0), min(b + 1, n - 1)):
                if ate_a_reta[g] <= raio:
                    cortes[g] = True
    return ocultos, cortes


def _aproximacoes(ate_o_ponto: list[float], ate_a_reta: list[float],
                  raio: float) -> list[tuple[int, int]]:
    """Cada chegada perto da zona, como o intervalo de pontos `[a, b]`.

    Uma travessia sem amostra nenhuma dentro — duas amostras de um arquivo
    v1 têm 2 km entre si — vira um intervalo VAZIO (`a > b`) na lacuna em
    que ela acontece. É o caso que o filtro ponto a ponto não via.
    """
    achadas: list[tuple[int, int]] = []
    n = len(ate_o_ponto)
    i = 0
    while i < n:
        if ate_o_ponto[i] <= raio:
            a = i
            while i < n and ate_o_ponto[i] <= raio:
                i += 1
            achadas.append((a, i - 1))
        else:
            i += 1
    for g, distancia in enumerate(ate_a_reta):
        if (distancia <= raio and ate_o_ponto[g] > raio
                and ate_o_ponto[g + 1] > raio):
            achadas.append((g + 1, g))     # vazia: só a reta passa perto
    # Em ordem de acontecimento, que é o que numera as aproximações.
    achadas.sort()
    return achadas
