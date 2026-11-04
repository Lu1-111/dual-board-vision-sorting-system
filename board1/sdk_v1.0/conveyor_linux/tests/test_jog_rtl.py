"""Replay the real CLI/driver's register traffic through GP0 at 75 MHz.

The test binary links a file-only backend, never the Linux UIO backend.
Successful command traces are verified by the real RTL, not the backend model.
Fault injection checks that plausible enable status cannot hide wrong duty.
"""
from pathlib import Path
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SOFTWARE = ROOT / "sdk_v1.0/conveyor_linux"
PROJECT = ROOT / "prj_v1.0/soc_prj"
BUILD = SOFTWARE / "build/jog-rtl"
TOOLS = Path("E:/Msys2/ucrt64/bin")
ENV = dict(os.environ, PATH=str(TOOLS) + os.pathsep + os.environ["PATH"])
BUILD.mkdir(parents=True, exist_ok=True)


def run(args, **kwargs):
    result = subprocess.run([str(x) for x in args], cwd=SOFTWARE, env=ENV,
                            capture_output=True, text=True, timeout=180, **kwargs)
    if result.returncode:
        raise RuntimeError(f"exit={result.returncode}: {args}\n" + result.stdout + result.stderr)
    return result


exe = BUILD / "jog-trace.exe"
run(["E:/MinGW/mingw64/bin/g++.exe", "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror", "-static", "-I.",
     "conveyor_jog.cpp", "conveyor.cpp", "tests/jog_trace_backend.cpp", "-pthread", "-o", exe])

source = (PROJECT / "sim/conveyor_gp0_tb.sv").read_text()
marker = "always @(negedge clk)"
assert marker in source, "GP0 fixture monitor marker changed; update replay extraction"
fixture = source.split(marker, 1)[0]
fixture = fixture.replace("module conveyor_gp0_tb;", "module jog_replay_tb;")
fixture = fixture.replace("always #500 clk=~clk;", "always #6.666667 clk=~clk;")
fixture, replacements = re.subn(r"conveyor_gp0 #\([^\n]+?\) dut",
    "conveyor_gp0 #(.CLK_HZ(75000000),.BENCH_ONLY(0),.JOG_ONLY(1)) dut", fixture)
assert replacements == 1, "expected exactly one GP0 instance"
fixture = fixture.replace("conveyor_gp0 #", "wire rpwm,lpwm,ren,len;\nconveyor_gp0 #", 1)
fixture = fixture.replace(".clk(clk),.rst_n(rst_n),.s_awaddr(s_awaddr)",
                          ".bts_rpwm(rpwm),.bts_lpwm(lpwm),.bts_r_en(ren),.bts_l_en(len),.clk(clk),.rst_n(rst_n),.s_awaddr(s_awaddr)", 1)
fixture += r'''
integer fd,got,op,a,b,ticks,reverse_mode,cycles=0,origin;
integer previous_rise=-1,periods=0,widths=0,high_ticks=0,full_samples=0;
reg previous_pwm=0,previous_en=0;
reg [2047:0] trace_file,wave_file;
wire selected_pwm=reverse_mode?lpwm:rpwm;
always @(posedge clk) cycles<=cycles+1;
always @(negedge clk) begin
  if(rpwm && lpwm) $fatal(1,"both PWM directions active");
  if(ren !== len) $fatal(1,"R_EN/L_EN mismatch");
  if((!rst_n || !ren) && (rpwm || lpwm)) $fatal(1,"PWM active while disabled");
  if(ren && (reverse_mode?rpwm:lpwm)) $fatal(1,"wrong PWM direction");
  if(ren && previous_en && ticks==3750) begin
    if(!selected_pwm) $fatal(1,"100 percent output must stay high while enabled");
    full_samples=full_samples+1;
  end
  if(ren && selected_pwm && !previous_pwm) begin
    if(previous_rise>=0) begin
      if(cycles-previous_rise!=3750) $fatal(1,"PWM period is not 3750 ticks");
      periods=periods+1;
    end
    previous_rise=cycles;
    high_ticks=0;
  end
  if(selected_pwm) high_ticks=high_ticks+1;
  if(ren && previous_en && !selected_pwm && previous_pwm && periods>0) begin
    if(high_ticks!=ticks) $fatal(1,"PWM high ticks %0d != %0d",high_ticks,ticks);
    widths=widths+1;
  end
  if(!ren) previous_rise=-1;
  previous_en=ren;previous_pwm=selected_pwm;
end
initial begin #100000000; $fatal(1,"simulation timeout"); end
initial begin
  if(!$value$plusargs("TRACE=%s",trace_file) || !$value$plusargs("WAVE=%s",wave_file)
      || !$value$plusargs("TICKS=%d",ticks) || !$value$plusargs("REVERSE=%d",reverse_mode))
    $fatal(1,"missing replay arguments");
  $dumpfile(wave_file); $dumpvars(0,rpwm,lpwm,ren,len);
  fd=$fopen(trace_file,"r");if(!fd)$fatal(1,"trace not found");
  repeat(5)@(negedge clk);rst_n=1;repeat(5)@(negedge clk);origin=cycles;
  while(!$feof(fd)) begin
    got=$fscanf(fd,"%c %d %d\n",op,a,b);
    if(got==3) begin
      case(op)
        "T": while(cycles-origin<a*75000) @(negedge clk);
        "W": wr(33'h080060000+a,b,0,0,0,0);
        "R": begin
          rd(33'h080060000+a,0,value);
          if(value!==b) $fatal(1,"register offset %0d: RTL=%h expected=%h",a,value,b);
        end
        default: $fatal(1,"bad trace opcode %0d",op);
      endcase
    end else if(got!=-1) $fatal(1,"malformed trace");
  end
  $fclose(fd);
  repeat(10)@(negedge clk);
  if(ren || len || rpwm || lpwm)$fatal(1,"outputs not stopped");
  if(ticks==3750) begin
    if(full_samples<37500)$fatal(1,"insufficient full-duty output samples");
  end else if(periods<10 || widths<10)$fatal(1,"insufficient physical output samples");
  $display("PASS CLI->GP0->BTS: duty_ticks=%0d period=3750 direction=%0d periods=%0d widths=%0d checks=%0d",ticks,reverse_mode,periods,widths,checks);
  $finish;
end
endmodule
'''
tb = BUILD / "jog_replay_tb.sv"
tb.write_text(fixture)
vvp = BUILD / "jog_replay.vvp"
run([TOOLS / "iverilog.exe", "-g2012", "-s", "jog_replay_tb", "-o", vvp,
     *[PROJECT / "src/conveyor" / name for name in ("axi_lite_route.v", "conveyor_axi.v", "conveyor_gp0.v")], tb])
