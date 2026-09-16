#!/usr/bin/env python3
"""Fresh-process CLI lifecycle and independent Pasmo byte verification."""
import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--analyze', required=True)
p.add_argument('--disassemble', required=True)
p.add_argument('--pasmo', required=True)
a = p.parse_args()
if not shutil.which(a.pasmo):
    print('SKIP: Pasmo 0.5.5 is required for independent semantic export verification')
    sys.exit(77)

def run(*args, ok=True):
    result = subprocess.run([str(x) for x in args], capture_output=True, text=True, timeout=30)
    if (result.returncode == 0) != ok:
        raise AssertionError(f'{args}\n{result.stdout}\n{result.stderr}')
    return result.stdout

with tempfile.TemporaryDirectory(prefix='z80-analysis-workflow-') as temp:
    root = Path(temp)
    binary = root / 'fixture.bin'
    data = bytes.fromhex('21 00 90 3a 00 90 cd 10 80 c3 19 80 18 02 ed 4c c9 41 42 43 00 01 90 10 80 dd 00 21 00 90')
    binary.write_bytes(data)
    project = root / 'fixture.z80analysis'
    common = ['--image', binary, '--org', '0x8000', '--project', project]
    def analyze(command, *args, ok=True):
        return run(a.analyze, command, *common, *args, ok=ok)
    analyze('new')
    def create(name, address, kind='LABEL', extent=None):
        args = ['--name', name, '--address', hex(address), '--kind', kind]
        if extent is not None:
            args += ['--extent', str(extent)]
        return analyze('create', *args).strip()
    entry = create('ENTRY', 0x8000, 'FUNCTION', 17)
    routine = create('ROUTINE', 0x8010, 'FUNCTION', 1)
    state = create('STATE', 0x9000, 'WORD_VARIABLE', 2)
    selected_reference = analyze('reference', '--offset', '6', '--relation', 'branch', '--symbol', routine).strip()
    create('MESSAGE', 0x8011, 'DATA_REGION', 3)
    create('TERMINATOR', 0x8014, 'DATA_REGION', 1)
    create('WORDS', 0x801b, 'DATA_REGION', 3)
    create('POINTERS', 0x8015, 'DATA_REGION', 4)
    create('TAIL', 0x8019)
    create('INTERIOR', 0x801a)
    analyze('create', '--name', 'MAGIC', '--value', '0x9000')
    for name, encoding in [('MESSAGE', 'text'), ('POINTERS', 'pointers'), ('TERMINATOR', 'bytes'), ('WORDS', 'words')]:
        analyze('set', '--symbol', name, '--field', 'encoding', '--value', encoding)
    analyze('reference', '--offset', '21', '--relation', 'pointer', '--symbol', state, '--addend', '1')
    analyze('set', '--symbol', entry, '--field', 'summary', '--value', 'Loads STATE\nNames are interpretations, bytes are verified.')
    analyze('rename', '--symbol', entry, '--name', 'START')
    snapshot = project.read_bytes()
    analyze('rename', '--symbol', 'START', '--name', 'START')
    assert project.read_bytes() == snapshot, 'no-op changes project revision'
    analyze('rename', '--symbol', 'START', '--name', 'STATE', ok=False)
    assert project.read_bytes() == snapshot, 'collision changes project'
    reopened = json.loads(analyze('show'))
    record = next(x for x in reopened['project']['symbols'] if x['id'] == entry)
    assert 'ENTRY' in record['aliases'] and record['fields']['summary']['value'].startswith('Loads STATE')
    # Every invocation above reopens from disk: this exercises restart identity.
    def export(stem, *extra, ok=True):
        asm, source_map, manifest = [root / (stem + suffix) for suffix in ('.asm', '.map.json', '.manifest.json')]
        run(a.disassemble, '--org', '0x8000', binary, '--analysis', project,
            '--output', asm, '--map', source_map, '--manifest', manifest, *extra, ok=ok)
        return asm, source_map, manifest
    asm, source_map, manifest = export('first', '--aliases')
    text = asm.read_text()
    assert 'LD HL, $9000' in text and 'LD HL, MAGIC' not in text, 'immediate wrongly classified as address'
    assert 'LD A, (STATE)' in text and 'CALL ROUTINE' in text and 'JR ROUTINE' in text
    assert 'INTERIOR equ $801a' in text and 'ENTRY equ START' in text
    assert 'defw STATE+1' in text and 'defw ROUTINE' in text
    assert 'defm "ABC"' in text and 'defw $0021' in text
    assert '; Names are interpretations' in text, 'multiline comment escaped incorrectly'
    mapping = json.loads(source_map.read_text())
    spans = [x for x in mapping['lines'] if x['byte_length']]
    assert sum(x['byte_length'] for x in spans) == len(data)
    assert [x['image_offset'] for x in spans] == [sum(y['byte_length'] for y in spans[:i]) for i in range(len(spans))]
    uses = [u for line in mapping['lines'] for u in line['references']]
    assert any(u['symbol'] == state for u in uses) and any(u['symbol'] == routine for u in uses)
    assert any(u['reference'] == selected_reference for u in uses)
    m = json.loads(manifest.read_text())
    assert m['assembly_sha256'] == hashlib.sha256(asm.read_bytes()).hexdigest()
    assert m['source_map_sha256'] == hashlib.sha256(source_map.read_bytes()).hexdigest()
    second = export('second', '--aliases')
    assert all(x.read_bytes() == y.read_bytes() for x, y in zip((asm, source_map, manifest), second)), 'nondeterministic export'
    pasmo = Path(a.pasmo)
    if not pasmo.is_file():
        raise AssertionError('Pasmo must be supplied for semantic acceptance')
    run(pasmo, '--bin', asm, root / 'roundtrip.bin')
    assert (root / 'roundtrip.bin').read_bytes() == data
    partial, _, _ = export('partial', '--offset', '2', '--length', '22')
    run(pasmo, '--bin', partial, root / 'partial.bin')
    assert (root / 'partial.bin').read_bytes() == data[2:24], 'range cut changed bytes'
    analyze('rename', '--symbol', routine, '--name', 'SUBROUTINE')
    renamed, renamed_map, _ = export('renamed')
    assert 'CALL SUBROUTINE' in renamed.read_text() and 'defw SUBROUTINE' in renamed.read_text()
    run(pasmo, '--bin', renamed, root / 'renamed.bin')
    assert (root / 'renamed.bin').read_bytes() == data
    create('OVERLAP', 0x8012, 'DATA_REGION', 2)
    analyze('set', '--symbol', 'OVERLAP', '--field', 'encoding', '--value', 'bytes')
    paths = export('overlap', ok=False)
    assert not any(path.exists() for path in paths), 'failed validation published output'
    analyze('retire', '--symbol', 'OVERLAP')
    create('start', 0x9200)
    export('case_collision', ok=False)
    analyze('retire', '--symbol', 'start')
    create('LD', 0x9300)
    export('reserved', ok=False)
    analyze('set', '--symbol', 'LD', '--field', 'export_name', '--value', 'LOAD_ROUTINE')
    valid, _, _ = export('spelling')
    run(pasmo, '--bin', valid, root / 'spelling.bin')
    assert (root / 'spelling.bin').read_bytes() == data
    # RST operands are evaluated early by Pasmo: forward target labels need equates.
    rst_binary = root / 'rst.bin'
    rst_binary.write_bytes(bytes([0xff]) + bytes(55) + bytes([0xc9]))
    rst_project = root / 'rst.z80analysis'
    rst_common = ['--image', rst_binary, '--org', '0', '--project', rst_project]
    run(a.analyze, 'new', *rst_common)
    run(a.analyze, 'create', *rst_common, '--name', 'RST_38_IM1', '--address', '56')
    rst_asm = root / 'rst.asm'
    run(a.disassemble, '--org', '0', rst_binary, '--analysis', rst_project,
        '--output', rst_asm, '--map', root / 'rst.map.json', '--manifest', root / 'rst.manifest.json')
    run(a.pasmo, '--bin', rst_asm, root / 'rst.roundtrip.bin')
    assert (root / 'rst.roundtrip.bin').read_bytes() == rst_binary.read_bytes()
    # Reject mismatched image before applying annotations or publishing artifacts.
    binary.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
    analyze('show', ok=False)
    export('wrong_image', ok=False)
# The semantic renderer must retain the same exceptional-encoding contract as
# the byte-only exporter across every existing opcode/prefix/range fixture.
from assembly_roundtrip import binary_cases
with tempfile.TemporaryDirectory(prefix='z80-semantic-opcodes-') as temp:
    root = Path(temp)
    for name, origin, data in binary_cases():
        binary = root / (name + '.bin')
        binary.write_bytes(data)
        project = root / (name + '.z80analysis')
        run(a.analyze, 'new', '--image', binary, '--org', str(origin), '--project', project)
        asm = root / (name + '.asm')
        run(a.disassemble, '--org', str(origin), binary, '--analysis', project,
            '--output', asm, '--map', root / (name + '.map.json'), '--manifest', root / (name + '.manifest.json'))
        output = root / (name + '.out.bin')
        run(a.pasmo, '--bin', asm, output)
        assert output.read_bytes() == data, name
print('Semantic workflow: identities, restart, aliases, data, maps, deterministic export and Pasmo bytes passed')
