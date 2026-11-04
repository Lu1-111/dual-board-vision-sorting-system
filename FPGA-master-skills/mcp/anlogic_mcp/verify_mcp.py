"""Portable read-only stdio handshake; does not build, flash, or cancel jobs."""
import argparse
import asyncio
import json
import os
from pathlib import Path
import sys
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

async def verify(args):
    summary = {}
    shared = {'environment_info', 'list_projects', 'project_info', 'list_reports',
              'read_text_file', 'list_jobs', 'job_status', 'read_job_log', 'cancel_job'}
    for kind in ('td', 'fd'):
        env = dict(os.environ)
        env.update(ANLOGIC_WORKSPACE=str(Path(args.workspace).resolve()),
                   ANLOGIC_TD_HOME=str(Path(args.td_home).resolve()),
                   ANLOGIC_FD_HOME=str(Path(args.fd_home).resolve()))
        params = StdioServerParameters(command=sys.executable,
            args=[str(Path(__file__).with_name('server.py')), '--tool', kind], env=env)
        async with stdio_client(params) as (reader, writer):
            async with ClientSession(reader, writer) as session:
                await session.initialize()
                tools = await session.list_tools()
                names = {tool.name for tool in tools.tools}
                expected = shared | {'build_project', 'run_tcl' if kind == 'td' else 'toolchain_version'}
                if names != expected:
                    raise RuntimeError(f'{kind}: tool set differs: {names ^ expected}')
                result = await session.call_tool('environment_info', {})
                if result.isError:
                    raise RuntimeError(f'{kind}: environment_info failed')
                data = json.loads(next(c.text for c in result.content if c.type == 'text'))
                summary[kind] = {'tools': sorted(names), 'environment': data}
    print(json.dumps(summary, ensure_ascii=False, indent=2))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', required=True)
    parser.add_argument('--td-home', required=True)
    parser.add_argument('--fd-home', required=True)
    asyncio.run(verify(parser.parse_args()))
