"""Restore the board CAN0 path after TD/DI regeneration.

The SN65HVD232D on MLK-F3P is wired to PL F20/F19, not PS MIO46/47.
Use the PS hard CAN controller and its PL interface without adding a PL clock
or a second synchronizer to the controller's asynchronous receive input.
"""
from pathlib import Path
import re
import xml.etree.ElementTree as ET

PS = "design_0_ARM_Processor_System_0"
PORTS = {"can0_rx_in": "input", "can0_tx_out": "output"}


def write_if_changed(path, text):
    if path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8")


def replace_once(text, pattern, replacement):
    text, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise ValueError("CAN integration anchor missing: " + pattern)
    return text


def add_ports(text, module):
    if not re.search(r"input\s+(?:wire\s+)?can0_rx_in\b", text):
        text = replace_once(text, r"(module\s+" + module + r"\b.*?\(\s*\n)(?=\s*input|\s*output|\s*inout|\s*//)",
                            r"\1    input wire can0_rx_in,\n    output wire can0_tx_out,\n")
    return text


def add_instance_ports(text, instance):
    pattern = r"(\b" + instance + r"\s*\()(.*?)(\);)"
    match = re.search(pattern, text, flags=re.S)
    if not match:
        raise ValueError("CAN instance missing: " + instance)
    body = match[2]
    for name in PORTS:
        if not re.search(r"\." + name + r"\s*\(", body):
            body = f"\n        .{name}({name})," + body
    return text[:match.start(2)] + body + text[match.end(2):]


def component_ports(text):
    for name, direction in PORTS.items():
        if f"<ipxact:name>{name}</ipxact:name>" in text:
            continue
        entry = f"""            <ipxact:port>
                <ipxact:name>{name}</ipxact:name>
                <ipxact:displayName>{name}</ipxact:displayName>
                <ipxact:wire><ipxact:direction>{'in' if direction == 'input' else 'out'}</ipxact:direction></ipxact:wire>
            </ipxact:port>
"""
        text = text.replace("        </ipxact:ports>", entry + "        </ipxact:ports>", 1)
    ET.fromstring(text)
    return text


def graph_ports(text, instance, outer=False):
    for name, direction in PORTS.items():
        if outer and not re.search(r"<ipxact:designPort>\s*<ipxact:name>" + name + "</", text):
            text = text.replace("    </ipxact:designPorts>", f"""        <ipxact:designPort>
            <ipxact:name>{name}</ipxact:name>
            <ipxact:direction>{direction}</ipxact:direction>
        </ipxact:designPort>
    </ipxact:designPorts>""", 1)
        if f'portRef="{name}"' not in text:
            text = text.replace("    </ipxact:adHocConnections>", f"""        <ipxact:adHocConnection>
            <ipxact:name>board_{name}</ipxact:name>
            <ipxact:portReferences>
                <ipxact:internalPortReference componentRef="{instance}" portRef="{name}"/>
                <ipxact:externalPortReference portRef="{name}"/>
            </ipxact:portReferences>
        </ipxact:adHocConnection>
    </ipxact:adHocConnections>""", 1)
    ET.fromstring(text)
    return text


