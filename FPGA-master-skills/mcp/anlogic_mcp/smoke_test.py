"""Real MCP handshake/tool calls plus actual TD Tcl and FD GCC/Make execution."""
import asyncio
from contextlib import asynccontextmanager
import json
from pathlib import Path
import sys
import time
import uuid

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

HERE = Path(__file__).resolve().parent


@asynccontextmanager
async def connect(kind):
    parameters = StdioServerParameters(command=sys.executable,
        args=[str(HERE / 'server.py'), '--tool', kind])
    async with stdio_client(parameters) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            yield session


async def call(session, name, args=None):
    result = await session.call_tool(name, args or {})
    if result.isError:
        raise AssertionError(f'{name}: {result.content}')
    return json.loads(next(c.text for c in result.content if c.type == 'text'))


async def wait(session, job):
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        result = await call(session, 'job_status', {'job_id': job['job_id']})
        if result['status'] not in ('starting', 'running'):
            log = await call(session, 'read_job_log', {'job_id': job['job_id'], 'offset': -1})
            return result, log['text']
        await asyncio.sleep(0.25)
    await call(session, 'cancel_job', {'job_id': job['job_id']})
    raise AssertionError('Smoke test timed out')


async def main():
    scratch = HERE / '.state' / ('smoke-' + uuid.uuid4().hex)
    scratch.mkdir(parents=True)
    summary = {}
    async with connect('td') as td:
        tools = await td.list_tools()
        assert len(tools.tools) == 11
        info = await call(td, 'project_info', {'project': 'soc_prj/soc_prj.al'})
        assert info['device'] == 'DR1M90GEG400'
        await call(td, 'list_projects')
        good = scratch / 'good.tcl'
        good.write_text('puts "MCP_TD_PROBE_OK"\nexit 0\n')
        job = await call(td, 'run_tcl', {'script': str(good), 'working_directory': str(scratch)})
        result, log = await wait(td, job)
        assert result['status'] == 'succeeded', (result, log)
        assert 'MCP_TD_PROBE_OK' in log
        bad = scratch / 'bad.tcl'
        bad.write_text('error "intentional-smoke-error"\n')
        job = await call(td, 'run_tcl', {'script': str(bad), 'working_directory': str(scratch)})
        result, log = await wait(td, job)
        assert result['status'] == 'failed', (result, log)
        summary['td'] = dict(tools=len(tools.tools), device=info['device'], success_and_failure_probes='passed')
    async with connect('fd') as fd:
        tools = await fd.list_tools()
        assert len(tools.tools) == 11
        version = await call(fd, 'toolchain_version')
        assert version['returncode'] == 0
        info = await call(fd, 'project_info', {'project': 'soc_sdk/soc_fsbl'})
        assert Path(info['inferred_sdk_root']).name == 'soc_base'
        (scratch / 'probe.c').write_text('int mcp_probe(void) { return 42; }\n')
        (scratch / 'Makefile').write_text('all:\n\t$(COMPILE_PREFIX)gcc -c probe.c -o probe.o\n')
        job = await call(fd, 'build_project', {'project': str(scratch)})
        job_id = job['job_id']
    # Explicit reconnect: detached worker status and logs must survive.
    async with connect('fd') as fd:
        result, log = await wait(fd, {'job_id': job_id})
        assert result['status'] == 'succeeded', (result, log)
        assert (scratch / 'probe.o').read_bytes()[:4] == b'\x7fELF'
        summary['fd'] = dict(tools=len(tools.tools), compiler=version['output'].splitlines()[0],
                             actual_c_compile='passed', reconnect='passed')
    (HERE / '.state' / 'smoke-result.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    asyncio.run(main())
