"""Read-only live SWD viewer. Does not halt, reset, flash or send motor commands."""
from pathlib import Path
import csv,json,os,queue,shutil,subprocess,sys,threading,time
import tkinter as tk
from tkinter import ttk,messagebox

ROOT=Path(__file__).resolve().parents[1]
MAGIC=0x524d4631
def signed(v):return v if v<0x80000000 else v-0x100000000
def age(now,then):return (now-then)&0xffffffff
def level(v):return {0:'无效',1:'低值挡',2:'中值挡',3:'高值挡'}.get(v,'未知')
def radio_state(v):
    if not v[12]:return '未收到有效帧：检查接收机供电、W.BUS 输出与 DBUS 接线'
    if age(v[3],v[9])>100:return '离线：遥控数据已超时（表中保留最后数值）'
    if v[11]&2:return '接收机报告 FAILSAFE 失控'
    if v[11]&1:return '接收机报告丢帧'
    if v[13]:return '接收正常'
    return '同步中：等待连续有效帧'

def tcl_script(layout):
    base=layout['base'];identity=layout['identity'];words=layout['words']
    profile_check=''
    if layout.get('ground_profile'):
        profile_check=f'''if {{[mrw {layout['profile_address']}] != 4 || [mrw {layout['revision_address']}] != 7}} {{
    echo "DIAG_ERROR Wrong_fast_firmware"
    shutdown error
}}'''
    # No 'halt', 'reset' or 'program'. OpenOCD F4 examine hook freezes IWDG;
    # undo only that debug bit so the independent watchdog continues normally.
    return f'''init
mmw 0xe0042008 0 0x1000
set ident [read_memory {identity} 32 4]
if {{[lindex $ident 0] != {MAGIC} || [lindex $ident 1] != 2 || [lindex $ident 2] != {words} || [lindex $ident 3] != 1}} {{
    echo "DIAG_ERROR Wrong_firmware_or_layout"
    shutdown error
}}
{profile_check}
while {{1}} {{
    for {{set attempt 0}} {{$attempt < 5}} {{incr attempt}} {{
        set before [mrw {base+8}]
        if {{$before & 1}} {{sleep 10; continue}}
        set values [read_memory {base} 32 {words}]
        set after [mrw {base+8}]
        if {{$before == $after && !($after & 1) && [lindex $values 0] == {MAGIC} && [lindex $values 1] == 2}} {{
            set decimals {{}}
            foreach value $values {{lappend decimals [expr {{$value + 0}}]}}
            echo "DIAG $decimals"
            break
        }}
        sleep 10
    }}
    sleep 150
}}
'''

