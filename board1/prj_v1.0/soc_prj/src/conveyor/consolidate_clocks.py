"""Keep generated HDL clock/reset wiring consistent with the Integrator graph.

Run through integrate_conveyor.py after DI regeneration. This only applies the
reviewed shared-PLL topology; it fails instead of guessing on an unknown graph.
"""
from pathlib import Path
import re
import xml.etree.ElementTree as ET

PROJECT = Path(__file__).resolve().parents[2]


def clock_groups(project=PROJECT):
    root = ET.parse(project / "design_0/Design.xml").getroot()
    groups = {}
    for node in root.iter():
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
        groups[name] = refs
    clocks = groups.get("w_net__pll_75m__clk1_out", [])
    locks = groups.get("w_net__pll_75m__lock", [])
    for endpoint in [("pll_75m", "clk1_out"),
                     ("ARM_Processor_System_0", "master_gp1_axi_aclk"),
                     ("ARM_Processor_System_0", "slave_hp0_axi_aclk"),
                     ("ARM_Processor_System_0", "slave_hp1_axi_aclk"),
                     ("Video_NPU_0", "M_AXI_ACLK"), ("external", "clk_150m")]:
        if endpoint not in clocks:
            raise ValueError("Integrator graph does not contain the reviewed shared clock: " + str(endpoint))
    for endpoint in [("pll_75m", "lock"), ("rstn_npu", "pll_locked"),
                     ("rstn_150m", "pll_locked")]:
        if endpoint not in locks:
            raise ValueError("Integrator graph reset lock mismatch: " + str(endpoint))
    return clocks, locks


def signal(endpoint):
    component, port = endpoint
    return port if component == "external" else "w_port__" + component + "__" + port


def validate(text, clocks, locks):
    for endpoints, source in [(clocks, "w_net__pll_75m__clk1_out"),
                              (locks, "w_net__pll_75m__lock")]:
        for endpoint in endpoints:
            if endpoint[0] == "pll_75m":
                continue
            target = signal(endpoint)
            assignments = re.findall(r"\bassign\s+" + re.escape(target) + r"\s*=\s*([^;]+);", text)
            if [a.strip() for a in assignments] != [source]:
                raise ValueError("Generated HDL clock/reset differs from graph: " + target)
    if re.search(r"\bdesign_0_(?:PLL_npu|pll_150m)_Inst\b", text):
        raise ValueError("Generated HDL still contains redundant PLL instances")
    instance = re.search(r"\bdesign_0_PLL_0\s+design_0_pll_75m_Inst\s*\((.*?)\);", text, re.S)
    if not instance or not re.search(r"\.clk1_out\(\s*w_port__pll_75m__clk1_out\s*\)", instance[1]):
        raise ValueError("Generated HDL does not connect the existing 150 MHz PLL output")


def transform(text, clocks, locks):
    # DI may have already generated the new topology. Validate every run;
    # never rely solely on an old marker being present.
    for endpoints, source in [(clocks, "w_net__pll_75m__clk1_out"),
                              (locks, "w_net__pll_75m__lock")]:
        for endpoint in endpoints:
            if endpoint[0] == "pll_75m":
                continue
            target = signal(endpoint)
            pattern = r"(\bassign\s+" + re.escape(target) + r"\s*=\s*)[^;]+;"
            text, count = re.subn(pattern, lambda m: m[1] + source + " ;", text)
            if count != 1:
                raise ValueError("Expected one generated assignment: " + target)
    for instance in ("PLL_npu", "pll_150m"):
        text = re.sub(r"(?m)^\s*design_0_PLL_[12]\s+design_0_" + instance + r"_Inst\s*\(.*?\);", "", text, flags=re.S)
        text = re.sub(r"(?m)^\s*(?:wire\s+\w*\s*|assign\s+)w_(?:port|net)__" + instance + r"__[^;]+;", "", text)
    pattern = r"(\bdesign_0_PLL_0\s+design_0_pll_75m_Inst\s*\()(.*?)(\);)"

    def connect(match):
        body, count = re.subn(r"\.clk1_out\([^)]*\)", ".clk1_out(w_port__pll_75m__clk1_out)", match[2])
        if count != 1:
            raise ValueError("Expected one PLL0 CLK1 output")
        return match[1] + body + match[3]

    text, count = re.subn(pattern, connect, text, flags=re.S)
    if count != 1:
        raise ValueError("Expected one original 75/150 MHz PLL")
    additions = []
    for name in ("w_port__pll_75m__clk1_out", "w_net__pll_75m__clk1_out"):
        if not re.search(r"\bwire\s+" + name + r"\s*;", text):
            additions.append("    wire " + name + ";")
    if not re.search(r"\bassign\s+w_net__pll_75m__clk1_out\s*=", text):
        additions.append("    assign w_net__pll_75m__clk1_out = w_port__pll_75m__clk1_out;")
    if additions:
        end = re.search(r"(?m)^\);[^\S\n]*$", text)
        if not end:
            raise ValueError("Generated module header not recognized")
        text = text[:end.end()] + "\n// SHARED_PS_PLL_TOPOLOGY: derived from Design.xml\n" + "\n".join(additions) + "\n" + text[end.end():]
    validate(text, clocks, locks)
    return text


def main(project=PROJECT):
    clocks, locks = clock_groups(project)
    path = project / "design_0/hdl/design_0.v"
    original = path.read_text(encoding="utf-8")
    result = transform(original, clocks, locks)
    if result != original:
        path.write_text(result, encoding="utf-8")
    print("CLOCK_TOPOLOGY_VERIFIED: one existing PLL, 75/150 MHz and matched reset locks")


if __name__ == "__main__":
    main()
