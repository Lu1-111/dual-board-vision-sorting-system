"""Create board1 CAN wiring from the existing control-board RTL, without regeneration."""
from pathlib import Path
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'soc_prj'
DST = ROOT / 'board1_can_native'
if DST.exists():
    raise SystemExit(f'Refusing to overwrite {DST}')
DST.mkdir()
for name in ('src', 'al_ip', 'design_0'):
    shutil.copytree(SRC / name, DST / name)
(DST / 'soc_prj.al').write_text(
    (SRC / 'soc_prj.al').read_text(encoding='utf-8').replace(SRC.as_posix(), DST.as_posix()),
    encoding='utf-8')

changed = []
def patch(rel, pairs):
    p = DST / rel
    text = p.read_text(encoding='utf-8')
    for old, new in pairs:
        assert text.count(old) == 1, (rel, old, text.count(old))
        text = text.replace(old, new)
    p.write_text(text, encoding='utf-8')
    changed.append(rel)

patch('src/system_top.v', [
    ('module system_top(', 'module system_top(\n    // MLK-F3P CAN0 transceiver: PL bank 33, 3.3 V.\n    input wire CAN0_RXD,\n    output wire CAN0_TXD,'),
    ('design_0  design_0_inst (', 'design_0  design_0_inst (\n        .CAN0_RXD(CAN0_RXD),\n        .CAN0_TXD(CAN0_TXD),')])
patch('design_0/hdl/design_0.v', [
    ('module design_0 (', 'module design_0 (\n    input wire CAN0_RXD,\n    output wire CAN0_TXD,'),
    ('design_0_ARM_Processor_System_0_Inst (', 'design_0_ARM_Processor_System_0_Inst (\n        .can0_rx_in(CAN0_RXD),\n        .can0_tx_out(CAN0_TXD),')])
ip = 'design_0/ip/design_0_ARM_Processor_System_0/'
patch(ip + 'design_0_ARM_Processor_System_0.v', [
    ('  input                         slave_hp0_axi_aclk,', '  input                         can0_rx_in,\n  output                        can0_tx_out,\n  input                         slave_hp0_axi_aclk,'),
    ('      .slave_hp0_axi_aclk(slave_hp0_axi_aclk),', '      .can0_rx_in(can0_rx_in),\n      .can0_tx_out(can0_tx_out),\n      .slave_hp0_axi_aclk(slave_hp0_axi_aclk),')])
patch(ip + 'RTL/ARM_Processor_System_9c4afc9a72e9.v', [
    ('\tinput                         slave_hp0_axi_aclk,', '\tinput                         can0_rx_in,\n\toutput                        can0_tx_out,\n\tinput                         slave_hp0_axi_aclk,'),
    (".can0_rx_in(1'b0),", '.can0_rx_in(can0_rx_in),\n\t\t.can0_tx_out(can0_tx_out),\n\t\t.can0_stby(), // SN65HVD232DR has no standby input.')])
constraints = 'src/constraint/io.adc'
with (DST / constraints).open('a', encoding='utf-8') as f:
    f.write('\n# MLK-F3P hardware manual, CAN interface, PL BANK33.\n'
            'set_pin_assignment { CAN0_TXD } { LOCATION = F20; IOSTANDARD = LVCMOS33; DRIVESTRENGTH = 8; PULLTYPE = NONE; }\n'
            'set_pin_assignment { CAN0_RXD } { LOCATION = F19; IOSTANDARD = LVCMOS33; PULLTYPE = NONE; }\n')
changed.append(constraints)

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

# Compare every copied input, so unrelated video changes cannot slip in.
manifest = []
for folder in ('src', 'al_ip', 'design_0'):
    for p in (SRC / folder).rglob('*'):
        if p.is_file():
            rel = p.relative_to(SRC).as_posix()
            before, after = sha(p), sha(DST / rel)
            assert (before != after) == (rel in changed), rel
            manifest.append({'path': rel, 'baseline_sha256': before, 'candidate_sha256': after})
(DST / 'source_manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')

print(DST / 'soc_prj.al')
print('Changed HDL/constraint files:', len(changed))
