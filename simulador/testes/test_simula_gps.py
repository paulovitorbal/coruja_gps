"""Testes do simulador do NEO-M8N.

Rode com:  python3 -m unittest discover -s simulador/testes -v

`unittest` e nao pytest, pela mesma razao do resto do projeto: tudo aqui e
stdlib-only por escolha.

Estes testes conduzem o simulador por uma **pty de verdade**, e nao por pipe.
Nao e capricho: o que eles verificam e justamente o comportamento do terminal
— modo canonico contra cbreak — e um pipe nao tem disciplina de linha para
exercitar. Testar por pipe passaria sem medir nada.
"""
import io
import os
import pty
import re
import select
import signal
import subprocess
import sys
import tempfile
import termios
import time
import unittest
from contextlib import redirect_stderr
from datetime import datetime, timezone
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
SIMULADOR = RAIZ / "simulador" / "simula_gps.py"
sys.path.insert(0, str(RAIZ / "simulador"))

import simula_gps as S  # noqa: E402


def confere_checksum(frase: str) -> bool:
    corpo, _, cs = frase.strip().partition("*")
    calculado = 0
    for c in corpo[1:]:
        calculado ^= ord(c)
    return f"{calculado:02X}" == cs


class FraseRmc(unittest.TestCase):
    """A sentenca como funcao pura: o que o parser do firmware vai ler."""

    AGORA = datetime(2026, 11, 19, 12, 34, 56, tzinfo=timezone.utc)

    def campos(self, *args, **kw) -> list[str]:
        frase = S.frase_rmc(*args, **kw)
        self.assertTrue(frase.endswith("\r\n"))
        self.assertTrue(confere_checksum(frase), frase)
        return frase.split("*")[0].split(",")

    def test_brasilia_e_sul_e_oeste(self):
        c = self.campos(-15.8, -47.9, 60.0, 12.0, self.AGORA)
        self.assertEqual(c[0], "$GNRMC")
        self.assertEqual(c[3:7], ["1548.0000", "S", "04754.0000", "W"])
        self.assertEqual(c[9], "191126")

    def test_norte_e_leste(self):
        c = self.campos(10.5, 20.25, 60.0, 12.0, self.AGORA)
        self.assertEqual(c[3:7], ["1030.0000", "N", "02015.0000", "E"])

    def test_minuto_nunca_sai_60(self):
        # 15 graus e 59,99999 minutos: arredondado a 4 casas, o minuto
        # virava "60.0000" e a sentenca dizia 15 graus e 60 minutos, que o
        # parser le como 16 graus -- ou recusa. O certo e levar ao grau.
        lat = -(15 + 59.99999 / 60.0)
        c = self.campos(lat, -47.9, 60.0, 12.0, self.AGORA)
        self.assertEqual(c[3], "1600.0000")

    def test_o_carimbo_tem_a_fracao_do_quarto_de_segundo(self):
        # A 4 Hz o modulo carimba .00, .25, .50, .75. Sempre .00 seriam
        # quatro sentencas com o mesmo instante por segundo.
        for us, esperado in ((0, "123456.00"), (250000, "123456.25"),
                             (500000, "123456.50"), (750000, "123456.75")):
            c = self.campos(-15.8, -47.9, 60.0, 12.0,
                            self.AGORA.replace(microsecond=us))
            self.assertEqual(c[1], esperado)

    def test_parado_o_rumo_fica_em_branco(self):
        c = self.campos(-15.8, -47.9, 0.0, 12.0, self.AGORA)
        self.assertEqual(c[8], "")
        self.assertEqual(c[7], "0.00")


class Carimbos(unittest.TestCase):
    def test_a_grade_de_quarto_de_segundo(self):
        inicio = datetime(2026, 11, 19, 12, 0, 59, 600000, tzinfo=timezone.utc)
        base = S.inicio_da_grade(inicio)
        self.assertEqual(base, inicio.replace(microsecond=500000))
        self.assertEqual(S.carimbo(base, 3),
                         datetime(2026, 11, 19, 12, 1, 0, 250000,
                                  tzinfo=timezone.utc))


class Velocidade(unittest.TestCase):
    def test_acima_do_teto_e_recusada(self):
        # As teclas param em VELOCIDADE_MAXIMA_KMH; a opcao nao parava. Acima
        # de ~8600 km/h um radar cabe inteiro entre duas amostras.
        for valor in ("5000", "-10", "nan"):
            with self.assertRaises(SystemExit) as e, \
                    redirect_stderr(io.StringIO()):
                S.main(["simula_gps.py", "--velocidade", valor])
            self.assertEqual(e.exception.code, 2, valor)


