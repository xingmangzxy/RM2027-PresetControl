"""Sample live runtime, then briefly halt with IWDG frozen for flash readback."""
import hashlib,json,subprocess
from pathlib import Path
from monitor import tcl_script
root=Path(__file__).resolve().parents[1]
layout=json.loads((root/'release/layout.json').read_text('utf-8'))
binary=(root/'release/CBOARD_MECANUM_FAST.bin').read_bytes()
readback=root/'evidence/flash-readback.bin'
script=tcl_script(layout).replace('while {1} {','for {set sample 0} {$sample < 10} {incr sample} {',1)
script+=f'''\nmmw 0xe0042008 0x1000 0
halt
dump_image {{{readback.as_posix()}}} 0x08000000 {len(binary)}
resume
mmw 0xe0042008 0 0x1000
shutdown
'''
path=root/'evidence/runtime-check.tcl';path.write_text(script,encoding='ascii')
result=subprocess.run(['openocd','-c','adapter driver hla','-c','hla_layout stlink','-c','hla_vid_pid 0x0483 0x3748','-c','transport select hla_swd','-f','target/stm32f4x.cfg',
    '-c','adapter speed 100','-c','gdb port disabled','-c','tcl port disabled','-c','telnet port disabled',
    '-f',str(path)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=45)
(root/'evidence/runtime-check.log').write_text(result.stdout,encoding='utf-8')
assert result.returncode==0,result.stdout
samples=[[int(x,0) for x in line[5:].strip().strip('{}').split()] for line in result.stdout.splitlines() if line.startswith('DIAG ')]
assert len(samples)>=3 and all(len(v)==256 for v in samples),result.stdout
assert samples[-1][3]>samples[0][3] and samples[-1][4]>samples[0][4], 'Runtime not advancing'
assert all(v[208] in (0,3) and not any(v[218:222]) for v in samples),'Unexpected armed state/output during controller-only verification'
assert all(v[231]==0 for v in samples),'Temperature stop flag differs from requested configuration'
assert all(v[236]==7 for v in samples),'Wrong control logic revision'
assert all(v[237]==4 for v in samples),'Fast profile not active'
assert all(v[248]==1 for v in samples),'Direct start not active'
assert readback.read_bytes()==binary,'Readback mismatch'
v=samples[-1]
report={'result':'PASS (flash readback and live diagnostic runtime)',
    'control_logic_revision':v[236],'binary_bytes':len(binary),'sha256':hashlib.sha256(binary).hexdigest(),'samples':len(samples),
    'time_first_ms':samples[0][3],'time_last_ms':v[3],'loops_first':samples[0][4],'loops_last':v[4],
    'uart_bytes':v[5],'uart_errors':v[6],'radio_frames':v[7],'bad_frames':v[8],
    'radio_online':bool(v[13]),'radio_flags':v[11],'channels_1_to_8':v[16:24],'drive_state':v[208],'fault':v[209],'reason':v[210],'currents_ID1_to_ID4':[x if x<0x80000000 else x-0x100000000 for x in v[218:222]],'software_temperature_stop_enabled':bool(v[231]),
    'CAN1_observed_mask':v[53],'CAN2_observed_mask':v[69],
    'boundary':'This verifies the flashed diagnostic runtime, not unobserved ESC IDs, physical wheel mapping or motor motion.'}
(root/'evidence/runtime-check.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
