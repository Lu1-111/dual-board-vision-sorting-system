"""Verify actual generated PS input wiring and fail-closed regeneration."""
from pathlib import Path
import importlib.util
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("servo_status", ROOT / "src/conveyor/integrate_servo_status.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ServoStatusIntegrationTests(unittest.TestCase):
    def test_actual_generated_status_wiring_is_idempotent(self):
        text = (ROOT / "design_0/hdl/design_0.v").read_text(encoding="utf-8")
        self.assertEqual(module.transform(text), text)
        self.assertIn(".pl_gpio_in(pl_gpio_in)", text)
        top = (ROOT / "src/system_top.v").read_text(encoding="utf-8")
        self.assertIn("{8'd0, servo_status, 50'd0}", top)
        self.assertIn(".pl_gpio_in(pl_gpio_in)", top)

    def test_unrecognized_ps_input_fails(self):
        with self.assertRaisesRegex(ValueError, "exactly one"):
            module.transform("input wire [63:0] pl_gpio_in; .pl_gpio_in(unknown)")

    def test_regenerated_zero_tie_is_reconnected(self):
        text = "module design_0(output wire [63:0] pl_gpio_out, input a); ps u(.pl_gpio_in(64'd0)); endmodule"
        transformed = module.transform(text)
        self.assertIn("input wire [63:0] pl_gpio_in", transformed)
        self.assertIn(".pl_gpio_in(pl_gpio_in)", transformed)
        self.assertEqual(module.transform(transformed), transformed)


if __name__ == "__main__":
    unittest.main()
