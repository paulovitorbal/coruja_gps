"""Testes do simulador do NEO-M8N.

Rode com:  python3 -m unittest discover -s simulador/testes -v

`unittest` e nao pytest, pela mesma razao do resto do projeto: tudo aqui e
stdlib-only por escolha.

Estes testes conduzem o simulador por uma **pty de verdade**, e nao por pipe.
Nao e capricho: o que eles verificam e justamente o comportamento do terminal
— modo canonico contra cbreak — e um pipe nao tem disciplina de linha para
exercitar. Testar por pipe passaria sem medir nada.
"""
import os
import pty
import select
import signal
import subprocess
import sys
import time
import unittest
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
SIMULADOR = RAIZ / "simulador" / "simula_gps.py"


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


if __name__ == "__main__":
    unittest.main()
