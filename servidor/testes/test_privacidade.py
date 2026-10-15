"""A zona privada: o que o mapa NÃO pode mostrar.

    python3 -m unittest discover -s servidor/testes

`unittest` e não pytest, como no resto do projeto.

⚠️ **Toda coordenada daqui é sintética**, à volta de (-10, -40), que é o mesmo
par que o `formato_logs` usa nos exemplos. Um teste de privacidade com o
endereço de verdade escrito dentro seria exatamente o vazamento que ele existe
para impedir — e este arquivo é versionado.
"""

import datetime
import math
import re
import sys
import unittest
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ))

import privacidade as p  # noqa: E402
import viagem  # noqa: E402

#: O centro da zona nos testes. Sintético.
CASA = p.ZonaPrivada(lat=-10.0, lon=-40.0)

#: O mesmo número que o `viagem._M_POR_GRAU`, repetido de propósito: se
#: alguém trocar a constante lá, estes testes têm de reprovar em vez de
#: acompanhar a mudança em silêncio.
M_POR_GRAU = 111_320.0

INICIO = datetime.datetime(2026, 10, 15, 8, 0, tzinfo=datetime.timezone.utc)


def ao_norte(metros: float) -> float:
    """A latitude a `metros` ao norte da casa. Longitude não muda."""
    return CASA.lat + metros / M_POR_GRAU


def ponto(metros_ao_norte: float, segundos: int,
          dist_km: float = 0.0, v: float = 50.0) -> viagem.PontoViagem:
    return viagem.PontoViagem(
        quando=INICIO + datetime.timedelta(seconds=segundos),
        lat=ao_norte(metros_ao_norte), lon=CASA.lon,
        v_media_kmh=v, dist_km=dist_km)


def trecho(distancias_m: list[float], nome: str = "20261015_080000.log",
           t0: int = 0) -> viagem.Viagem:
    """Um trecho reto ao norte, um ponto a cada 6 s."""
    return viagem.Viagem(
        nome=nome,
        pontos=[ponto(d, t0 + i * 6, dist_km=i * 0.1)
                for i, d in enumerate(distancias_m)])


def so_casa(raio_m: float = 1000.0, folga_m: float = 0.0) -> p.Privacidade:
    return p.Privacidade(zonas=(CASA,), raio_m=raio_m, folga_m=folga_m)