class Viewer:
    def __init__(self,window,demo=False):
        self.window=window;self.demo=demo;self.items=queue.Queue();self.proc=None
        self.closed=False;self.values=None;self.last_host=0;self.last_tick=None;self.last_change=0
        window.title('RM2027 快速版 · 免SC启动步骤 · ID1右前 ID2左前 ID3左后 ID4右后')
        window.geometry('1160x930');window.minsize(1050,820)
        font=('Microsoft YaHei UI',10)
        ttk.Style().configure('.',font=font)
        panel=ttk.Frame(window,padding=14);panel.pack(fill='both',expand=True)
        self.status=tk.StringVar(value='正在连接 ST-Link，读取诊断固件……')
        ttk.Label(panel,textvariable=self.status,wraplength=1080,font=(*font[:1],12,'bold')).pack(anchor='w')
        ttk.Label(panel,text='快速版：SC保持中挡、摇杆居中即可遥控；SB低值停止、中值慢速、高值快速；无SC切换/KEY/测试计时。').pack(anchor='w',pady=5)
        self.stats=tk.StringVar();ttk.Label(panel,textvariable=self.stats).pack(anchor='w')
        self.axes=tk.StringVar();ttk.Label(panel,textvariable=self.axes).pack(anchor='w',pady=5)
        self.drive_status=tk.StringVar(value='等待控制状态……')
        ttk.Label(panel,textvariable=self.drive_status,wraplength=1080,font=(*font[:1],11,'bold')).pack(anchor='w',pady=5)
        self.drive_currents=tk.StringVar()
        ttk.Label(panel,textvariable=self.drive_currents,wraplength=1080).pack(anchor='w',pady=3)
        self.channels=ttk.Treeview(panel,columns=('channel','function','raw','center','position'),show='headings',height=8)
        for key,text,width in [('channel','通道',65),('function','框架用途 / 应动的操纵件',360),('raw','11 位原始值',150),('center','相对 1024',150),('position','开关电平',150)]:
            self.channels.heading(key,text=text);self.channels.column(key,width=width,anchor='center')
        uses=['右杆左右 → right_rol / 平移','左杆上下 → left_col / 预留','右杆上下 → right_col / 前后','左杆左右 → left_rol / 旋转','SB 三挡 → sLeft','SC 三挡 → sRight','CH7 / 预留','CH8 / 预留']
        for i in range(8):self.channels.insert('', 'end',iid=str(i),values=(f'CH{i+1}',uses[i],'—','—','—'))
        self.channels.pack(fill='x',pady=5)
        ttk.Label(panel,text='低值 / 中值 / 高值仅表示接收数值，请实际拨动后记录物理上、中、下对应关系。').pack(anchor='w')
        self.can_status=tk.StringVar();ttk.Label(panel,textvariable=self.can_status,wraplength=1080).pack(anchor='w',pady=8)
        self.motors=ttk.Treeview(panel,columns=('bus','id','frame','state','count','rpm','current','temp','age'),show='headings',height=10)
        for key,text,width in [('bus','总线',70),('id','电调 ID',70),('frame','反馈帧',80),('state','状态',150),('count','有效帧数',130),('rpm','转子 RPM',100),('current','电流原码',100),('temp','温度 ℃',80),('age','距上次 ms',100)]:
            self.motors.heading(key,text=text);self.motors.column(key,width=width,anchor='center')
        for b in range(2):
            for i in range(8):self.motors.insert('', 'end',iid=str(b*8+i),values=(f'CAN{b+1}',i+1,f'0x{0x201+i:03X}','未见反馈',0,'—','—','—','—'))
        self.motors.pack(fill='both',expand=True)
        actions=ttk.Frame(panel);actions.pack(fill='x',pady=8)
        ttk.Button(actions,text='导出当前记录 CSV',command=self.export).pack(side='left')
        ttk.Button(actions,text='打开操作说明',command=lambda:os.startfile(ROOT/'操作说明.md')).pack(side='left',padx=10)
        ttk.Label(panel,text='轮位：ID1右前、ID2左前、ID3左后、ID4右后。右杆上下前后、左右平移，左杆左右旋转。\n无需拨SC：SC中挡、运动轴居中即可启动；失联/反馈中断停止，恢复后回中位即可，无需拨SC。',wraplength=1080).pack(anchor='w')
        window.protocol('WM_DELETE_WINDOW',self.close)
        if not demo:threading.Thread(target=self.worker,daemon=True).start()
        window.after(100,self.poll)
    def worker(self):
        try:
            layout=json.loads((ROOT/'release/layout.json').read_text('utf-8'))
            if layout.get('words')!=256 or layout.get('magic')!=MAGIC or layout.get('version')!=2:raise RuntimeError('诊断布局文件不匹配，请重新编译')
            executable=shutil.which('openocd')
            if not executable:raise RuntimeError('未找到 OpenOCD，请通过 01_Monitor.cmd 启动')
            script=ROOT/'evidence/monitor-live.tcl';script.write_text(tcl_script(layout),encoding='ascii')
            args=[executable,'-c','adapter driver hla','-c','hla_layout stlink','-c','hla_vid_pid 0x0483 0x3748','-c','transport select hla_swd','-f','target/stm32f4x.cfg','-c','adapter speed 100',
                  '-c','gdb port disabled','-c','tcl port disabled','-c','telnet port disabled','-f',str(script)]
            proc=subprocess.Popen(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            self.proc=proc
            if self.closed:proc.terminate();return
            previous=ROOT/'evidence/monitor-latest.log'
            if previous.exists() and previous.stat().st_size:
                archive=ROOT/'evidence/monitor-sessions';archive.mkdir(exist_ok=True)
                shutil.copy2(previous,archive/(time.strftime('%Y%m%d-%H%M%S')+f'-{os.getpid()}-previous.log'))
            with previous.open('w',encoding='utf-8') as log:
                for line in proc.stdout:
                    log.write(line);log.flush()
                    if line.startswith('DIAG '):
                        values=[int(v,0) for v in line[5:].strip().strip('{}').split()]
                        if len(values)==256 and values[0]==MAGIC and values[1]==2:self.items.put(('data',values))
                    elif 'DIAG_ERROR' in line:self.items.put(('error','当前 C 板固件与诊断窗口不匹配，请先烧录本包 HEX'))
                    elif 'Error:' in line:self.items.put(('error',line.strip()))
            proc.wait()
            if not self.closed:self.items.put(('error','ST-Link 连接已结束；检查供电和接线，关闭占用探针的其他软件后重开窗口'))
        except Exception as e:self.items.put(('error',str(e)))
    def show(self,v):
        self.values=v;now=time.monotonic();self.last_host=now
        if v[3]!=self.last_tick:self.last_tick=v[3];self.last_change=now
        self.status.set(radio_state(v))
        states={0:'停止',2:'正常遥控运行',3:'通信故障锁定'}
        reasons={0:'刚上电',1:'等待遥控有效数据',2:'遥控失控/丢帧',3:'SC停止端挡；中挡使能',
            5:'启动/恢复时将运动摇杆回中位；不需要拨SC',6:'等待四个电机新鲜反馈',8:'KEY停止',10:'故障',
            11:'等待有效启动条件',14:'SB低值挡停止',17:'驱动未启用',
            18:'SB/SC不在有效挡位',19:'正常遥控；无测试时限',20:'CAN正在自动恢复，连通后摇杆回中即可继续'}
        faults={0:'无',1:'CAN通信',5:'CAN反馈协议',8:'控制循环超时',9:'总线上存在其他驱动主控'}
        wheels={1:'右前',2:'左前',3:'左后',4:'右后'}
        speed='慢速' if 800<=v[227]<=1248 else '快速' if v[227]>1400 else '停止'
        self.drive_status.set(f'控制：{states.get(v[208],v[208])} | 故障：{faults.get(v[209],v[209])} | {reasons.get(v[210],v[210])}\nSB：{speed} | 免SC启动步骤 | 快速框架版本：{v[236]}')
        current='  '.join(f'ID{i+1} {wheels[i+1]}：{signed(v[218+i])} / 目标{signed(v[222+i])}RPM' for i in range(4))
        self.drive_currents.set(current+f'\n连续运行；快速档参考{v[247]/1000:.2f}m/s、旋转{v[249]/1000:.0f}RPM；指令范围±{v[243]}；温度仅显示')
        self.stats.set(f'运行 {v[3]/1000:.1f} s  |  遥控有效帧 {v[7]}  |  串口字节 {v[5]}  |  串口错误 {v[6]}  |  坏帧 {v[8]}  |  丢帧标志 {v[11]&1} / 失控标志 {(v[11]>>1)&1}')
        self.axes.set(f'框架轴值：right_col 前后 {signed(v[32])/1000:+.3f}    right_rol 平移 {signed(v[33])/1000:+.3f}    left_rol 旋转 {signed(v[35])/1000:+.3f}')
        for i in range(8):
            previous=self.channels.item(str(i),'values');raw=v[16+i]
            self.channels.item(str(i),values=(previous[0],previous[1],raw if v[12] else '—',raw-1024 if v[12] else '—',level(v[36+i-4]) if 4<=i<=5 and v[12] else '—'))
        bus_text=[]
        for b in range(2):
            at=48+b*16
            bus_text.append(f'CAN{b+1}：总帧 {v[at]} / 电机反馈 {v[at+1]} / 非法反馈 {v[at+2]} / FIFO 溢出 {v[at+7]} / 其他帧 {v[at+9]} / ESR 0x{v[at+8]:08X}')
            for i in range(8):
                m=80+(b*8+i)*8;seen=v[m]>0
                self.motors.item(str(b*8+i),values=(f'CAN{b+1}',i+1,f'0x{0x201+i:03X}',('在线' if v[m+6] else '曾见，现无反馈') if seen else '未见反馈',v[m],signed(v[m+3]) if seen else '—',signed(v[m+4]) if seen else '—',v[m+5] if seen else '—',age(v[3],v[m+1]) if seen else '—'))
        bus_text.append(f'CAN自动恢复：{"进行中" if v[250] else "就绪"} / 事件{v[251]} / 重连尝试{v[252]} / 最近错误ESR 0x{v[253]:08X} / 原因{v[254]} / 稳定等待{v[255]}ms')
        self.can_status.set('\n'.join(bus_text))
    def stale(self):
        self.status.set('连接或固件更新已中断；当前表格是历史记录，不代表在线状态')
        self.drive_status.set('控制状态无法读取：ST-Link连接已中断，请重新连接后查看实时使能条件。')
        for key in self.motors.get_children():
            values=list(self.motors.item(key,'values'));values[3]='状态未知 / 历史记录';self.motors.item(key,values=values)
    def poll(self):
        if self.closed:return
        while True:
            try:kind,item=self.items.get_nowait()
            except queue.Empty:break
            if kind=='data':self.show(item)
            else:self.status.set('连接提示：'+item)
        now=time.monotonic()
        if self.last_host and (now-self.last_host>1.5 or now-self.last_change>1.5):self.stale()
        self.window.after(100,self.poll)
    def export(self):
        if self.values is None:messagebox.showinfo('尚无数据','请先烧录诊断固件并连接 ST-Link。');return
        path=ROOT/'evidence'/('ID核对-'+time.strftime('%Y%m%d-%H%M%S')+'.csv')
        with path.open('w',encoding='utf-8-sig',newline='') as f:
            out=csv.writer(f);out.writerow(['总线','电调ID','CAN反馈帧','状态','帧数','转子RPM','电流原码','温度℃','距上次ms','实物标签或轮位(手工填写)'])
            for key in self.motors.get_children():out.writerow([*self.motors.item(key,'values'),''])
        messagebox.showinfo('记录已导出',str(path)+'\n请在最后一列填写单独接入的实物标签。')
    def close(self):
        self.closed=True
        if self.proc and self.proc.poll() is None:self.proc.terminate()
        self.window.destroy()

def self_test():
    v=[0]*256;v[0]=MAGIC;v[1]=2;v[3]=100;v[12]=v[13]=1;v[9]=100
    v[16:32]=[1024]*16;v[36]=2;v[37]=2;v[7]=20;v[227]=v[228]=1024
    v[236]=7;v[237]=4;v[247]=1200;v[248]=1;v[249]=18000;v[243]=16384;v[208]=2;v[210]=19
    v[80]=10;v[81]=100;v[83]=0xffffff9c;v[85]=42;v[86]=1
    w=tk.Tk();w.withdraw();app=Viewer(w,demo=True);app.show(v);w.update()
    assert app.status.get()=='接收正常'
    assert '正常遥控运行' in app.drive_status.get() and '无测试时限' in app.drive_status.get()
    assert '连续运行' in app.drive_currents.get() and '16384' in app.drive_currents.get()
    assert app.channels.item('4','values')[-1]=='中值挡'
    assert app.motors.item('0','values')[5]=='-100'
    v[208]=0;v[210]=20;v[250]=1;v[251]=2;v[252]=3;v[253]=0x14860033;v[255]=150
    app.show(v)
    assert 'CAN正在自动恢复' in app.drive_status.get()
    assert '进行中' in app.can_status.get() and '重连尝试3' in app.can_status.get()
    v[250]=0;v[208]=3;v[209]=9;app.show(v)
    assert '其他驱动主控' in app.drive_status.get()
    v[11]=2;app.show(v);assert 'FAILSAFE' in app.status.get()
    v[3]=201;app.show(v);assert '超时' in app.status.get()
    app.stale();assert app.motors.item('0','values')[3]=='状态未知 / 历史记录'
    script=tcl_script({'base':0x200001c8,'identity':0x08000200,'words':256,
        'ground_profile':4,'profile_address':0x08000210,'revision_address':0x08000214})
    assert 'Wrong_fast_firmware' in script and 'reset halt' not in script and 'program ' not in script
    app.close();print('PASS fast direct-start framework viewer, signed telemetry, timeout, stale status and firmware identity checks.')

if __name__=='__main__':
    if '--self-test' in sys.argv:self_test()
    else:
        main=tk.Tk();Viewer(main);main.mainloop()
