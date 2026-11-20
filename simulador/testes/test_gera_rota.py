"""Testes do gerador da rota do Eixão.

    python3 -m unittest discover -s simulador/testes

⚠️ **Nenhum destes testes vai à rede.** A Overpass entra pelo
`--de-arquivo`, com respostas sintéticas montadas aqui — é o que permite
exercitar a resposta curta, a vazia e o `remark` de tempo esgotado, que são
justamente os casos em que o CSV versionado era destruído.
"""
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "simulador"))

import gera_rota as G  # noqa: E402
from geo import M_POR_GRAU  # noqa: E402


def via(lat0: float, lat1: float, lon: float, nos: int = 60,
        nome: str = "Eixão", ref: str = "DF-002") -> dict:
    """Um `way` da Overpass, reto, de `lat0` a `lat1`."""
    passo = (lat1 - lat0) / (nos - 1)
    return {
        "type": "way",
        "tags": {"ref": ref, "name": nome},
        "geometry": [{"lat": lat0 + k * passo, "lon": lon}
                     for k in range(nos)],
    }


def resposta(*ways, **extra) -> dict:
    return {"version": 0.6, "elements": list(ways), **extra}


#: Duas pistas a ~40 m, do tamanho do Eixão: 16,5 km em latitude.
GRAUS_DE_16_5_KM = 16_500.0 / M_POR_GRAU
EIXAO = (via(-15.86, -15.86 + GRAUS_DE_16_5_KM, -47.93),
         via(-15.86, -15.86 + GRAUS_DE_16_5_KM, -47.9296))


class Fixture(unittest.TestCase):
    """O gerador aceita a resposta sintética, para os outros testes valerem."""

    def test_a_via_sintetica_tem_o_tamanho_do_eixao(self):
        rota = G.linha_de_centro(G.nos_do_eixao(list(EIXAO)))
        km = G.comprimento_km(rota)
        self.assertAlmostEqual(km, 16.5, delta=0.3)


class Validacao(unittest.TestCase):
    def test_a_rota_versionada_passa_na_propria_conferencia(self):
        # Amarra o COMPRIMENTO_KM ao arquivo que o simulador usa: um
        # limite apertado demais recusaria a proxima regeracao legitima, e
        # so se descobriria na hora de regerar.
        pontos = [tuple(float(x) for x in l.split(","))
                  for l in (RAIZ / "simulador" / "rota_eixao.csv").read_text(
                      encoding="utf-8").splitlines()
                  if l and not l.startswith(("#", "lat"))]
        self.assertIsNone(G.confere(pontos))


    def test_a_rota_do_tamanho_certo_passa(self):
        self.assertIsNone(G.confere(G.linha_de_centro(
            G.nos_do_eixao(list(EIXAO)))))

    def test_rota_curta_demais_e_recusada(self):
        # O caso real: a Overpass devolve meia via sob carga, e o CSV era
        # truncado sem ninguem ver -- o simulador passava a andar por um
        # pedaco do Eixao, com os radares do resto fora de alcance.
        curta = via(-15.86, -15.86 + GRAUS_DE_16_5_KM / 3, -47.93)
        erro = G.confere(G.linha_de_centro(G.nos_do_eixao([curta])))
        self.assertIsNotNone(erro)
        self.assertIn("km", erro)

    def test_rota_que_sai_da_caixa_e_recusada(self):
        # Uma via com a mesma `ref` noutro lugar puxaria a rota para fora
        # do Plano Piloto, e o comprimento sozinho nao pega isso.
        longe = via(-15.80, -15.80 + GRAUS_DE_16_5_KM, -47.50)
        erro = G.confere(G.linha_de_centro(G.nos_do_eixao([longe])))
        self.assertIsNotNone(erro)
        self.assertIn("caixa", erro)

    def test_remark_de_tempo_esgotado_e_erro_e_nao_rota_vazia(self):
        # A Overpass responde 200 com `remark` e a lista vazia. Lido como
        # resposta boa, viraria "nenhuma via DF-002 retornada" -- mensagem
        # que manda procurar o problema no lugar errado.
        with self.assertRaises(ValueError) as e:
            G.elementos_de(resposta(remark="runtime error: Query timed out"))
        self.assertIn("Overpass", str(e.exception))
        self.assertIn("timed out", str(e.exception))

    def test_json_sem_elements_e_erro(self):
        with self.assertRaises(ValueError):
            G.elementos_de({"version": 0.6})


