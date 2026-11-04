"""Check the authoritative Integrator graph, including clock/reset consumers."""
from pathlib import Path
import importlib.util
import unittest
import xml.etree.ElementTree as ET


class ClockTopologyTests(unittest.TestCase):
    def setUp(self):
        root = ET.parse(Path(__file__).resolve().parents[1] / "design_0/Design.xml").getroot()
        self.nodes = list(root.iter())
        self.nets = {}
        for node in self.nodes:
            if node.tag.rsplit("}", 1)[-1] != "adHocConnection":
                continue
            name = next(c.text for c in node if c.tag.rsplit("}", 1)[-1] == "name")
            refs = []
            for ref in node.iter():
                kind = ref.tag.rsplit("}", 1)[-1]
                if kind == "internalPortReference":
                    refs.append((ref.get("componentRef"), ref.get("portRef")))
                elif kind == "externalPortReference":
                    refs.append(("external", ref.get("portRef")))
            self.nets[name] = refs

    def network(self, endpoint):
        found = [refs for refs in self.nets.values() if endpoint in refs]
        self.assertEqual(len(found), 1, endpoint)
        return found[0]

    def test_gp0_remains_75mhz(self):
        self.assertIn(("pll_75m", "clk0_out"),
                      self.network(("ARM_Processor_System_0", "master_gp0_axi_aclk")))

    def test_all_150mhz_logic_moves_with_ps_ports(self):
        shared = self.network(("pll_75m", "clk1_out"))
        required = [
            ("ARM_Processor_System_0", "master_gp1_axi_aclk"),
            ("ARM_Processor_System_0", "slave_hp0_axi_aclk"),
            ("ARM_Processor_System_0", "slave_hp1_axi_aclk"),
            ("Video_NPU_0", "S_AXILite_ACLK"), ("Video_NPU_0", "M_AXI_ACLK"),
            ("AXI_Protocol_Converter_1", "aclk"), ("AXI_Matrix_1", "aclk"),
            ("AXI_Matrix_2", "aclk"), ("VDMA_0", "m_axi_mm2s_aclk"),
            ("VDMA_0", "m_axis_mm2s_aclk"), ("AXI4_Stream_to_Video_Out_0", "aclk"),
            ("rstn_npu", "clk"), ("rstn_150m", "clk"), ("external", "clk_150m"),
        ]
        for endpoint in required:
            self.assertIn(endpoint, shared)
            self.assertEqual(self.network(endpoint), shared)

    def test_resets_follow_the_same_pll_lock(self):
        shared = self.network(("pll_75m", "lock"))
        for endpoint in [("rst_n_75m", "pll_locked"), ("rstn_npu", "pll_locked"),
                         ("rstn_150m", "pll_locked"), ("AXI4_Stream_to_Video_Out_0", "aclken")]:
            self.assertIn(endpoint, shared)

    def test_no_references_to_removed_plls(self):
        removed = {"PLL_npu", "pll_150m"}
        for node in self.nodes:
            self.assertNotIn(node.get("componentRef"), removed)
            if node.tag.rsplit("}", 1)[-1] == "instanceName":
                self.assertNotIn(node.text, removed)

    def test_actual_generated_hdl_matches_graph(self):
        project = Path(__file__).resolve().parents[1]
        spec = importlib.util.spec_from_file_location("consolidate_clocks", project / "src/conveyor/consolidate_clocks.py")
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        groups = helper.clock_groups(project)
        text = (project / "design_0/hdl/design_0.v").read_text(encoding="utf-8")
        helper.validate(text, *groups)
        self.assertEqual(helper.transform(text, *groups), text)
        stale = text.replace(".clk1_out(w_port__pll_75m__clk1_out)", ".clk1_out()")
        self.assertNotEqual(stale, text)
        with self.assertRaisesRegex(ValueError, "150 MHz PLL output"):
            helper.validate(stale, *groups)


if __name__ == "__main__":
    unittest.main()