def main(project):
    project = Path(project)
    ip = project / "design_0/ip" / PS
    instances = [node.text for node in ET.parse(ip / "Design.xml").getroot().iter()
                 if node.tag.endswith("}instanceName") and node.text.startswith("ARM_Processor_System_")]
    if len(instances) != 1 or not instances[0].endswith("_Inst"):
        raise ValueError("Expected exactly one generated PS primitive instance")
    primitive_instance = instances[0]
    primitive_module = primitive_instance.removesuffix("_Inst")
    gui = ip / "GuiFinal.xml"
    text = gui.read_text(encoding="utf-8")
    values = {"CAN0_Dot_Enable": "1", "CAN0_Available": "1", "CAN0_Dot_IO": "PL IO",
              "CAN0_tx_Dot_IO": "PL IO", "CAN0_rx_Dot_IO": "PL IO", "CAN0_TYPE": "2.0B",
              "CAN0_psioclk_Dot_Enable": "0"}
    for key, value in values.items():
        text = replace_once(text, r'(parameterId="' + key + r'" value=")[^"]*(")',
                            lambda m: m[1] + value + m[2])
    # Enable the nominal-bit-rate settings, retaining the existing IO PLL clock.
    text = re.sub(r'(<parameter parameterId="CAN0_(?!Fast|F_|psioclk)[^"]*"[^>]*enable=")0("/>)', r'\g<1>1\2', text)
    write_if_changed(gui, text)
    params = {node.get("parameterId"): node.get("value") for node in ET.fromstring(text).iter()
              if node.get("parameterId", "").startswith("CAN0_") and node.get("enable") == "1"}
    params.update(values)
    params = {key: (value if re.fullmatch(r"[0-9]+(?:\.[0-9]+)?", value) else '"' + value + '"')
              for key, value in params.items() if value != "nan"}
    for path, instance, outer in [(project / "design_0/Design.xml", "ARM_Processor_System_0", True),
                                   (ip / "Design.xml", primitive_instance, False),
                                   (ip / "DesignTmp.xml", primitive_instance, False)]:
        text = path.read_text(encoding="utf-8")
        def configure(match):
            body = match[0]
            if f"<ipxact:instanceName>{instance}</ipxact:instanceName>" not in body:
                return body
            for key, value in params.items():
                pattern = r'(<ipxact:configurableElementValue referenceId="' + key + r'">).*?(</ipxact:configurableElementValue>)'
                if re.search(pattern, body):
                    body = replace_once(body, pattern, lambda m: m[1] + value + m[2])
                else:
                    body = body.replace("</ipxact:configurableElementValues>",
                        f'<ipxact:configurableElementValue referenceId="{key}">{value}</ipxact:configurableElementValue>\n            </ipxact:configurableElementValues>', 1)
            return body
        text, count = re.subn(r"<ipxact:componentInstance>.*?</ipxact:componentInstance>", configure, text, flags=re.S)
        if not count:
            raise ValueError("No component instances: " + str(path))
        if path.name != "DesignTmp.xml":
            text = graph_ports(text, instance, outer)
        write_if_changed(path, text)
    for path in [project / "design_0/design_0.xml", ip / (PS + ".xml")]:
        write_if_changed(path, component_ports(path.read_text(encoding="utf-8")))
    ports_file = ip / (".ports_" + PS + ".txt")
    text = ports_file.read_text(encoding="utf-8")
    for name in PORTS:
        text = replace_once(text, r"(?m)^" + name + r"; [01]", name + "; 1")
    write_if_changed(ports_file, text)
    rtl_path = ip / "RTL" / (primitive_module + ".v")
    text = rtl_path.read_text(encoding="utf-8")
    # The parameter list precedes the port list in this vendor wrapper.
    if not re.search(r"input\s+(?:wire\s+)?can0_rx_in\b", text):
        text = replace_once(text, r"(\)\s*\(\s*\n)", r"\1    input wire can0_rx_in,\n    output wire can0_tx_out,\n")
    text = replace_once(text, r"\.can0_rx_in\(\s*(?:1'b0|can0_rx_in)\s*\)", ".can0_rx_in(can0_rx_in)")
    if ".can0_tx_out(" not in text:
        text = text.replace(".can0_rx_in(can0_rx_in),", ".can0_rx_in(can0_rx_in),\n        .can0_tx_out(can0_tx_out),", 1)
    write_if_changed(rtl_path, text)
    path = ip / (PS + ".v")
    text = add_ports(path.read_text(encoding="utf-8"), PS)
    write_if_changed(path, add_instance_ports(text, primitive_instance))
    path = project / "design_0/hdl/design_0.v"
    text = add_ports(path.read_text(encoding="utf-8"), "design_0")
    write_if_changed(path, add_instance_ports(text, PS + "_Inst"))
    print("CAN0_INTEGRATED: PS CAN0 -> PL F20 TX / F19 RX, LVCMOS33; boot requires EMIOSEL_2=0")


if __name__ == "__main__":
    main(Path(__file__).resolve().parents[2])
