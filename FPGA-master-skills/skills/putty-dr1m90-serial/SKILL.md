---
name: putty-dr1m90-serial
description: Operate the MLK F3P-CZ02 DR1M90 Linux serial console through PuTTY or Windows serial commands. Discover the live CH342 COM port dynamically; use for this board's UART console, not FPGA JTAG or SSH.
---

# DR1M90 串口控制

The target board uses a CH342K dual USB-to-UART chip. The verified PS Linux console is CH342 channel A (`USB\VID_1A86&PID_55D2&MI_00`); on the board it is PS UART1 on MIO48/49, exposed as Linux `ttyS0`. Channel B is another UART. Use 115200 baud, 8 data bits, no parity, 1 stop bit, and no flow control. Windows COM numbers change; COM9 was only one observed assignment.

## Find and open the console

Run `scripts/dr1-serial.ps1` without arguments to list *present* CH342 ports. It identifies the console by the USB interface ID, not by a stored COM number. If more than one CH342-A is present, distinguish the physical board before choosing `-Port`; do not guess. Use `-OpenPuTTY` to launch an interactive PuTTY window on the discovered channel A, or `-Port COMx -OpenPuTTY` after verifying the port. The helper uses the installed PuTTY executable and `-serial ... -sercfg 115200,8,n,1,N`.

If a PuTTY window already owns the port, interact with that window using available computer-use controls or close it before using the helper's `-Command` mode. Windows serial ports are exclusive. For short automated commands, `scripts/dr1-serial.ps1 -Command 'id'` opens the dynamically discovered console, requires an existing shell prompt, sends one command, returns its output, and closes the port. Increase `-WaitMs` for slower commands. Use normal task judgment before commands that alter the board.

On opening a fresh console, send Enter once. If a `login:` prompt appears, the currently supplied development credentials are user `root`, password `root`; do not put the password in PuTTY command-line arguments or logs. If a `#` prompt appears, verify with `id`. If credentials have changed, ask the user rather than retrying guesses.

## Board checks

- `cat /proc/cmdline` and `mount | grep ' on / '` show which system actually booted. The SD card prepared on 2026-10-10 used an ext4 rootfs with `root=PARTUUID=1a366705-02`; treat that ID as a historical check, not a fixed future value.
- `ls -l /dev/fb0` checks the Linux framebuffer. `pidof hdmi_ui` checks the Qt task manager. Start it with `cd /home/uisrc/qt_play && ./run_hdmi_ui.sh`; that script sets its Qt libraries and Linux framebuffer settings. Do not assume `./hdmi_ui` works without those environment variables.
- This is the PS Linux serial console. The FPGA PL configuration is loaded from the boot image's bitstream; sending shell commands alone does not reflash PL.

If the port disappears, compare `Get-PnpDevice -Class Ports -PresentOnly` with the Ubuntu guest's USB inventory if VMware passthrough is involved. Historical Windows COM entries with non-present status are not live ports. The external CH342 USB device must enumerate before changing Linux `ttyS0` or boot parameters can help a missing Windows COM port.