class Atalho(unittest.TestCase):
    def test_so_apaga_o_atalho_que_e_seu(self):
        # Duas instancias: a segunda refez o atalho para a pty dela. Ao
        # sair, a primeira apagava o atalho da OUTRA, e quem consumia
        # perdia a serial de uma instancia que continuava viva.
        with tempfile.TemporaryDirectory() as tmp:
            link = Path(tmp) / "serial"
            link.symlink_to("/dev/ttys999")
            S.apaga_atalho_se_for_meu(link, "/dev/ttys012")
            self.assertTrue(link.is_symlink(), "apagou o atalho da outra")
            S.apaga_atalho_se_for_meu(link, "/dev/ttys999")
            self.assertFalse(link.is_symlink())


def sobe(*args, espera_prompt=False):
    """Sobe o simulador sob uma pty e devolve (processo, fd_mestre)."""
    mestre, escravo = pty.openpty()
    p = subprocess.Popen([sys.executable, str(SIMULADOR), "--sem-link", *args],
                         stdin=escravo, stdout=escravo, stderr=escravo,
                         cwd=str(RAIZ), close_fds=True)
    os.close(escravo)
    return p, mestre


def le_ate(mestre, texto, limite_s, acumulado=b""):
    """Le da pty ate `texto` aparecer. Devolve (achou, tudo_que_leu)."""
    fim = time.time() + limite_s
    while time.time() < fim:
        if select.select([mestre], [], [], 0.2)[0]:
            try:
                acumulado += os.read(mestre, 65536)
            except OSError:
                break
        if texto.encode() in acumulado:
            return True, acumulado
    return False, acumulado


def encerra(p, mestre):
    p.terminate()
    try:
        p.wait(timeout=5)
    except subprocess.TimeoutExpired:
        p.kill()
    os.close(mestre)


class TeclasDePilotagem(unittest.TestCase):
    """As teclas a/d/0/q dependem de o terminal estar em cbreak.

    Em modo canonico o kernel segura os bytes ate o Enter, entao uma tecla
    solta nunca chega ao programa.
    """

    def _velocidade_muda_com_uma_tecla(self, *args):
        p, mestre = sobe(*args)
        self.addCleanup(encerra, p, mestre)
        ok, lido = le_ate(mestre, "60.0 km/h", 15)   # velocidade padrao
        self.assertTrue(ok, f"simulador nao chegou a emitir status: {lido[-300:]!r}")
        os.write(mestre, b"d" * 50)          # UMA rajada, sem Enter
        ok, _ = le_ate(mestre, "10.0 km/h", 15, lido)
        return ok

    def test_funcionam_com_a_pausa_automatica(self):
        # REGRESSAO: `--sem-pausa` ja desligou o teclado inteiro, por ter
        # amarrado o `tty.setcbreak` a condicao da pausa em vez de a "stdin
        # e um terminal". As teclas ficavam na fila de edicao de linha
        # esperando um Enter que nunca vem, e o simulador seguia na
        # velocidade inicial sem dar sinal de que algo estava errado.
        self.assertTrue(self._velocidade_muda_com_uma_tecla("--sem-pausa"),
                        "com --sem-pausa a tecla nao surtiu efeito: "
                        "o terminal provavelmente ficou em modo canonico")


class Pausa(unittest.TestCase):
    def test_sem_a_opcao_espera_uma_tecla(self):
        p, mestre = sobe()
        self.addCleanup(encerra, p, mestre)
        ok, lido = le_ate(mestre, "qualquer tecla para iniciar", 15)
        self.assertTrue(ok, "nao apareceu o prompt da pausa")
        # Enquanto pausado nao emite status.
        _, lido = le_ate(mestre, "@@nunca@@", 2, lido)
        self.assertNotIn(b"volta", lido.split(b"iniciar")[-1],
                         "emitiu status antes de a tecla chegar")
        os.write(mestre, b"x")
        ok, _ = le_ate(mestre, "km/h", 15, lido)
        self.assertTrue(ok, "nao destravou depois da tecla")

    def test_com_a_opcao_comeca_sozinho(self):
        p, mestre = sobe("--sem-pausa")
        self.addCleanup(encerra, p, mestre)
        ok, lido = le_ate(mestre, "km/h", 15)
        self.assertTrue(ok, "nao comecou a emitir")
        self.assertNotIn(b"qualquer tecla para iniciar", lido,
                         "pausou apesar de --sem-pausa")

    def test_q_no_prompt_encerra(self):
        p, mestre = sobe()
        self.addCleanup(encerra, p, mestre)
        ok, _ = le_ate(mestre, "qualquer tecla para iniciar", 15)
        self.assertTrue(ok)
        os.write(mestre, b"q")
        try:
            self.assertEqual(p.wait(timeout=10), 0)
        except subprocess.TimeoutExpired:
            self.fail("nao encerrou com q no prompt")