class Recorte(unittest.TestCase):
    """O que sai e o que fica."""

    def test_sem_zona_nenhuma_a_viagem_passa_intacta(self):
        # Quem não configurou privacidade não pode ter o mapa alterado.
        j = viagem.Jornada([trecho([0, 500, 1500, 3000])])
        saida = p.esconde_jornada(j, p.Privacidade(zonas=()))
        self.assertIs(saida, j)

    def test_ponto_dentro_do_raio_some_e_o_de_fora_fica(self):
        j = viagem.Jornada([trecho([200, 900, 1200, 4000])])
        saida = p.esconde_jornada(j, so_casa())
        restantes = [round(_metros_da_casa(q)) for q in saida.pontos]
        self.assertEqual(restantes, [1200, 4000])

    def test_exatamente_no_raio_e_considerado_dentro(self):
        # O pedido é "nenhum ponto dentro de 1 km". A borda fica de fora: na
        # dúvida, esconde — é a opção conservadora, e custa um ponto.
        #
        # O raio sai da MESMA medida do ponto, e não do número 1000: assim a
        # igualdade é exata e o teste não depende de arredondamento.
        borda = ponto(1000.0, 0)
        exato = viagem.metros_entre(CASA.lat, CASA.lon, borda.lat, borda.lon)
        j = viagem.Jornada([trecho([1000.0, 2000, 3000])])
        saida = p.esconde_jornada(j, so_casa(raio_m=exato))
        self.assertEqual([round(_metros_da_casa(q)) for q in saida.pontos],
                         [2000, 3000])

    def test_saida_de_casa_perde_o_comeco_e_marca_o_corte(self):
        j = viagem.Jornada([trecho([0, 300, 800, 1400, 2500])])
        saida = p.esconde_jornada(j, so_casa())
        self.assertEqual(len(saida.trechos), 1)
        self.assertTrue(saida.trechos[0].inicio_oculto)
        self.assertFalse(saida.trechos[0].fim_oculto)

    def test_chegada_em_casa_perde_o_fim(self):
        j = viagem.Jornada([trecho([2500, 1400, 800, 300, 0])])
        saida = p.esconde_jornada(j, so_casa())
        self.assertEqual(len(saida.trechos), 1)
        self.assertFalse(saida.trechos[0].inicio_oculto)
        self.assertTrue(saida.trechos[0].fim_oculto)

    def test_passar_por_casa_no_meio_parte_a_viagem_em_dois(self):
        # ⚠️ É o caso que o simples "filtrar pontos" erraria: a trilha
        # viraria uma reta por cima da zona, ligando os dois lados e
        # apontando para o centro dela.
        j = viagem.Jornada([trecho([3000, 1500, 400, 0, 500, 1600, 3200])])
        saida = p.esconde_jornada(j, so_casa())
        self.assertEqual(len(saida.trechos), 2)
        self.assertEqual([round(_metros_da_casa(q)) for q in saida.pontos],
                         [3000, 1500, 1600, 3200])
        self.assertTrue(saida.trechos[0].fim_oculto)
        self.assertTrue(saida.trechos[1].inicio_oculto)

    def test_viagem_inteira_dentro_da_zona_nao_sobra_nada(self):
        # Uma volta no quarteirão. Não há mapa a desenhar, e dizer isso é
        # melhor que abrir uma página em branco.
        j = viagem.Jornada([trecho([0, 100, 300, 200, 0])])
        saida = p.esconde_jornada(j, so_casa())
        self.assertEqual(saida.trechos, [])
        self.assertFalse(saida.tem_o_que_desenhar)

    def test_fragmento_de_um_ponto_so_e_descartado(self):
        # Um ponto solto não desenha trajeto: viraria um pingo na borda da
        # zona, que é justamente onde ele não pode estar.
        j = viagem.Jornada([trecho([0, 1100, 0, 0, 2000, 2500])])
        saida = p.esconde_jornada(j, so_casa())
        self.assertEqual(len(saida.trechos), 1)
        self.assertEqual([round(_metros_da_casa(q)) for q in saida.pontos],
                         [2000, 2500])

    def test_um_trecho_solto_vale_como_jornada_de_um_trecho(self):
        # A pagina do mapa tem um caminho de recuo que le o arquivo direto
        # quando ele nao casa com nenhuma jornada. Sem isto, ali o mapa
        # viraria 500.
        saida = p.esconde_jornada(trecho([0, 300, 1400, 2500]), so_casa())
        self.assertEqual([round(_metros_da_casa(q)) for q in saida.pontos],
                         [1400, 2500])
        self.assertTrue(saida.trechos[0].inicio_oculto)

    def test_trecho_solto_sem_zona_nenhuma_vira_jornada_intacta(self):
        t = trecho([0, 300, 1400])
        saida = p.esconde_jornada(t, p.Privacidade(zonas=()))
        self.assertEqual(saida.trechos, [t])

    def test_duas_zonas_recortam_as_duas(self):
        longe = p.ZonaPrivada(lat=ao_norte(10_000), lon=CASA.lon)
        dois = p.Privacidade(zonas=(CASA, longe), raio_m=1000.0, folga_m=0.0)
        j = viagem.Jornada([trecho([0, 2000, 5000, 9500, 12000])])
        saida = p.esconde_jornada(j, dois)
        self.assertEqual([round(_metros_da_casa(q)) for q in saida.pontos],
                         [2000, 5000])