results = []
for percent, direction in ((1, "forward"), (20, "forward"), (25, "forward"), (50, "forward"),
                           (75, "reverse"), (99, "forward"), (100, "forward"), (100, "reverse")):
    trace = BUILD / f"{percent}-{direction}.trace"
    ENV["CONVEYOR_TEST_TRACE"] = str(trace)
    cli = run([exe, "--uio", "/dev/uio0", "--jog", percent, 30, direction])
    assert "REGISTER_READBACK" in cli.stdout and "STOP_CONFIRMED" in cli.stdout, cli
    ticks = (percent * 3750 + 50) // 100
    sim = run([TOOLS / "vvp.exe", vvp, f"+TRACE={trace.as_posix()}",
               f"+WAVE={(BUILD / (trace.stem + '.vcd')).as_posix()}", f"+TICKS={ticks}",
               f"+REVERSE={int(direction == 'reverse')}"])
    (BUILD / (trace.stem + ".log")).write_text(cli.stdout + sim.stdout)
    results.append({"percent": percent, "direction": direction, "result": sim.stdout.strip()})
    print(sim.stdout.strip())
for mode, message in (("wrong-duty", "active duty readback mismatch"),
                      ("wrong-direction", "motor enable/direction readback mismatch"),
                      ("dropped-apply", "motor enable/direction readback mismatch")):
    ENV["CONVEYOR_TEST_FAILURE"] = mode
    trace = BUILD / f"failure-{mode}.trace"
    ENV["CONVEYOR_TEST_TRACE"] = str(trace)
    failed = subprocess.run([str(exe), "--uio", "/dev/uio0", "--jog", "25", "30", "forward"],
                            env=ENV, capture_output=True, text=True, timeout=5)
    assert failed.returncode != 0 and message in failed.stderr, failed
    # Destructor must issue a zero-duty/disabled commit after the rejection.
    assert "W 8 0\nW 12 0\nW 16 3235774465\n" in trace.read_text().split("W 8 938", 1)[1]
    results.append({"injected_failure": mode, "rejected": True, "stop_written": True})
    print("PASS failure detection and stop:", mode)
ENV["CONVEYOR_TEST_FAILURE"] = "legacy-pl"
trace = BUILD / "legacy-pl.trace"
ENV["CONVEYOR_TEST_TRACE"] = str(trace)
failed = subprocess.run([str(exe), "--uio", "/dev/uio0", "--jog", "50", "30", "forward"],
                        env=ENV, capture_output=True, text=True, timeout=5)
assert failed.returncode != 0 and "legacy PL limits" in failed.stderr, failed
assert "W " not in trace.read_text(), "Legacy rejection must precede register writes"
results.append({"legacy_pl_above_25_rejected_before_writes": True})
print("PASS legacy PL: expanded duty rejected before writes")
(BUILD / "results.json").write_text(json.dumps(results, indent=2))
