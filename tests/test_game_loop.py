"""Execute the production scheduler with real car, game and race stepping."""

import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class GameLoopTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        if not compiler or not shutil.which(compiler[0]):
            raise unittest.SkipTest("Host C compiler unavailable (set HOST_CC)")
        cls.directory = tempfile.TemporaryDirectory(prefix="notracin-game-loop-")
        cls.addClassCleanup(cls.directory.cleanup)
        work = Path(cls.directory.name)
        source = (ROOT / "src/main.c").read_text()
        start = source.index("        // Fixed 60 Hz physics")
        end = source.index("        label_timer -= steps;", start)
        (work / "main_loop_body.inc").write_text(source[start:end])
        constant = re.search(r"^#define STEP_CYCLES[^\n]+", source, re.M).group()
        lengths = re.search(r"    static const u32 step_len\[3\][^\n]+", source).group()
        (work / "game_loop_settings.inc").write_text(constant + "\n" + lengths + "\n")
        header = work / "gba_host.h"
        header.write_text((ROOT / "src/gba.h").read_text().replace('target("arm"), ', ""))
        cls.binary = work / "game_loop"
        command = compiler + [
            "-std=c99", "-O2", "-ffunction-sections", "-fdata-sections",
            "-Wno-pointer-to-int-cast", "-include", str(header),
            "-I", str(work), "-I", str(ROOT / "src"), "-Wl,--gc-sections",
            str(ROOT / "tests/game_loop_host.c"),
            *[str(ROOT / "src" / name) for name in
              ("car.c", "softbody.c", "game.c", "race.c", "tune.c", "render.c")],
            "-o", str(cls.binary),
        ]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def run_case(self, *args):
        result = subprocess.run([str(self.binary), *map(str, args)],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def check_boundary(self, scenario):
        for model in (0, 1):
            with self.subTest(model=model):
                single = self.run_case(scenario, 1, model).replace("group=1", "group=N")
                batch = self.run_case(scenario, 4, model).replace("group=4", "group=N")
                self.assertEqual(single, batch)

    def test_fuel_depletion_does_not_depend_on_drawing(self):
        self.check_boundary("fuel")

    def test_countdown_release_does_not_depend_on_drawing(self):
        self.check_boundary("countdown")

    def test_crash_stops_boost_without_waiting_for_a_draw(self):
        self.check_boundary("crash")

    def test_recovery_rechecks_controls_without_waiting_for_a_draw(self):
        self.check_boundary("recover")

    def test_goal_stops_held_steering_and_nitro_on_the_next_step(self):
        self.check_boundary("goal")

    def test_timeout_brakes_on_the_next_step(self):
        self.check_boundary("timeover")

    def test_fractional_time_survives_the_four_step_limit(self):
        for speed in range(3):
            for slowmo in (0, 1):
                for percent in (100, 310, 350, 375, 390, 400):
                    with self.subTest(speed=speed, slowmo=slowmo, percent=percent):
                        output = self.run_case("cadence", percent, 1000, speed, slowmo, 1)
                        fields = dict(word.split("=") for word in output.split()[1:])
                        self.assertEqual(fields["actual_steps"], fields["expected_steps"])

    def test_real_overload_keeps_the_step_cap_and_fraction(self):
        for speed in range(3):
            for slowmo in (0, 1):
                with self.subTest(speed=speed, slowmo=slowmo):
                    output = self.run_case("cadence", 410, 1000, speed, slowmo, 1)
                    fields = dict(word.split("=") for word in output.split()[1:])
                    self.assertEqual(int(fields["actual_steps"]), 4000)
                    # Four-step overload discards only whole steps, never the fraction.
                    lengths = (280896, 280896 * 10 // 13, 280896 * 4 // 3)
                    interval = lengths[speed] * (3 if slowmo else 1)
                    self.assertEqual(int(fields["remainder"]),
                                     int(fields["elapsed"]) % interval)


if __name__ == "__main__":
    unittest.main()