class Totais(unittest.TestCase):
    """Os números do painel continuam sendo os da viagem gravada."""

    def test_os_numeros_sao_os_do_arquivo_e_nao_os_do_que_sobrou(self):
        # A listagem em /viagens já publica o total, e ela não tem
        # coordenada nenhuma. Recalcular aqui só criaria duas respostas
        # diferentes para a mesma pergunta.
        t = trecho([0, 300, 1400, 2500, 4000])
        j = viagem.Jornada([t])
        saida = p.esconde_jornada(j, so_casa())
        self.assertAlmostEqual(saida.distancia_km, j.distancia_km)
        self.assertEqual(saida.duracao_gravada, j.duracao_gravada)
        self.assertEqual(saida.inicio, j.inicio)
        self.assertEqual(saida.fim, j.fim)
        self.assertEqual(len(saida.pontos_gravados), 5)

    def test_o_corte_nao_inventa_pausas(self):
        # `pausas` conta carro desligado. Um corte de privacidade parte a
        # trilha sem que ninguém tenha parado, e contá-lo como pausa poria no
        # painel uma parada que não houve.
        j = viagem.Jornada([trecho([3000, 2000, 400, 2000, 3200])])
        saida = p.esconde_jornada(j, so_casa())
        self.assertEqual(len(saida.trechos), 2)
        self.assertEqual(saida.pausas, 0)

    def test_pausa_de_verdade_continua_contando(self):
        a = trecho([2000, 3000], nome="20261015_080000.log", t0=0)
        b = trecho([3000, 4000], nome="20261015_090000.log", t0=3600)
        saida = p.esconde_jornada(viagem.Jornada([a, b]), so_casa())
        self.assertEqual(saida.pausas, 1)

    def test_jornada_sem_corte_nao_carrega_copia(self):
        j = viagem.Jornada([trecho([2000, 3000, 4000])])
        saida = p.esconde_jornada(j, so_casa())
        self.assertFalse(saida.cortada)
        self.assertEqual(len(saida.trechos), 1)


class Folga(unittest.TestCase):
    """O raio sorteado, que é o que impede ajustar um círculo à borda."""

    def test_nunca_corta_menos_que_o_raio_pedido(self):
        for nome in [f"2026101{d}_0{h}0000.log"
                     for d in range(5) for h in range(6)]:
            raio = so_casa(folga_m=400.0).raio_de(0, nome)
            self.assertGreaterEqual(raio, 1000.0)
            self.assertLessEqual(raio, 1400.0)

    def test_a_mesma_viagem_corta_sempre_no_mesmo_raio(self):
        # Recarregar a página não pode mover a borda: duas renderizações com
        # bordas diferentes dariam duas amostras do mesmo trajeto, que é
        # justo o que o sorteio existe para negar.
        conf = so_casa(folga_m=400.0)
        self.assertEqual(conf.raio_de(0, "20261015_080000.log"),
                         conf.raio_de(0, "20261015_080000.log"))

    def test_viagens_diferentes_cortam_em_raios_diferentes(self):
        # ⚠️ É a defesa contra a fraqueza conhecida do recorte circular: com
        # raio fixo, três pontas de trajeto determinam a circunferência — e o
        # centro dela é a casa.
        conf = so_casa(folga_m=400.0)
        raios = {round(conf.raio_de(0, f"2026101{d}_080000.log"), 3)
                 for d in range(8)}
        self.assertGreater(len(raios), 5)

    def test_folga_zero_devolve_o_raio_exato(self):
        self.assertEqual(so_casa(folga_m=0.0).raio_de(0, "x"), 1000.0)

    def test_zonas_diferentes_tem_raios_diferentes_na_mesma_viagem(self):
        conf = p.Privacidade(zonas=(CASA, CASA), raio_m=1000.0, folga_m=400.0)
        self.assertNotEqual(conf.raio_de(0, "a.log"), conf.raio_de(1, "a.log"))