class SemTerminal(unittest.TestCase):
    def test_nao_pausa_nem_gira_em_falso_com_stdin_fechado(self):
        # REGRESSAO: o laco de teclado girava em espera ocupada quando stdin
        # nao era tty -- select reporta /dev/null sempre legivel, os.read
        # devolve b"" de EOF, nenhum ramo casa. Ficava a 100% de CPU sem
        # emitir uma sentenca.
        env = dict(os.environ, PYTHONUNBUFFERED="1")
        p = subprocess.Popen([sys.executable, str(SIMULADOR), "--sem-link"],
                             stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True,
                             cwd=str(RAIZ), env=env)
        try:
            time.sleep(4)
            p.send_signal(signal.SIGTERM)
            saida = p.communicate(timeout=10)[0]
        finally:
            if p.poll() is None:
                p.kill()
        self.assertNotIn("qualquer tecla para iniciar", saida)
        self.assertIn("volta", saida.split("teclas:")[-1],
                      "nao emitiu status: provavelmente travou no laco de teclado")


class LadoEscravo(unittest.TestCase):
    """O que sai na SERIAL, lido como o aparelho le: do lado escravo."""

    def test_rmc_valida_a_4_hz_com_carimbo_em_quartos(self):
        env = dict(os.environ, PYTHONUNBUFFERED="1")
        p = subprocess.Popen([sys.executable, str(SIMULADOR), "--sem-link"],
                             stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, cwd=str(RAIZ), env=env)
        self.addCleanup(lambda: p.poll() is None and p.kill())
        self.addCleanup(p.stdout.close)

        caminho = None
        fim = time.time() + 15
        while caminho is None and time.time() < fim:
            linha = p.stdout.readline().decode(errors="replace")
            m = re.search(r"serial: (\S+)", linha)
            caminho = m.group(1) if m else None
        self.assertIsNotNone(caminho, "o simulador nao disse qual e a pty")

        fd = os.open(caminho, os.O_RDONLY | os.O_NOCTTY)
        self.addCleanup(os.close, fd)
        lido = b""
        fim = time.time() + 3
        while lido.count(b"\r\n") < 9 and time.time() < fim:
            if select.select([fd], [], [], 0.2)[0]:
                lido += os.read(fd, 4096)
        p.send_signal(signal.SIGTERM)
        p.wait(timeout=10)

        frases = [f for f in lido.decode().split("\r\n")
                  if f.startswith("$GNRMC")][:8]
        self.assertGreaterEqual(len(frases), 8, lido[-300:])
        for f in frases:
            self.assertTrue(confere_checksum(f), f)
        # Sentencas consecutivas, carimbo andando um quarto de segundo.
        instantes = []
        for f in frases:
            hhmmss = f.split(",")[1]
            h, m, s = int(hhmmss[0:2]), int(hhmmss[2:4]), float(hhmmss[4:])
            instantes.append(h * 3600 + m * 60 + s)
        passos = {round(b - a, 2) for a, b in zip(instantes, instantes[1:])}
        self.assertEqual(passos, {0.25}, instantes)


class Sinal(unittest.TestCase):
    def test_sigterm_devolve_o_terminal_ao_modo_de_antes(self):
        # Sem tratar o SIGTERM o processo morria sem passar pelo `finally`,
        # e o shell de quem o rodou ficava em cbreak, sem eco.
        mestre, escravo = pty.openpty()
        self.addCleanup(os.close, mestre)
        self.addCleanup(os.close, escravo)
        antes = termios.tcgetattr(escravo)
        p = subprocess.Popen([sys.executable, str(SIMULADOR), "--sem-link",
                              "--sem-pausa"],
                             stdin=escravo, stdout=escravo, stderr=escravo,
                             cwd=str(RAIZ), close_fds=True)
        self.addCleanup(lambda: p.poll() is None and p.kill())
        ok, _ = le_ate(mestre, "km/h", 15)
        self.assertTrue(ok, "o simulador nao comecou")
        self.assertFalse(termios.tcgetattr(escravo)[3] & termios.ICANON,
                         "o simulador nem chegou a ligar o cbreak")
        p.send_signal(signal.SIGTERM)
        p.wait(timeout=10)
        depois = termios.tcgetattr(escravo)
        self.assertEqual(depois[3] & (termios.ICANON | termios.ECHO),
                         antes[3] & (termios.ICANON | termios.ECHO))


if __name__ == "__main__":
    unittest.main()
