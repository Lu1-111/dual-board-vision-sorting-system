"""Check pin conflicts, regeneration, and actual GP0 commissioning waveforms."""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("motor", PROJECT / "src/conveyor/integrate_conveyor_motor.py")
motor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(motor)


class Integration(unittest.TestCase):
    def test_regeneration_and_pin_collision(self):
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory)
            sources = {
                "src/system_top.v": "module system_top(\n input wire clk);\n design_0 design_0_inst (.clk(clk));\nendmodule\n",
                "design_0/hdl/design_0.v": "module design_0 (\n input wire clk);\n conveyor_gp0 conveyor_gp0_inst (.clk(clk));\nendmodule\n",
                "src/constraint/io.adc": "",
            }
            for name, text in sources.items():
                path = project / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            motor.main(project)
            before = {name: (project / name).read_bytes() for name in sources}
            motor.main(project)
            self.assertEqual(before, {name: (project / name).read_bytes() for name in sources})
            self.assertIn(b".BENCH_ONLY(0), .JOG_ONLY(1)", before["design_0/hdl/design_0.v"])
            for name, pin in motor.PINS.items():
                self.assertIn(f".{name}({name})".encode(), before["src/system_top.v"])
                self.assertIn(f"LOCATION = {pin};".encode(), before["src/constraint/io.adc"])
            (project / "src/constraint/io.adc").write_text("set_pin_assignment { servo_pwm[0] } { LOCATION = Y8; }")
            with self.assertRaisesRegex(ValueError, "already belongs"):
                motor.main(project)

    def test_physical_gp0_profile(self):
        # Reuse the AXI master/legacy slave fixture, replacing its bench-only scenario.
        source = (PROJECT / "sim/conveyor_gp0_tb.sv").read_text().split("always @(negedge clk)if(rst_n")[0]
        source = source.replace("module conveyor_gp0_tb;", "module conveyor_motor_tb;")
        source = source.replace("#(.CLK_HZ(1000000)) dut", "#(.CLK_HZ(1000000),.BENCH_ONLY(0),.JOG_ONLY(1)) dut")
        source = source.replace(".clk(clk),.rst_n(rst_n),.s_awaddr(s_awaddr)",
                                ".bts_rpwm(rpwm),.bts_lpwm(lpwm),.bts_r_en(ren),.bts_l_en(len),.clk(clk),.rst_n(rst_n),.s_awaddr(s_awaddr)", 1)
        source = source.replace("conveyor_gp0 #", "wire rpwm,lpwm,ren,len;\nconveyor_gp0 #", 1)
        source += r'''
always @(negedge clk) begin
  if (rpwm && lpwm) $fatal(1,"both PWM directions active");
  if (ren !== len) $fatal(1,"enable mismatch");
  if (!rst_n && (rpwm || lpwm || ren || len)) $fatal(1,"reset must disable all pins");
end
integer edges, i;
initial begin #2500000000; $fatal(1,"timeout"); end
initial begin
  repeat(5)@(negedge clk); rst_n=1; repeat(5)@(negedge clk);
  check(!ren && !len && !rpwm && !lpwm,"idle pins low");
  rd(33'h08006002c,0,value); check(value==6,"full-duty jog-only feature advertised");
  command(33'h080060000,5,1); repeat(1100)@(negedge clk);
  edges=0; repeat(100) begin @(negedge clk); if(rpwm)edges=edges+1; check(!lpwm && ren,"forward output pins"); end
  check(edges==10,"10 percent PWM measured over two periods");
  command(33'h080060000,5,3); check(!ren && !rpwm && !lpwm,"reversal dead time");
  repeat(1100)@(negedge clk); edges=0;
  repeat(100) begin @(negedge clk); if(lpwm)edges=edges+1; check(!rpwm && ren,"reverse output pins"); end
  check(edges==10,"reverse 10 percent PWM");
  command(33'h080060000,0,0); check(!ren && !rpwm && !lpwm,"explicit stop");
  command(33'h080060000,50,1); repeat(1100)@(negedge clk);
  repeat(100) begin @(negedge clk); check(ren && rpwm && !lpwm,"100 percent PWM stays high"); end
  command(33'h080060000,0,0); check(!ren && !rpwm && !lpwm,"full-duty explicit stop");
  command(33'h080060000,51,1); rd(33'h080060014,0,value);
  check(value[10] && !ren,"duty above 100 percent rejected and disabled");
  command(33'h080060000,0,4); rd(33'h080060014,0,value); check((value & 32'h701)==0 && !ren,"clear leaves stopped");
  wr(33'h080060030,1,0,0,0,2);
  command(33'h080060000,50,1); repeat(251000)@(negedge clk);
  rd(33'h080060014,0,value); check(value[8] && !ren && !rpwm && !lpwm,"heartbeat loss disables all pins");
  command(33'h080060000,0,4);
  // Continuous valid heartbeats and a reversal must NOT extend the one-second limit.
  for(i=0;i<11;i=i+1) begin command(33'h080060000,50,(i<5?1:3)); repeat(100000)@(negedge clk); end
  rd(33'h080060014,0,value); check(value[10] && !value[8] && !ren,"one second independent hardware limit");
  command(33'h080060000,5,1); check(!ren,"fault cannot re-enable without clear");
  command(33'h080060000,0,4); command(33'h080060000,5,1); repeat(1100)@(negedge clk);
  check(ren,"explicit clear allows a new jog"); rst_n=0; #1;
  check(!ren && !len && !rpwm && !lpwm,"asynchronous reset cuts output");
  $display("PASS MOTOR PROFILE: %0d checks",checks); $finish;
end
endmodule
'''
        build = PROJECT / "sim/build-conveyor"
        build.mkdir(exist_ok=True)
        tb = build / "conveyor_motor_tb.sv"
        tb.write_text(source)
        output = build / "conveyor_motor_tb.vvp"
        tools = Path("E:/Msys2/ucrt64/bin")
        env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ["PATH"])
        subprocess.run([str(tools / "iverilog.exe"), "-g2012", "-Wall", "-s", "conveyor_motor_tb", "-o", str(output),
                        *[str(PROJECT / "src/conveyor" / n) for n in ("axi_lite_route.v", "conveyor_axi.v", "conveyor_gp0.v")], str(tb)],
                       check=True, env=env, timeout=30)
        subprocess.run([str(tools / "vvp.exe"), str(output)], check=True, env=env, timeout=60)


if __name__ == "__main__":
    unittest.main()