class Configuracao(unittest.TestCase):
    """Ler a seção `[privacidade]` do `aparelhos.cfg`."""

    def test_uma_zona_com_raio_padrao(self):
        conf = p.monta({"zona": ["-10.0,-40.0"]})
        self.assertEqual(conf.zonas, (CASA,))
        self.assertEqual(conf.raio_m, p.RAIO_PADRAO_M)

    def test_espaco_em_volta_da_virgula_nao_atrapalha(self):
        self.assertEqual(p.monta({"zona": [" -10.0 , -40.0 "]}).zonas, (CASA,))

    def test_varias_zonas(self):
        conf = p.monta({"zona": ["-10.0,-40.0", "-15.5,-47.9"]})
        self.assertEqual(len(conf.zonas), 2)

    def test_raio_e_folga_configuraveis(self):
        conf = p.monta({"zona": ["-10,-40"], "raio_m": ["500"],
                        "folga_m": ["0"]})
        self.assertEqual((conf.raio_m, conf.folga_m), (500.0, 0.0))

    def test_secao_vazia_nao_liga_nada(self):
        self.assertFalse(p.monta({}).ativa)

    def test_chave_desconhecida_e_recusada(self):
        # ⚠️ O MOTIVO de isto levantar: `raio=1000` em vez de `raio_m=1000`
        # cairia num balde ignorado e o mapa sairia com o raio padrão sem
        # avisar. Pior, `zonas=` em vez de `zona=` desligaria a proteção
        # inteira — e o sintoma seria um mapa que parece certo.
        with self.assertRaises(p.ErroDePrivacidade) as e:
            p.monta({"zona": ["-10,-40"], "raio": ["500"]})
        self.assertIn("raio", str(e.exception))

    def test_zona_sem_virgula_e_recusada(self):
        with self.assertRaises(p.ErroDePrivacidade):
            p.monta({"zona": ["-10.0 -40.0"]})

    def test_zona_com_texto_e_recusada(self):
        with self.assertRaises(p.ErroDePrivacidade):
            p.monta({"zona": ["minha casa"]})

    def test_latitude_fora_da_faixa_e_recusada(self):
        with self.assertRaises(p.ErroDePrivacidade):
            p.monta({"zona": ["-91,-40"]})

    def test_longitude_fora_da_faixa_e_recusada(self):
        with self.assertRaises(p.ErroDePrivacidade):
            p.monta({"zona": ["-10,181"]})

    def test_raio_zero_e_recusado(self):
        # Raio zero não esconde nada, e quem o escreveu queria esconder.
        with self.assertRaises(p.ErroDePrivacidade):
            p.monta({"zona": ["-10,-40"], "raio_m": ["0"]})

    def test_folga_negativa_e_recusada(self):
        with self.assertRaises(p.ErroDePrivacidade):
            p.monta({"zona": ["-10,-40"], "folga_m": ["-1"]})

    def test_raio_sem_zona_nenhuma_e_recusado(self):
        # Configurar o raio e esquecer a zona é o erro que deixa a proteção
        # desligada parecendo ligada.
        with self.assertRaises(p.ErroDePrivacidade):
            p.monta({"raio_m": ["500"]})

    def test_zona_repetida_na_mesma_linha_nao_e_aceita(self):
        with self.assertRaises(p.ErroDePrivacidade):
            p.monta({"zona": ["-10,-40,1000"]})

    def test_resumo_nao_mostra_coordenada(self):
        # ⚠️ O resumo vai para o log do servidor, que não é segredo e é
        # copiado em qualquer suporte. A coordenada da casa não pode estar
        # nele.
        conf = p.monta({"zona": ["-10.0,-40.0"]})
        texto = conf.resumo()
        self.assertNotIn("-10.0", texto)
        self.assertNotIn("-40.0", texto)
        self.assertIn("1", texto)


def _metros_da_casa(q) -> float:
    leste = (q.lon - CASA.lon) * M_POR_GRAU * math.cos(math.radians(CASA.lat))
    norte = (q.lat - CASA.lat) * M_POR_GRAU
    return math.hypot(leste, norte)


