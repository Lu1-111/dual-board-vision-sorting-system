"""Host checks: live state file freshness and malformed input must not keep heartbeat alive."""
import pathlib
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory() as directory:
    state = pathlib.Path(directory) / 'line-state.txt'
    state.write_text('1 0 1 1 95 12 8 6 3 0 0\n')
    result = subprocess.run([sys.argv[1], '--udp', '39019', '--state', str(state),
                             '--ticks', '45', '--interval-ms', '100'], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stderr
    assert 'heartbeat withheld' in result.stderr, result.stderr
    tx = int(result.stdout.split('STOP TX=')[1].split()[0])
    assert 0 < tx < 45 * 8, result.stdout
    state.write_text('1 0 1 0 95 12 8 6 3 0 0\n')  # absent item must not have confidence
    result = subprocess.run([sys.argv[1], '--udp', '39019', '--state', str(state), '--ticks', '2', '--interval-ms', '50'], capture_output=True, text=True, timeout=3)
    assert 'STOP TX=0' in result.stdout, result.stdout
print('sender freshness / invalid-state checks: PASS')
