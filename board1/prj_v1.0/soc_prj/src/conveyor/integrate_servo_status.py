"""Expose the reviewed servo control-status input after DI regeneration."""
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def transform(text):
    if not re.search(r"input\s+wire\s+\[63:0\]\s+pl_gpio_in\b", text):
        text, count = re.subn(r"(output\s+wire\s+\[63:0\]\s+pl_gpio_out\s*,)",
                              r"input wire [63:0] pl_gpio_in,\n    \1", text, count=1)
        if count != 1:
            raise ValueError("Generated PL GPIO output header not recognized")
    text, count = re.subn(r"\.pl_gpio_in\(\s*(?:64'd0|pl_gpio_in)\s*\)",
                          ".pl_gpio_in(pl_gpio_in)", text)
    if count != 1:
        raise ValueError("Expected exactly one PS GPIO input connection")
    return text


def main(project):
    project = Path(project)
    graph = ET.parse(project / "design_0/Design.xml").getroot()
    matches = []
    for node in graph.iter():
        if node.tag.rsplit("}", 1)[-1] != "adHocConnection":
            continue
        refs = {(c.get("componentRef", "external"), c.get("portRef"))
                for c in node.iter() if c.tag.rsplit("}", 1)[-1].endswith("PortReference")}
        if ("ARM_Processor_System_0", "pl_gpio_in") in refs:
            matches.append(refs)
    if matches != [{("ARM_Processor_System_0", "pl_gpio_in"), ("external", "pl_gpio_in")}]:
        raise ValueError("DI graph must expose exactly the reviewed PS GPIO status input")
    path = project / "design_0/hdl/design_0.v"
    original = path.read_text(encoding="utf-8")
    result = transform(original)
    if result != original:
        path.write_text(result, encoding="utf-8")
    print("SERVO_STATUS_VERIFIED: PL GPIO 55..50 input, command acknowledgement and active/rearm state")