class NoMapa(unittest.TestCase):
    """O que efetivamente sai no HTML — que é onde o vazamento aconteceria."""

    @classmethod
    def setUpClass(cls):
        try:
            import folium  # noqa: F401,PLC0415
        except ImportError:
            raise unittest.SkipTest("sem folium")

    def test_nenhuma_coordenada_do_html_cai_dentro_da_zona(self):
        # ⚠️ **É o teste que interessa.** Os outros conferem a lógica; este
        # confere o produto. Qualquer caminho novo que volte a pôr posição na
        # página — um marcador, um ajuste de enquadramento, uma camada — cai
        # aqui.
        j = viagem.Jornada([trecho([0, 200, 700, 1300, 2600, 5000])])
        html = viagem.desenha(p.esconde_jornada(j, so_casa()))
        dentro = [par for par in _pares_de_coordenadas(html)
                  if _dentro(par, 1000.0)]
        self.assertEqual(dentro, [])

    def test_o_par_do_centro_nao_aparece_escrito(self):
        # A rota destes testes corre no mesmo meridiano do centro, entao a
        # longitude dele aparece sozinha e isso nao e vazamento. O que nao
        # pode aparecer e o PAR -- nas duas convencoes, porque o GeoJSON
        # escreve [lon, lat] e o folium escreve [lat, lon].
        j = viagem.Jornada([trecho([0, 1300, 2600])])
        html = viagem.desenha(p.esconde_jornada(j, so_casa()))
        compacto = html.replace(" ", "").replace("\n", "")
        self.assertNotIn("[-10.0,-40.0]", compacto)
        self.assertNotIn("[-40.0,-10.0]", compacto)

    def test_sem_marcador_de_inicio_quando_o_inicio_foi_cortado(self):
        # Um pino verde "inicio" na borda do recorte é uma placa dizendo
        # "a casa está a 1 km daqui".
        j = viagem.Jornada([trecho([0, 300, 1300, 2600])])
        html = viagem.desenha(p.esconde_jornada(j, so_casa()))
        self.assertNotIn("inicio", html)

    def test_o_marcador_de_inicio_fica_quando_nada_foi_cortado(self):
        j = viagem.Jornada([trecho([1300, 2600, 4000])])
        html = viagem.desenha(p.esconde_jornada(j, so_casa()))
        self.assertIn("inicio", html)

    def test_corte_no_meio_nao_vira_marcador_de_pausa(self):
        # O pino de pausa carrega um horário e fica exatamente na borda do
        # recorte: dois deles, de duas viagens, já valem uma corda do
        # círculo.
        j = viagem.Jornada([trecho([3000, 2000, 400, 2000, 3200])])
        html = viagem.desenha(p.esconde_jornada(j, so_casa()))
        self.assertNotIn("pausa de", html)

    def test_o_painel_avisa_que_ha_trecho_oculto(self):
        # Sem o aviso, a falha na trilha se lê como perda de sinal — e este
        # projeto já gastou caro com aparelho que mente sobre o próprio
        # estado.
        j = viagem.Jornada([trecho([0, 300, 1300, 2600])])
        html = viagem.desenha(p.esconde_jornada(j, so_casa()))
        self.assertIn("zona privada", html)

    def test_o_painel_conta_os_pontos_da_viagem_inteira(self):
        # A nota ao pe do painel promete que os numeros sao os da viagem
        # inteira, e a listagem em /viagens publica este mesmo total.
        j = viagem.Jornada([trecho([0, 300, 1300, 2600, 4000])])
        html = viagem.desenha(p.esconde_jornada(j, so_casa()))
        self.assertIn("<strong>5</strong>", html)

    def test_viagem_sem_corte_nao_ganha_aviso(self):
        j = viagem.Jornada([trecho([1300, 2600, 4000])])
        html = viagem.desenha(p.esconde_jornada(j, so_casa()))
        self.assertNotIn("zona privada", html)

    def test_radar_dentro_da_zona_nao_entra_no_mapa(self):
        # Os radares são escolhidos por proximidade da rota. Com a rota
        # recortada, os de dentro da zona não podem mais ser selecionados —
        # senão eles próprios desenham o pedaço apagado.
        j = p.esconde_jornada(
            viagem.Jornada([trecho([0, 300, 1300, 2600])]), so_casa())
        radares = [viagem.RadarNoMapa(lat=ao_norte(100), lon=CASA.lon,
                                      limite=60),
                   viagem.RadarNoMapa(lat=ao_norte(1400), lon=CASA.lon,
                                      limite=80)]
        perto = viagem.radares_na_rota(j, radares)
        self.assertEqual([r.limite for r in perto], [80])


#: Qualquer número com pelo menos quatro casas decimais. Coordenada escrita
#: por folium ou por `json.dumps` cai aqui; `weight: 4` e `zoom: 14` não.
_NUMERO = re.compile(r"-?\d+\.\d{4,}")


def _pares_de_coordenadas(html: str) -> list[tuple[float, float]]:
    """Todo par de números vizinhos do HTML, nas DUAS ordens.

    O GeoJSON escreve `[lon, lat]` e o folium escreve `[lat, lon]`. Testar só
    uma das convenções deixaria passar metade dos vazamentos, então aqui se
    testam as duas — um falso positivo custa um teste reprovado, e um falso
    negativo custa o endereço.
    """
    n = [float(x) for x in _NUMERO.findall(html)]
    pares = []
    for a, b in zip(n, n[1:]):
        pares.append((a, b))
        pares.append((b, a))
    return pares


def _dentro(par: tuple[float, float], raio_m: float) -> bool:
    lat, lon = par
    if not (-90.0 <= lat <= 90.0 and -180.0 <= lon <= 180.0):
        return False
    leste = (lon - CASA.lon) * M_POR_GRAU * math.cos(math.radians(CASA.lat))
    norte = (lat - CASA.lat) * M_POR_GRAU
    return math.hypot(leste, norte) <= raio_m


if __name__ == "__main__":
    unittest.main()
