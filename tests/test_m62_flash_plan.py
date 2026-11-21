import configparser
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FLASH_CONFIG = ROOT / "firmware" / "flash_prog_cfg_m62_app.ini"


class M62FlashPlanTests(unittest.TestCase):
    def test_programming_plan_only_erases_and_writes_slot_zero_app(self):
        config = configparser.ConfigParser()
        config.read(FLASH_CONFIG)

        self.assertEqual(config.sections(), ["cfg", "FW"])
        self.assertEqual(config["cfg"].getint("erase"), 1)
        self.assertEqual(config["cfg"].getint("boot2_isp_mode"), 0)
        self.assertEqual(int(config["FW"]["address"], 0), 0x10000)
        self.assertEqual(
            config["FW"]["filedir"],
            "./build/build_out/uni_usb_dongle_bl616.bin",
        )


if __name__ == "__main__":
    unittest.main()