class GravaOCsv(unittest.TestCase):
    def roda(self, pacote: dict, saida: Path, *args) -> int:
        entrada = saida.parent / "resposta.json"
        entrada.write_text(json.dumps(pacote), encoding="utf-8")
        with mock.patch.object(G, "SAIDA", saida), \
                redirect_stdout(io.StringIO()) as tela:
            codigo = G.main(["gera_rota.py", "--de-arquivo", str(entrada),
                             *args])
        self.saida_do_console = tela.getvalue()
        return codigo

    def test_resposta_boa_escreve_a_rota(self):
        with tempfile.TemporaryDirectory() as tmp:
            csv = Path(tmp) / "rota.csv"
            self.assertEqual(0, self.roda(resposta(*EIXAO), csv))
            texto = csv.read_text(encoding="utf-8")
        self.assertIn("lat,lon\n", texto)
        self.assertIn("GERADO por gera_rota.py", texto)
        pontos = [l for l in texto.splitlines()
                  if l and not l.startswith(("#", "lat"))]
        self.assertGreater(len(pontos), 50)

    def test_resposta_ruim_NAO_toca_no_csv_versionado(self):
        # Era o defeito: o arquivo era aberto em "w" antes de qualquer
        # conferencia, entao uma resposta curta truncava a rota boa.
        curta = via(-15.86, -15.86 + GRAUS_DE_16_5_KM / 3, -47.93)
        with tempfile.TemporaryDirectory() as tmp:
            csv = Path(tmp) / "rota.csv"
            csv.write_text("a rota boa\n", encoding="utf-8")
            self.assertEqual(1, self.roda(resposta(curta), csv))
            self.assertEqual(csv.read_text(encoding="utf-8"), "a rota boa\n")
        self.assertIn("km", self.saida_do_console)

    def test_remark_nao_toca_no_csv_e_diz_que_foi_a_overpass(self):
        with tempfile.TemporaryDirectory() as tmp:
            csv = Path(tmp) / "rota.csv"
            csv.write_text("a rota boa\n", encoding="utf-8")
            self.assertEqual(1, self.roda(
                resposta(remark="runtime error: Query timed out"), csv))
            self.assertEqual(csv.read_text(encoding="utf-8"), "a rota boa\n")
        self.assertIn("Overpass", self.saida_do_console)

    def test_a_troca_e_atomica(self):
        # Entre o truncar e o escrever, uma interrupcao deixava o CSV pela
        # metade -- e meia rota ainda carrega no simulador. A gravacao vai
        # a um temporario e so entao substitui.
        with tempfile.TemporaryDirectory() as tmp:
            csv = Path(tmp) / "rota.csv"
            csv.write_text("a rota boa\n", encoding="utf-8")
            with mock.patch.object(G.os, "replace",
                                   side_effect=OSError("disco cheio")):
                with self.assertRaises(OSError):
                    self.roda(resposta(*EIXAO), csv)
            self.assertEqual(csv.read_text(encoding="utf-8"), "a rota boa\n")
            self.assertEqual(list(Path(tmp).glob("rota.csv*")), [csv],
                             "sobrou um temporario ao lado do CSV")

    def test_a_ponte_do_bragueto_nao_entra_na_linha_de_centro(self):
        # Ela compartilha a `ref` e fica a leste do Eixao: entrar puxaria a
        # linha de centro para fora da via. A rota com ela tem de ser a
        # MESMA que sem ela.
        ponte = via(-15.76, -15.74, -47.86, nome="Ponte do Bragueto")
        with tempfile.TemporaryDirectory() as tmp:
            sem = Path(tmp) / "rota.csv"
            self.assertEqual(0, self.roda(resposta(*EIXAO), sem))
            pontos_sem = [l for l in sem.read_text().splitlines()
                          if l and not l.startswith(("#", "lat"))]
            com = Path(tmp) / "rota2.csv"
            self.assertEqual(0, self.roda(resposta(*EIXAO, ponte), com))
            pontos_com = [l for l in com.read_text().splitlines()
                          if l and not l.startswith(("#", "lat"))]
        self.assertEqual(pontos_sem, pontos_com)


if __name__ == "__main__":
    unittest.main()
