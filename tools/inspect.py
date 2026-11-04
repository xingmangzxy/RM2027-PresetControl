from pathlib import Path
import json,struct,subprocess,hashlib
root=Path(__file__).resolve().parents[1]
image=root/'release/CBOARD_MECANUM_FAST.hex'
memory={};upper=0;eof=False
for line in image.read_text('ascii').splitlines():
    assert line.startswith(':')
    raw=bytes.fromhex(line[1:]);assert len(raw)==raw[0]+5 and sum(raw)%256==0
    length,addr,kind=raw[0],int.from_bytes(raw[1:3],'big'),raw[3]
    if kind==0:
        for i,v in enumerate(raw[4:4+length]):
            a=upper+addr+i;assert 0x08000000<=a<0x08100000 and a not in memory
            memory[a]=v
    elif kind==4:upper=int.from_bytes(raw[4:6],'big')<<16
    elif kind==1:eof=True
assert eof
data=image.with_suffix('.bin').read_bytes()
assert data==bytes(memory.get(a,255) for a in range(0x08000000,max(memory)+1))
rows=subprocess.check_output(['arm-none-eabi-nm','-n',str(image.with_suffix('.elf'))],text=True).splitlines()
symbols={r.split()[-1]:int(r.split()[0],16) for r in rows if len(r.split())==3}
sp,reset,nmi,hard=struct.unpack_from('<4I',data)
assert sp==0x20020000 and symbols['g_pfnVectors']==0x08000000
for name,v in [('Reset_Handler',reset),('NMI_Handler',nmi),('HardFault_Handler',hard)]:
    assert v==(symbols[name]|1) and 0x08000000<=(v&~1)<0x08000000+len(data)
assert 'HAL_CAN_AddTxMessage' in symbols
assert any('CtrlHandle' in k for k in symbols) and any('ChassisHandle' in k for k in symbols)
assert any('CANPackageSend' in k for k in symbols) and '__init_array_start' in symbols
for name in ('HAL_CAN_ActivateNotification','HAL_TIM_PWM_Start','Motor::Init','ChassisStart'):
    assert name not in symbols,name
assert symbols['_ebss']<0x2001ef00
identity=symbols['g_diag_identity']
assert struct.unpack_from('<4I',data,identity-0x08000000)==(0x524d4631,2,256,1)
assert struct.unpack_from('<I',data,symbols['g_temperature_stop_enabled']-0x08000000)[0]==0
assert struct.unpack_from('<I',data,symbols['g_control_logic_revision']-0x08000000)[0]==7
assert struct.unpack_from('<I',data,symbols['g_ground_profile']-0x08000000)[0]==4
assert struct.unpack_from('<I',data,symbols['g_direct_start']-0x08000000)[0]==1
layout={'direct_start':1,'direct_start_address':symbols['g_direct_start'],'control_logic_revision':7,'ground_profile':4,'profile_address':symbols['g_ground_profile'],'revision_address':symbols['g_control_logic_revision'],'base':symbols['g_snapshot'],'identity':identity,'magic':0x524d4631,'version':2,'words':256,
        'fields':{'sequence':2,'time':3,'loops':4,'uart_bytes':5,'uart_errors':6,'rc_frames':7,
        'rc_bad':8,'last_frame':9,'last_good':10,'flags':11,'seen':12,'online':13,'streak':14,
        'channels':16,'right_col':32,'right_rol':33,'left_col':34,'left_rol':35,'SB':36,'SC':37,
        'buses':48,'bus_stride':16,'motors':80,'motor_stride':8,'drive':208,'temperature_stop_enabled':231},
        'binary_sha256':hashlib.sha256(data).hexdigest()}
(root/'release/layout.json').write_text(json.dumps(layout,indent=2)+'\n',encoding='utf-8')
report={'result':'PASS (static inspection, no hardware test)','bytes':len(data),'initial_sp':hex(sp),
        'reset_vector':hex(reset),'software_temperature_stop_enabled':False,'fast_direct_start_profile':True,'direct_start':True,'control_logic_revision':7,'drive_CAN1_0x200':True,'no_actuator_PWM':True,'snapshot_address':hex(symbols['g_snapshot'])}
(root/'evidence/image-checks.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
