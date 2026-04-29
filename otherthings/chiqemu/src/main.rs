use std::{fs, io, path::Path, path::PathBuf, process::Command, time::Duration};

use crossterm::{
    event::{self, Event, KeyCode, KeyModifiers},
    execute,
    terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen},
};
use ratatui::{
    backend::CrosstermBackend,
    layout::{Alignment, Constraint, Layout, Rect},
    style::{Color, Modifier, Style},
    text::{Line, Span},
    widgets::{Block, Borders, Cell, Clear, Paragraph, Row, Table, TableState},
    Frame, Terminal,
};
use serde::{Deserialize, Serialize};

// ── Colors ────────────────────────────────────────────────────────────────────

const C_BG:     Color = Color::Rgb(30,  18,  36);
const C_BORDER: Color = Color::Rgb(61,  43,  69);
const C_FG:     Color = Color::Rgb(240, 184, 208);
const C_ACCENT: Color = Color::Rgb(201, 71,  155);
const C_CURSOR: Color = Color::Rgb(255, 121, 198);
const C_DIM:    Color = Color::Rgb(100, 70,  110);
const C_GREEN:  Color = Color::Rgb(80,  220, 130);
const C_RED:    Color = Color::Rgb(220, 80,  80);
const C_YELLOW: Color = Color::Rgb(240, 200, 80);

fn s_base() -> Style { Style::default().fg(C_FG).bg(C_BG) }
fn s_dim()  -> Style { Style::default().fg(C_DIM).bg(C_BG) }
fn s_sel()  -> Style { Style::default().fg(C_BG).bg(C_ACCENT).add_modifier(Modifier::BOLD) }
fn s_run()  -> Style { Style::default().fg(C_GREEN).bg(C_BG) }
fn s_err()  -> Style { Style::default().fg(C_RED).bg(C_BG) }
fn s_bord() -> Style { Style::default().fg(C_BORDER).bg(C_BG) }
fn s_head() -> Style { Style::default().fg(C_CURSOR).add_modifier(Modifier::BOLD) }

// ── VM ────────────────────────────────────────────────────────────────────────

#[derive(Serialize, Deserialize, Clone)]
struct Vm {
    name:    String,
    arch:    String,
    ram:     u32,
    cores:   u32,
    #[serde(default)]
    disk:    String,
    #[serde(default)]
    hdb:     String,
    #[serde(default)]
    hdc:     String,
    #[serde(default)]
    cdrom:   String,
    #[serde(default)]
    fda:     String,
    #[serde(default)]
    fdb:     String,
    #[serde(default)]
    boot:    String,   // dc / cd / d / c / ""
    display: String,
    #[serde(default)]
    vga:     String,   // std / cirrus / virtio / vmware / ""
    #[serde(default = "yes")]
    net:     bool,
    #[serde(default)]
    net_model: String, // virtio / e1000 / rtl8139 / ""
    #[serde(default = "yes")]
    kvm:     bool,
    #[serde(default)]
    extra:   String,
}

fn yes() -> bool { true }

impl Vm {
    fn pid_path(&self) -> PathBuf {
        PathBuf::from(format!("/tmp/chiqemu-{}.pid", self.name))
    }

    fn is_running(&self) -> bool {
        let Ok(s) = fs::read_to_string(self.pid_path()) else { return false };
        let Ok(pid) = s.trim().parse::<u32>() else { return false };
        Path::new(&format!("/proc/{}", pid)).exists()
    }

    fn get_pid(&self) -> Option<u32> {
        fs::read_to_string(self.pid_path()).ok()?.trim().parse().ok()
    }

    fn qemu_bin(&self) -> String { format!("qemu-system-{}", self.arch) }

    fn shell_cmd(&self) -> String {
        let mut p: Vec<String> = vec![sh_q(&self.qemu_bin())];
        p.push(format!("-m {}", self.ram));
        p.push(format!("-smp {}", self.cores));
        if self.kvm && matches!(self.arch.as_str(), "x86_64" | "i386") {
            p.push("-enable-kvm".into());
        }
        if !self.disk.is_empty()  { p.push(format!("-hda {}", sh_q(&self.disk))); }
        if !self.hdb.is_empty()   { p.push(format!("-hdb {}", sh_q(&self.hdb))); }
        if !self.hdc.is_empty()   { p.push(format!("-hdc {}", sh_q(&self.hdc))); }
        if !self.cdrom.is_empty() { p.push(format!("-cdrom {}", sh_q(&self.cdrom))); }
        if !self.fda.is_empty()   { p.push(format!("-fda {}", sh_q(&self.fda))); }
        if !self.fdb.is_empty()   { p.push(format!("-fdb {}", sh_q(&self.fdb))); }
        if !self.boot.is_empty()  { p.push(format!("-boot order={}", self.boot)); }
        if self.net {
            let model = if self.net_model.is_empty() { "virtio" } else { &self.net_model };
            p.push(format!("-net nic,model={} -net user", model));
        } else {
            p.push("-net none".into());
        }
        p.push(format!("-display {}", self.display));
        if !self.vga.is_empty() && self.vga != "none" {
            p.push(format!("-vga {}", self.vga));
        }
        if !self.extra.is_empty() { p.push(self.extra.clone()); }
        let pid = sh_q(&self.pid_path().to_string_lossy());
        format!("{} & echo $! > {}", p.join(" "), pid)
    }
}

fn sh_q(s: &str) -> String { format!("'{}'", s.replace('\'', "'\\''")) }

// ── Persistence ───────────────────────────────────────────────────────────────

fn config_dir() -> PathBuf {
    let home = std::env::var("HOME").unwrap_or_else(|_| "/home/mark".into());
    PathBuf::from(home).join(".local/share/chiqemu")
}

fn vms_dir() -> PathBuf {
    let home = std::env::var("HOME").unwrap_or_else(|_| "/home/mark".into());
    PathBuf::from(home).join("vms")
}

fn load_vms() -> Vec<Vm> {
    let dir = config_dir();
    let _ = fs::create_dir_all(&dir);
    let Ok(rd) = fs::read_dir(&dir) else { return vec![] };
    let mut entries: Vec<_> = rd.filter_map(|e| e.ok()).collect();
    entries.sort_by_key(|e| e.file_name());
    entries.iter().filter_map(|e| {
        let p = e.path();
        if p.extension()?.to_str()? != "json" { return None; }
        serde_json::from_str(&fs::read_to_string(p).ok()?).ok()
    }).collect()
}

fn save_vm(vm: &Vm) -> io::Result<()> {
    let dir = config_dir();
    fs::create_dir_all(&dir)?;
    let data = serde_json::to_string_pretty(vm)
        .map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
    fs::write(dir.join(format!("{}.json", vm.name)), data)
}

fn remove_vm_config(name: &str) -> io::Result<()> {
    fs::remove_file(config_dir().join(format!("{}.json", name)))
}

// ── Disk ops ──────────────────────────────────────────────────────────────────

fn create_disk(path: &str, gb: u32) -> Result<(), String> {
    if let Some(par) = Path::new(path).parent() {
        fs::create_dir_all(par).map_err(|e| e.to_string())?;
    }
    let st = Command::new("qemu-img")
        .args(["create", "-f", "qcow2", path, &format!("{}G", gb)])
        .status().map_err(|e| format!("qemu-img: {}", e))?;
    if !st.success() { return Err(format!("qemu-img create failed ({:?})", st.code())); }
    Ok(())
}

fn resize_disk(path: &str, gb: u32) -> Result<(), String> {
    let st = Command::new("qemu-img")
        .args(["resize", path, &format!("{}G", gb)])
        .status().map_err(|e| format!("qemu-img: {}", e))?;
    if !st.success() { return Err(format!("resize failed ({:?})", st.code())); }
    Ok(())
}

fn launch_vm(vm: &Vm) -> Result<(), String> {
    Command::new("sh").arg("-c").arg(vm.shell_cmd())
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn().map_err(|e| format!("spawn: {}", e))?
        .wait().map_err(|e| e.to_string())?;
    Ok(())
}

// ── Disk info ─────────────────────────────────────────────────────────────────

struct DiskInfo { format: String, virtual_size: u64, actual_size: u64, snap_count: usize }

#[derive(Deserialize)]
struct QiInfo {
    #[serde(default)] format: String,
    #[serde(rename = "virtual-size", default)] virtual_size: u64,
    #[serde(rename = "actual-size",  default)] actual_size:  u64,
    #[serde(default)] snapshots: Vec<serde_json::Value>,
}

fn load_disk_info(disk: &str) -> Option<DiskInfo> {
    if disk.is_empty() || !Path::new(disk).exists() { return None; }
    let out = Command::new("qemu-img").args(["info","--output=json",disk]).output().ok()?;
    let qi: QiInfo = serde_json::from_slice(&out.stdout).ok()?;
    Some(DiskInfo { format: qi.format, virtual_size: qi.virtual_size,
                    actual_size: qi.actual_size, snap_count: qi.snapshots.len() })
}

fn fmt_bytes(b: u64) -> String {
    if      b >= 1<<30 { format!("{:.1} GB", b as f64/(1u64<<30) as f64) }
    else if b >= 1<<20 { format!("{:.1} MB", b as f64/(1u64<<20) as f64) }
    else if b >= 1<<10 { format!("{:.1} KB", b as f64/(1u64<<10) as f64) }
    else               { format!("{} B", b) }
}

// ── Disk entries (Disks tab) ──────────────────────────────────────────────────

struct DiskEntry { path: String, name: String, info: Option<DiskInfo>, used_by: String }

fn scan_disks(vms: &[Vm]) -> Vec<DiskEntry> {
    let dir = vms_dir();
    let _ = fs::create_dir_all(&dir);

    // Collect paths: ~/vms/ contents + any VM-referenced disks outside ~/vms/
    let mut paths: Vec<PathBuf> = Vec::new();

    if let Ok(rd) = fs::read_dir(&dir) {
        let mut entries: Vec<_> = rd.filter_map(|e| e.ok()).collect();
        entries.sort_by_key(|e| e.file_name());
        for e in entries {
            let p = e.path();
            if let Some(ext) = p.extension().and_then(|s| s.to_str()) {
                if matches!(ext, "qcow2"|"img"|"raw"|"iso") { paths.push(p); }
            }
        }
    }

    // Add VM disk/cdrom paths not already in ~/vms/
    for vm in vms {
        for disk_path in [&vm.disk, &vm.hdb, &vm.hdc, &vm.cdrom, &vm.fda, &vm.fdb] {
            if disk_path.is_empty() { continue; }
            let p = PathBuf::from(disk_path);
            if p.exists() && !paths.contains(&p) { paths.push(p); }
        }
    }

    paths.iter().filter_map(|p| {
        let path = p.to_string_lossy().into_owned();
        let name = p.file_name()?.to_string_lossy().into_owned();
        let info = load_disk_info(&path);
        let used_by = vms.iter()
            .filter(|v| v.disk == path || v.hdb == path || v.hdc == path
                     || v.cdrom == path || v.fda == path || v.fdb == path)
            .map(|v| v.name.clone())
            .collect::<Vec<_>>().join(", ");
        Some(DiskEntry { path, name, info, used_by })
    }).collect()
}

// ── Snapshots ─────────────────────────────────────────────────────────────────

struct Snap { id: String, name: String, date: String }

fn load_snaps(disk: &str) -> Vec<Snap> {
    if disk.is_empty() || !Path::new(disk).exists() { return vec![]; }
    let Ok(out) = Command::new("qemu-img").args(["snapshot","-l",disk]).output() else { return vec![] };
    let text = String::from_utf8_lossy(&out.stdout);
    let mut snaps = Vec::new();
    let mut past = false;
    for line in text.lines() {
        if line.starts_with("ID") { past = true; continue; }
        if !past || line.trim().is_empty() { continue; }
        let f: Vec<&str> = line.split_whitespace().collect();
        if f.len() < 4 { continue; }
        let date = if f.len() >= 6 { format!("{} {}", f[f.len()-3], f[f.len()-2]) } else { String::new() };
        snaps.push(Snap { id: f[0].into(), name: f[1].into(), date });
    }
    snaps
}

fn snap_create(disk: &str, name: &str) -> Result<(), String> {
    let st = Command::new("qemu-img").args(["snapshot","-c",name,disk])
        .status().map_err(|e| e.to_string())?;
    if !st.success() { return Err(format!("snapshot create failed ({:?})", st.code())); }
    Ok(())
}

fn snap_restore(disk: &str, name: &str) -> Result<(), String> {
    let st = Command::new("qemu-img").args(["snapshot","-a",name,disk])
        .status().map_err(|e| e.to_string())?;
    if !st.success() { return Err(format!("restore failed ({:?})", st.code())); }
    Ok(())
}

fn snap_delete(disk: &str, name: &str) -> Result<(), String> {
    let st = Command::new("qemu-img").args(["snapshot","-d",name,disk])
        .status().map_err(|e| e.to_string())?;
    if !st.success() { return Err(format!("delete failed ({:?})", st.code())); }
    Ok(())
}

// ── Clone ─────────────────────────────────────────────────────────────────────

fn clone_disk_path(src: &str, new_name: &str) -> String {
    let p = Path::new(src);
    let par = p.parent().unwrap_or(Path::new("."));
    let stem = p.file_stem().and_then(|s| s.to_str()).unwrap_or("disk");
    let ext  = p.extension().and_then(|s| s.to_str()).unwrap_or("qcow2");
    par.join(format!("{}-{}.{}", stem, new_name, ext)).to_string_lossy().into_owned()
}

fn linked_clone(src: &str, dst: &str) -> Result<(), String> {
    if let Some(p) = Path::new(dst).parent() { fs::create_dir_all(p).map_err(|e| e.to_string())?; }
    let st = Command::new("qemu-img").args(["create","-f","qcow2","-b",src,"-F","qcow2",dst])
        .status().map_err(|e| format!("qemu-img: {}", e))?;
    if !st.success() { return Err(format!("linked clone failed ({:?})", st.code())); }
    Ok(())
}

// ── Form ──────────────────────────────────────────────────────────────────────

// Field indices
const F_NAME:   usize = 0;
const F_ARCH:   usize = 1;
const F_RAM:    usize = 2;
const F_CPU:    usize = 3;
const F_DISK:   usize = 4;
const F_HDB:    usize = 5;
const F_HDC:    usize = 6;
const F_CD:     usize = 7;
const F_FDA:    usize = 8;
const F_FDB:    usize = 9;
const F_BOOT:   usize = 10;
const F_DISP:   usize = 11;
const F_VGA:    usize = 12;
const F_NET:    usize = 13;
const F_NMODEL: usize = 14;
const F_KVM:    usize = 15;
const F_EXT:    usize = 16;
const F_COUNT:  usize = 17;

// Tab definitions: (name, field_start, field_end_exclusive)
const TABS: &[(&str, usize, usize)] = &[
    ("Basic",    0,  4),
    ("Storage",  4,  11),
    ("Display",  11, 13),
    ("Network",  13, 15),
    ("Advanced", 15, 17),
];

const FORM_LABELS: [&str; F_COUNT] = [
    "Name", "Arch", "RAM (MB)", "Cores",
    "Disk (hda)", "Drive 2 (hdb)", "Drive 3 (hdc)", "CD-ROM", "Floppy A", "Floppy B", "Boot order",
    "Display", "VGA",
    "Network", "Net model",
    "KVM", "Extra args",
];

const FORM_HINTS: [&str; F_COUNT] = [
    "identifier (no spaces/slashes)",
    "x86_64 · aarch64 · i386 · riscv64",
    "memory in megabytes",
    "vCPU count",
    "primary hard disk (.qcow2 or .img)",
    "second hard disk (optional)",
    "third hard disk (optional)",
    "CD-ROM/DVD image (.iso, optional)",
    "floppy disk image (.img, optional)",
    "second floppy (optional)",
    "(blank) · dc · cd · d · c · a",
    "sdl · gtk · vnc · none",
    "(blank) · std · cirrus · virtio · vmware",
    "yes · no  (user NAT)",
    "virtio · e1000 · rtl8139 · ne2k_pci",
    "yes · no  (hardware accel, x86 only)",
    "additional qemu-system flags",
];

fn field_tab(f: usize) -> usize {
    TABS.iter().position(|&(_, s, e)| f >= s && f < e).unwrap_or(0)
}

struct Form {
    fields:    [String; F_COUNT],
    focus:     usize,
    is_edit:   bool,
    orig_name: String,
    error:     String,
}

impl Form {
    fn new_vm() -> Self {
        Form {
            fields: [
                String::new(), "x86_64".into(), "2048".into(), "2".into(),
                String::new(), String::new(), String::new(), String::new(),
                String::new(), String::new(), "dc".into(),
                "sdl".into(), "std".into(),
                "yes".into(), "virtio".into(),
                "yes".into(), String::new(),
            ],
            focus: 0, is_edit: false, orig_name: String::new(), error: String::new(),
        }
    }

    fn from_vm(vm: &Vm) -> Self {
        Form {
            fields: [
                vm.name.clone(), vm.arch.clone(), vm.ram.to_string(), vm.cores.to_string(),
                vm.disk.clone(), vm.hdb.clone(), vm.hdc.clone(), vm.cdrom.clone(),
                vm.fda.clone(), vm.fdb.clone(), vm.boot.clone(),
                vm.display.clone(), vm.vga.clone(),
                bool_yn(vm.net), if vm.net_model.is_empty() { "virtio".into() } else { vm.net_model.clone() },
                bool_yn(vm.kvm), vm.extra.clone(),
            ],
            focus: 0, is_edit: true, orig_name: vm.name.clone(), error: String::new(),
        }
    }

    fn to_vm(&self) -> Result<Vm, String> {
        let name = self.fields[F_NAME].trim().to_string();
        if name.is_empty() { return Err("Name is required".into()); }
        if name.contains('/') || name.contains(' ') { return Err("Name: no spaces or slashes".into()); }
        let ram: u32   = self.fields[F_RAM].trim().parse().map_err(|_| "RAM: not a number")?;
        let cores: u32 = self.fields[F_CPU].trim().parse().map_err(|_| "Cores: not a number")?;
        Ok(Vm {
            name, arch: self.fields[F_ARCH].trim().to_string(), ram, cores,
            disk:      self.fields[F_DISK].trim().to_string(),
            hdb:       self.fields[F_HDB].trim().to_string(),
            hdc:       self.fields[F_HDC].trim().to_string(),
            cdrom:     self.fields[F_CD].trim().to_string(),
            fda:       self.fields[F_FDA].trim().to_string(),
            fdb:       self.fields[F_FDB].trim().to_string(),
            boot:      self.fields[F_BOOT].trim().to_string(),
            display:   self.fields[F_DISP].trim().to_string(),
            vga:       self.fields[F_VGA].trim().to_string(),
            net:       yn_bool(&self.fields[F_NET]),
            net_model: self.fields[F_NMODEL].trim().to_string(),
            kvm:       yn_bool(&self.fields[F_KVM]),
            extra:     self.fields[F_EXT].trim().to_string(),
        })
    }

    fn is_enum(i: usize) -> bool {
        matches!(i, F_ARCH|F_BOOT|F_DISP|F_VGA|F_NET|F_NMODEL|F_KVM)
    }

    fn cycle(fields: &mut [String; F_COUNT], i: usize) {
        match i {
            F_ARCH   => cyc(&mut fields[i], &["x86_64","aarch64","i386","riscv64"]),
            F_BOOT   => cyc(&mut fields[i], &["dc","cd","d","c","a",""]),
            F_DISP   => cyc(&mut fields[i], &["sdl","gtk","vnc","none"]),
            F_VGA    => cyc(&mut fields[i], &["std","cirrus","virtio","vmware",""]),
            F_NET    => { fields[i] = if yn_bool(&fields[i]) { "no".into() } else { "yes".into() } }
            F_NMODEL => cyc(&mut fields[i], &["virtio","e1000","rtl8139","ne2k_pci"]),
            F_KVM    => { fields[i] = if yn_bool(&fields[i]) { "no".into() } else { "yes".into() } }
            _ => {}
        }
    }

    fn cur_tab(&self) -> usize { field_tab(self.focus) }

    fn goto_tab(&mut self, tab: usize) {
        if tab < TABS.len() { self.focus = TABS[tab].1; }
    }

    fn tab_next_field(&mut self) {
        let (_, _, end) = TABS[self.cur_tab()];
        self.focus = if self.focus + 1 < end { self.focus + 1 } else { TABS[self.cur_tab()].1 };
    }

    fn tab_prev_field(&mut self) {
        let (_, start, _) = TABS[self.cur_tab()];
        self.focus = if self.focus > start { self.focus - 1 } else { TABS[self.cur_tab()].2 - 1 };
    }
}

fn cyc(val: &mut String, opts: &[&str]) {
    let i = opts.iter().position(|&o| o == val.as_str()).unwrap_or(0);
    *val = opts[(i + 1) % opts.len()].into();
}

fn bool_yn(b: bool) -> String { if b { "yes".into() } else { "no".into() } }
fn yn_bool(s: &str)  -> bool  { s.trim().eq_ignore_ascii_case("yes") }

// ── App ───────────────────────────────────────────────────────────────────────

enum Screen { List, Form, Detail, Snapshots }

enum Modal {
    None,
    ConfirmDelete,
    ConfirmStop,
    CreateDisk { path: String, size: String, auto_start: bool },
    ResizeDisk { path: String, size: String },
    RenameDisk { path: String, name: String },
    Clone(String),
    CreateSnap(String),
    ConfirmRestore(String),
    ConfirmDelSnap(String),
    ConfirmDelDisk(String),
    Error(String),
}

struct App {
    vms:      Vec<Vm>,
    vm_ts:    TableState,
    screen:   Screen,
    form:     Form,
    modal:    Modal,
    main_tab: usize,       // 0=VMs 1=Disks
    // detail/snap context
    view_idx: usize,
    disk_info: Option<DiskInfo>,
    snaps:    Vec<Snap>,
    snap_ts:  TableState,
    // disks tab
    disks:    Vec<DiskEntry>,
    disk_ts:  TableState,
}

impl App {
    fn new() -> Self {
        let vms = load_vms();
        let mut vm_ts = TableState::default();
        if !vms.is_empty() { vm_ts.select(Some(0)); }
        let disks = scan_disks(&vms);
        let mut disk_ts = TableState::default();
        if !disks.is_empty() { disk_ts.select(Some(0)); }
        App {
            vms, vm_ts, screen: Screen::List, form: Form::new_vm(),
            modal: Modal::None, main_tab: 0,
            view_idx: 0, disk_info: None, snaps: vec![], snap_ts: TableState::default(),
            disks, disk_ts,
        }
    }

    fn reload(&mut self) {
        let prev = self.vm_ts.selected();
        self.vms = load_vms();
        let sel = prev.map(|i| i.min(self.vms.len().saturating_sub(1)));
        self.vm_ts.select(if self.vms.is_empty() { None } else { sel.or(Some(0)) });
        self.reload_disks();
    }

    fn reload_disks(&mut self) {
        let prev = self.disk_ts.selected();
        self.disks = scan_disks(&self.vms);
        let sel = prev.map(|i| i.min(self.disks.len().saturating_sub(1)));
        self.disk_ts.select(if self.disks.is_empty() { None } else { sel.or(Some(0)) });
    }

    fn selected_vm(&self) -> Option<&Vm> {
        self.vm_ts.selected().and_then(|i| self.vms.get(i))
    }

    fn view_vm(&self) -> Option<&Vm> { self.vms.get(self.view_idx) }

    fn selected_disk(&self) -> Option<&DiskEntry> {
        self.disk_ts.selected().and_then(|i| self.disks.get(i))
    }

    fn nav_vm(&mut self, d: i32) {
        if self.vms.is_empty() { return; }
        let n = self.vms.len();
        let i = self.vm_ts.selected().unwrap_or(0);
        self.vm_ts.select(Some((i as i32 + d).rem_euclid(n as i32) as usize));
    }

    fn nav_disk(&mut self, d: i32) {
        if self.disks.is_empty() { return; }
        let n = self.disks.len();
        let i = self.disk_ts.selected().unwrap_or(0);
        self.disk_ts.select(Some((i as i32 + d).rem_euclid(n as i32) as usize));
    }

    fn nav_snap(&mut self, d: i32) {
        if self.snaps.is_empty() { return; }
        let n = self.snaps.len();
        let i = self.snap_ts.selected().unwrap_or(0);
        self.snap_ts.select(Some((i as i32 + d).rem_euclid(n as i32) as usize));
    }

    fn enter_detail(&mut self, idx: usize) {
        self.view_idx = idx;
        let disk = self.vms.get(idx).map(|v| v.disk.clone()).unwrap_or_default();
        self.disk_info = load_disk_info(&disk);
        self.screen = Screen::Detail;
    }

    fn enter_snapshots(&mut self) {
        let disk = self.view_vm().map(|v| v.disk.clone()).unwrap_or_default();
        self.snaps = load_snaps(&disk);
        self.snap_ts = TableState::default();
        if !self.snaps.is_empty() { self.snap_ts.select(Some(0)); }
        self.screen = Screen::Snapshots;
    }

    fn start_selected(&self) -> Result<(), String> {
        let vm = self.selected_vm().ok_or("No VM selected")?;
        if vm.is_running() { return Err(format!("{} is already running", vm.name)); }
        launch_vm(vm)
    }

    fn stop_selected(&self) -> Result<(), String> {
        let vm = self.selected_vm().ok_or("No VM selected")?;
        if !vm.is_running() { return Err(format!("{} is not running", vm.name)); }
        let pid = vm.get_pid().ok_or("No PID file")?;
        Command::new("kill").arg("-TERM").arg(pid.to_string())
            .status().map_err(|e| e.to_string())?;
        let _ = fs::remove_file(vm.pid_path());
        Ok(())
    }

    fn delete_selected_vm(&mut self) -> Result<(), String> {
        let vm = self.selected_vm().ok_or("No VM selected")?.clone();
        if vm.is_running() { return Err(format!("Stop {} first", vm.name)); }
        remove_vm_config(&vm.name).map_err(|e| e.to_string())?;
        self.reload();
        Ok(())
    }
}

// ── Drawing ───────────────────────────────────────────────────────────────────

fn draw(f: &mut Frame, app: &mut App) {
    let area = f.area();
    f.render_widget(Block::default().style(s_base()), area);

    let [title_a, tabs_a, body_a, bar_a] = Layout::vertical([
        Constraint::Length(1),
        Constraint::Length(1),
        Constraint::Min(0),
        Constraint::Length(1),
    ]).areas(area);

    draw_title(f, title_a);
    draw_main_tabs(f, app, tabs_a);

    match app.screen {
        Screen::List      => match app.main_tab {
            1 => draw_disks(f, app, body_a),
            _ => draw_vms(f, app, body_a),
        },
        Screen::Form      => draw_form(f, app, body_a),
        Screen::Detail    => draw_detail(f, app, body_a),
        Screen::Snapshots => draw_snapshots(f, app, body_a),
    }

    draw_bar(f, app, bar_a);

    match &app.modal {
        Modal::ConfirmDelete  => draw_confirm(f, area, " Delete VM? ",   "  [y] delete forever    [n] cancel  ", C_RED),
        Modal::ConfirmStop    => draw_confirm(f, area, " Stop VM? ",     "  [y] send SIGTERM      [n] cancel  ", C_ACCENT),
        Modal::ConfirmDelDisk(p) => {
            let msg = format!("  Delete {}?  ", Path::new(p).file_name()
                .and_then(|s| s.to_str()).unwrap_or(p));
            draw_confirm(f, area, " Delete disk file? ", &msg, C_RED);
        }
        Modal::ConfirmRestore(sn) => {
            let msg = format!("  Restore to snapshot '{}'?  ", sn);
            draw_confirm(f, area, " Restore? ", &msg, C_YELLOW);
        }
        Modal::ConfirmDelSnap(sn) => {
            let msg = format!("  Delete snapshot '{}'?  ", sn);
            draw_confirm(f, area, " Delete snapshot? ", &msg, C_RED);
        }
        Modal::CreateDisk { path, size, auto_start } => {
            let (p, s) = (path.clone(), size.clone());
            let hint = if *auto_start { "[Enter] create & start    [Esc] cancel" }
                       else           { "[Enter] create    [Esc] cancel" };
            draw_size_modal(f, area, " Create disk image ", &p, &s, hint);
        }
        Modal::ResizeDisk { path, size } => {
            let (p, s) = (path.clone(), size.clone());
            draw_size_modal(f, area, " Resize disk image ", &p, &s,
                "[Enter] resize    [Esc] cancel");
        }
        Modal::RenameDisk { path, name } => {
            let (p, n) = (path.clone(), name.clone());
            draw_input_modal(f, area, " Rename disk ", &trunc(&p, 50), &n,
                "[Enter] rename    [Esc] cancel");
        }
        Modal::Clone(name) => {
            let name = name.clone();
            let src = app.selected_vm().map(|v| v.disk.clone()).unwrap_or_default();
            draw_clone_modal(f, area, &name, &src);
        }
        Modal::CreateSnap(name) => {
            let name = name.clone();
            draw_input_modal(f, area, " New Snapshot ", "Snapshot name:", &name,
                "[Enter] create    [Esc] cancel");
        }
        Modal::Error(msg) => { let m = msg.clone(); draw_msg(f, area, " Error ", &m, C_RED); }
        Modal::None => {}
    }
}

fn draw_title(f: &mut Frame, area: Rect) {
    f.render_widget(Paragraph::new(Line::from(vec![
        Span::styled(" chiqemu", Style::default().fg(C_ACCENT).add_modifier(Modifier::BOLD)),
        Span::styled(" — QEMU virtual machine manager", s_dim()),
    ])).style(s_base()), area);
}

fn draw_main_tabs(f: &mut Frame, app: &App, area: Rect) {
    let tabs = ["VMs", "Disks"];
    let spans: Vec<Span> = tabs.iter().enumerate().flat_map(|(i, t)| {
        if i == app.main_tab {
            vec![Span::styled(format!(" {} ", t),
                Style::default().fg(C_BG).bg(C_ACCENT).add_modifier(Modifier::BOLD))]
        } else {
            vec![Span::styled(format!(" {} ", t), s_dim())]
        }
    }).collect();
    f.render_widget(Paragraph::new(Line::from(spans)).style(s_base()), area);
}

fn draw_vms(f: &mut Frame, app: &mut App, area: Rect) {
    let block = Block::default().borders(Borders::ALL).border_style(s_bord())
        .title(Span::styled(" VMs ", Style::default().fg(C_ACCENT))).style(s_base());

    let header = Row::new(["Name","Status","RAM","Cores","Arch","Boot","Disk"]
        .map(|h| Cell::from(h).style(s_head()))).height(1);

    let rows: Vec<Row> = app.vms.iter().map(|vm| {
        let (stat, ss) = if vm.is_running() { ("● running", s_run()) } else { ("○ stopped", s_dim()) };
        Row::new(vec![
            Cell::from(vm.name.clone()),
            Cell::from(stat).style(ss),
            Cell::from(format!("{} MB", vm.ram)),
            Cell::from(vm.cores.to_string()),
            Cell::from(vm.arch.clone()),
            Cell::from(if vm.boot.is_empty() { "—".into() } else { vm.boot.clone() }),
            Cell::from(trunc(&vm.disk, 28)),
        ]).style(s_base()).height(1)
    }).collect();

    let widths = [
        Constraint::Length(16), Constraint::Length(12), Constraint::Length(9),
        Constraint::Length(6),  Constraint::Length(9),  Constraint::Length(5),
        Constraint::Min(10),
    ];
    f.render_stateful_widget(
        Table::new(rows, widths).header(header).block(block).row_highlight_style(s_sel()),
        area, &mut app.vm_ts,
    );
}

fn draw_disks(f: &mut Frame, app: &mut App, area: Rect) {
    let block = Block::default().borders(Borders::ALL).border_style(s_bord())
        .title(Span::styled(" Disks  ~/vms/ ", Style::default().fg(C_ACCENT))).style(s_base());

    if app.disks.is_empty() {
        let inner = block.inner(area);
        f.render_widget(block, area);
        f.render_widget(
            Paragraph::new("~/vms/ is empty — press [n] to create a disk image")
                .alignment(Alignment::Center).style(s_dim()),
            Rect { x: inner.x, y: inner.y + inner.height/2, width: inner.width, height: 1 },
        );
        return;
    }

    let header = Row::new(["Name","Format","Virtual","Actual","Used by"]
        .map(|h| Cell::from(h).style(s_head()))).height(1);

    let rows: Vec<Row> = app.disks.iter().map(|d| {
        let (fmt, virt, act) = match &d.info {
            Some(i) => (i.format.clone(), fmt_bytes(i.virtual_size), fmt_bytes(i.actual_size)),
            None    => ("?".into(), "?".into(), "?".into()),
        };
        Row::new(vec![
            Cell::from(d.name.clone()),
            Cell::from(fmt),
            Cell::from(virt),
            Cell::from(act),
            Cell::from(if d.used_by.is_empty() { "—".into() } else { d.used_by.clone() }),
        ]).style(s_base()).height(1)
    }).collect();

    let widths = [
        Constraint::Length(28), Constraint::Length(8), Constraint::Length(10),
        Constraint::Length(10), Constraint::Min(10),
    ];
    f.render_stateful_widget(
        Table::new(rows, widths).header(header).block(block).row_highlight_style(s_sel()),
        area, &mut app.disk_ts,
    );
}

fn draw_detail(f: &mut Frame, app: &App, area: Rect) {
    let Some(vm) = app.view_vm() else { return };
    let block = Block::default().borders(Borders::ALL)
        .border_style(Style::default().fg(C_ACCENT))
        .title(Span::styled(format!(" VM: {} ", vm.name),
            Style::default().fg(C_CURSOR).add_modifier(Modifier::BOLD)))
        .style(s_base());
    f.render_widget(block, area);

    let inner = Rect { x: area.x+2, y: area.y+1,
        width: area.width.saturating_sub(4), height: area.height.saturating_sub(2) };

    let rows: &[(&str, String)] = &[
        ("Arch",      vm.arch.clone()),
        ("RAM",       format!("{} MB", vm.ram)),
        ("Cores",     vm.cores.to_string()),
        ("Boot",      if vm.boot.is_empty() { "—".into() } else { vm.boot.clone() }),
        ("Display",   vm.display.clone()),
        ("VGA",       if vm.vga.is_empty() { "—".into() } else { vm.vga.clone() }),
        ("Network",   bool_yn(vm.net)),
        ("Net model", if vm.net_model.is_empty() { "virtio".into() } else { vm.net_model.clone() }),
        ("KVM",       bool_yn(vm.kvm)),
        ("Disk (hda)", if vm.disk.is_empty()  { "—".into() } else { vm.disk.clone() }),
        ("Drive 2",   if vm.hdb.is_empty()   { "—".into() } else { vm.hdb.clone() }),
        ("Drive 3",   if vm.hdc.is_empty()   { "—".into() } else { vm.hdc.clone() }),
        ("CD-ROM",    if vm.cdrom.is_empty() { "—".into() } else { vm.cdrom.clone() }),
        ("Floppy A",  if vm.fda.is_empty()   { "—".into() } else { vm.fda.clone() }),
        ("Floppy B",  if vm.fdb.is_empty()   { "—".into() } else { vm.fdb.clone() }),
        ("Extra",     if vm.extra.is_empty() { "—".into() } else { vm.extra.clone() }),
    ];

    let mut y = inner.y;
    for (lbl, val) in rows {
        if y >= inner.y + inner.height { break; }
        f.render_widget(Paragraph::new(format!("{:>10}: ", lbl)).style(s_dim()),
            Rect { x: inner.x, y, width: 12, height: 1 });
        f.render_widget(Paragraph::new(val.as_str()).style(s_base()),
            Rect { x: inner.x+12, y, width: inner.width.saturating_sub(12), height: 1 });
        y += 1;
    }

    y += 1;
    if y < inner.y + inner.height {
        let sep = "─".repeat((inner.width as usize).min(40));
        f.render_widget(Paragraph::new(format!(" disk {}", sep)).style(s_dim()),
            Rect { x: inner.x, y, width: inner.width, height: 1 });
        y += 1;
    }

    match &app.disk_info {
        None => {
            if y < inner.y + inner.height {
                let msg = if vm.disk.is_empty() { "no disk configured" } else { "disk file not found" };
                f.render_widget(Paragraph::new(msg).style(s_dim()),
                    Rect { x: inner.x+2, y, width: inner.width, height: 1 });
            }
        }
        Some(di) => {
            for (lbl, val) in &[
                ("Format",    di.format.clone()),
                ("Virtual",   fmt_bytes(di.virtual_size)),
                ("Actual",    fmt_bytes(di.actual_size)),
                ("Snapshots", di.snap_count.to_string()),
            ] {
                if y >= inner.y + inner.height { break; }
                f.render_widget(Paragraph::new(format!("{:>10}: ", lbl)).style(s_dim()),
                    Rect { x: inner.x, y, width: 12, height: 1 });
                f.render_widget(Paragraph::new(val.as_str()).style(s_base()),
                    Rect { x: inner.x+12, y, width: inner.width.saturating_sub(12), height: 1 });
                y += 1;
            }
        }
    }
}

fn draw_snapshots(f: &mut Frame, app: &mut App, area: Rect) {
    let name = app.view_vm().map(|v| v.name.clone()).unwrap_or_default();
    let block = Block::default().borders(Borders::ALL)
        .border_style(Style::default().fg(C_ACCENT))
        .title(Span::styled(format!(" Snapshots: {} ", name),
            Style::default().fg(C_CURSOR).add_modifier(Modifier::BOLD)))
        .style(s_base());

    if app.snaps.is_empty() {
        let inner = block.inner(area);
        f.render_widget(block, area);
        f.render_widget(
            Paragraph::new("no snapshots — press [n] to create one")
                .alignment(Alignment::Center).style(s_dim()),
            Rect { x: inner.x, y: inner.y + inner.height/2, width: inner.width, height: 1 },
        );
        return;
    }

    let header = Row::new(["ID","Name","Date"].map(|h| Cell::from(h).style(s_head()))).height(1);
    let rows: Vec<Row> = app.snaps.iter().map(|s|
        Row::new(vec![Cell::from(s.id.clone()), Cell::from(s.name.clone()), Cell::from(s.date.clone())])
            .style(s_base()).height(1)
    ).collect();
    let widths = [Constraint::Length(6), Constraint::Length(24), Constraint::Min(20)];
    f.render_stateful_widget(
        Table::new(rows, widths).header(header).block(block).row_highlight_style(s_sel()),
        area, &mut app.snap_ts,
    );
}

fn draw_form(f: &mut Frame, app: &mut App, area: Rect) {
    let popup = centered_rect(68, 92, area);
    f.render_widget(Clear, popup);
    let title = if app.form.is_edit { " Edit VM " } else { " New VM " };
    let block = Block::default().borders(Borders::ALL)
        .border_style(Style::default().fg(C_ACCENT))
        .title(Span::styled(title, Style::default().fg(C_CURSOR).add_modifier(Modifier::BOLD)))
        .style(s_base());
    f.render_widget(block, popup);

    // Tab bar inside form
    let inner_full = Rect { x: popup.x+1, y: popup.y+1,
        width: popup.width.saturating_sub(2), height: popup.height.saturating_sub(2) };

    let tab_spans: Vec<Span> = TABS.iter().enumerate().flat_map(|(i, (name, _, _))| {
        if i == app.form.cur_tab() {
            vec![Span::styled(format!(" {} ", name),
                Style::default().fg(C_BG).bg(C_ACCENT).add_modifier(Modifier::BOLD))]
        } else {
            vec![Span::styled(format!(" {} ", name), s_dim())]
        }
    }).collect();
    f.render_widget(Paragraph::new(Line::from(tab_spans)).style(s_base()),
        Rect { x: inner_full.x, y: inner_full.y, width: inner_full.width, height: 1 });

    let inner = Rect { x: popup.x+2, y: popup.y+2,
        width: popup.width.saturating_sub(4), height: popup.height.saturating_sub(4) };

    // Reserve bottom 2 rows for hint + error
    let fields_height = inner.height.saturating_sub(3);
    let (_, tab_start, tab_end) = TABS[app.form.cur_tab()];
    for (row, fi) in (tab_start..tab_end).enumerate() {
        let y = inner.y + row as u16;
        if y >= inner.y + fields_height { break; }
        let focused = app.form.focus == fi;
        let val = &app.form.fields[fi];
        let ls = if focused { Style::default().fg(C_CURSOR).add_modifier(Modifier::BOLD) } else { s_dim() };
        let lw: u16 = 14;
        f.render_widget(Paragraph::new(format!("{:>12}: ", FORM_LABELS[fi])).style(ls),
            Rect { x: inner.x, y, width: lw, height: 1 });
        let vw = inner.width.saturating_sub(lw);
        let vs = if focused { Style::default().fg(C_FG).bg(C_BORDER).add_modifier(Modifier::BOLD) }
                 else { Style::default().fg(C_FG).bg(C_BG) };
        let disp = if Form::is_enum(fi) { format!(" ‹ {} › ", val) } else { format!(" {}█", val) };
        f.render_widget(Paragraph::new(disp).style(vs),
            Rect { x: inner.x+lw, y, width: vw, height: 1 });
    }

    // Hint line (fixed at bottom)
    let hint_y = inner.y + inner.height.saturating_sub(2);
    let hint = FORM_HINTS[app.form.focus];
    f.render_widget(Paragraph::new(format!(" ↳ {}", hint)).style(s_dim()),
        Rect { x: inner.x, y: hint_y, width: inner.width, height: 1 });

    // Error line
    if !app.form.error.is_empty() {
        let ey = inner.y + inner.height.saturating_sub(1);
        f.render_widget(Paragraph::new(format!(" ✗ {}", app.form.error)).style(s_err()),
            Rect { x: inner.x, y: ey, width: inner.width, height: 1 });
    }
}

fn draw_bar(f: &mut Frame, app: &App, area: Rect) {
    let spans: Vec<Span> = match app.screen {
        Screen::List => match app.main_tab {
            1 => {
                let has = app.disk_ts.selected().is_some();
                let mut v = vec![];
                if has { v.extend(kh("r","resize")); v.extend(kh("m","rename")); v.extend(kh("d","delete")); }
                v.extend(kh("n","new disk")); v.extend(kh("R","refresh"));
                v.extend(kh("h/l","switch tab")); v.extend(kh("q","quit")); v
            }
            _ => {
                let has = app.vm_ts.selected().is_some();
                let mut v = vec![];
                if has {
                    v.extend(kh("Enter","start")); v.extend(kh("s","stop"));
                    v.extend(kh("i","detail")); v.extend(kh("S","snaps"));
                    v.extend(kh("C","clone")); v.extend(kh("e","edit")); v.extend(kh("d","del"));
                }
                v.extend(kh("n","new")); v.extend(kh("r","reload"));
                v.extend(kh("h/l","switch tab")); v.extend(kh("q","quit")); v
            }
        },
        Screen::Form => {
            let hint = FORM_HINTS.get(app.form.focus).copied().unwrap_or("");
            let mut v = vec![];
            v.extend(kh("[/]","prev/next tab")); v.extend(kh("Tab","next field"));
            v.extend(kh("Space","cycle")); v.extend(kh("^S","save")); v.extend(kh("Esc","cancel"));
            v.push(Span::styled(format!("  {}", hint), s_dim())); v
        }
        Screen::Detail => {
            let mut v = vec![];
            v.extend(kh("S","snapshots")); v.extend(kh("C","clone"));
            v.extend(kh("e","edit")); v.extend(kh("Esc/q","back")); v
        }
        Screen::Snapshots => {
            let mut v = vec![];
            v.extend(kh("n","new")); v.extend(kh("Enter","restore"));
            v.extend(kh("d","delete")); v.extend(kh("r","refresh")); v.extend(kh("Esc/q","back")); v
        }
    };
    f.render_widget(Paragraph::new(Line::from(spans)).style(s_base()), area);
}

fn kh<'a>(k: &'a str, v: &'a str) -> [Span<'a>; 2] {
    [
        Span::styled(format!(" [{}]", k), Style::default().fg(C_ACCENT).add_modifier(Modifier::BOLD)),
        Span::styled(format!(" {} ", v), s_dim()),
    ]
}

fn draw_confirm(f: &mut Frame, area: Rect, title: &str, msg: &str, color: Color) {
    let popup = centered_rect(52, 24, area);
    f.render_widget(Clear, popup);
    let block = Block::default().borders(Borders::ALL)
        .border_style(Style::default().fg(color))
        .title(Span::styled(title, Style::default().fg(color).add_modifier(Modifier::BOLD)))
        .style(s_base());
    let inner = block.inner(popup);
    f.render_widget(block, popup);
    f.render_widget(Paragraph::new(format!("{}\n\n  [y] confirm    [n/Esc] cancel", msg))
        .alignment(Alignment::Center).style(s_base()), inner);
}

fn draw_msg(f: &mut Frame, area: Rect, title: &str, msg: &str, color: Color) {
    let popup = centered_rect(54, 22, area);
    f.render_widget(Clear, popup);
    let block = Block::default().borders(Borders::ALL)
        .border_style(Style::default().fg(color))
        .title(Span::styled(title, Style::default().fg(color).add_modifier(Modifier::BOLD)))
        .style(s_base());
    let inner = block.inner(popup);
    f.render_widget(block, popup);
    f.render_widget(Paragraph::new(format!("{}\n\n[any key] dismiss", msg))
        .alignment(Alignment::Center).style(s_base()), inner);
}

fn draw_size_modal(f: &mut Frame, area: Rect, title: &str, path: &str, size: &str, hint: &str) {
    let popup = centered_rect(56, 30, area);
    f.render_widget(Clear, popup);
    let block = Block::default().borders(Borders::ALL)
        .border_style(Style::default().fg(C_CURSOR))
        .title(Span::styled(title, Style::default().fg(C_CURSOR).add_modifier(Modifier::BOLD)))
        .style(s_base());
    let inner = block.inner(popup);
    f.render_widget(block, popup);
    let [pa, _, la, ia, _, ha] = Layout::vertical([
        Constraint::Length(1), Constraint::Length(1), Constraint::Length(1),
        Constraint::Length(1), Constraint::Length(1), Constraint::Length(1),
    ]).areas(inner);
    f.render_widget(Paragraph::new(trunc(path, inner.width as usize))
        .alignment(Alignment::Center).style(s_dim()), pa);
    f.render_widget(Paragraph::new("Size (GB):").style(s_base()), la);
    f.render_widget(Paragraph::new(format!(" {}█", size))
        .style(Style::default().fg(C_FG).bg(C_BORDER).add_modifier(Modifier::BOLD)), ia);
    f.render_widget(Paragraph::new(hint).style(s_dim()), ha);
}

fn draw_clone_modal(f: &mut Frame, area: Rect, name: &str, src_disk: &str) {
    let popup = centered_rect(58, 36, area);
    f.render_widget(Clear, popup);
    let block = Block::default().borders(Borders::ALL)
        .border_style(Style::default().fg(C_CURSOR))
        .title(Span::styled(" Clone VM ", Style::default().fg(C_CURSOR).add_modifier(Modifier::BOLD)))
        .style(s_base());
    let inner = block.inner(popup);
    f.render_widget(block, popup);
    let [la, ia, _, dla, dva, ara, nda, _, ha] = Layout::vertical([
        Constraint::Length(1), Constraint::Length(1), Constraint::Length(1),
        Constraint::Length(1), Constraint::Length(1), Constraint::Length(1),
        Constraint::Length(1), Constraint::Length(1), Constraint::Length(1),
    ]).areas(inner);
    f.render_widget(Paragraph::new("New name:").style(s_dim()), la);
    f.render_widget(Paragraph::new(format!(" {}█", name))
        .style(Style::default().fg(C_FG).bg(C_BORDER).add_modifier(Modifier::BOLD)), ia);
    if !src_disk.is_empty() && !name.is_empty() {
        let new_path = clone_disk_path(src_disk, name);
        f.render_widget(Paragraph::new("Disk (linked clone):").style(s_dim()), dla);
        f.render_widget(Paragraph::new(trunc(src_disk, inner.width as usize)).style(s_dim()), dva);
        f.render_widget(Paragraph::new("  →").style(Style::default().fg(C_ACCENT)), ara);
        f.render_widget(Paragraph::new(trunc(&new_path, inner.width as usize)).style(s_base()), nda);
    } else if src_disk.is_empty() {
        f.render_widget(Paragraph::new("(no disk — config only)").style(s_dim()), dla);
    }
    f.render_widget(Paragraph::new("[Enter] clone    [Esc] cancel").style(s_dim()), ha);
}

fn draw_input_modal(f: &mut Frame, area: Rect, title: &str, label: &str, value: &str, hint: &str) {
    let popup = centered_rect(50, 26, area);
    f.render_widget(Clear, popup);
    let block = Block::default().borders(Borders::ALL)
        .border_style(Style::default().fg(C_CURSOR))
        .title(Span::styled(title, Style::default().fg(C_CURSOR).add_modifier(Modifier::BOLD)))
        .style(s_base());
    let inner = block.inner(popup);
    f.render_widget(block, popup);
    let [la, ia, _, ha] = Layout::vertical([
        Constraint::Length(1), Constraint::Length(1),
        Constraint::Length(1), Constraint::Length(1),
    ]).areas(inner);
    f.render_widget(Paragraph::new(label).style(s_dim()), la);
    f.render_widget(Paragraph::new(format!(" {}█", value))
        .style(Style::default().fg(C_FG).bg(C_BORDER).add_modifier(Modifier::BOLD)), ia);
    f.render_widget(Paragraph::new(hint).style(s_dim()), ha);
}

fn centered_rect(px: u16, py: u16, r: Rect) -> Rect {
    let [_, mv, _] = Layout::vertical([
        Constraint::Percentage((100-py)/2), Constraint::Percentage(py), Constraint::Percentage((100-py)/2),
    ]).areas(r);
    let [_, mh, _] = Layout::horizontal([
        Constraint::Percentage((100-px)/2), Constraint::Percentage(px), Constraint::Percentage((100-px)/2),
    ]).areas(mv);
    mh
}

fn trunc(s: &str, max: usize) -> String {
    if max == 0 { return String::new(); }
    if s.len() <= max { s.to_string() } else { format!("…{}", &s[s.len()-(max-1)..]) }
}

// ── Events ────────────────────────────────────────────────────────────────────

fn on_key(app: &mut App, code: KeyCode, mods: KeyModifiers) -> bool {
    match &app.modal {
        Modal::ConfirmDelete => {
            app.modal = Modal::None;
            if matches!(code, KeyCode::Char('y')|KeyCode::Enter) {
                if let Err(e) = app.delete_selected_vm() { app.modal = Modal::Error(e); }
            }
            return false;
        }
        Modal::ConfirmStop => {
            app.modal = Modal::None;
            if matches!(code, KeyCode::Char('y')|KeyCode::Enter) {
                if let Err(e) = app.stop_selected() { app.modal = Modal::Error(e); }
            }
            return false;
        }
        Modal::ConfirmDelDisk(p) => {
            let path = p.clone();
            app.modal = Modal::None;
            if matches!(code, KeyCode::Char('y')|KeyCode::Enter) {
                if let Err(e) = fs::remove_file(&path).map_err(|e| e.to_string()) {
                    app.modal = Modal::Error(e);
                } else {
                    app.reload_disks();
                }
            }
            return false;
        }
        Modal::ConfirmRestore(sn) => {
            let sn = sn.clone();
            app.modal = Modal::None;
            if matches!(code, KeyCode::Char('y')|KeyCode::Enter) {
                let disk = app.view_vm().map(|v| v.disk.clone()).unwrap_or_default();
                match snap_restore(&disk, &sn) {
                    Err(e) => { app.modal = Modal::Error(e); }
                    Ok(_)  => { app.snaps = load_snaps(&disk); }
                }
            }
            return false;
        }
        Modal::ConfirmDelSnap(sn) => {
            let sn = sn.clone();
            app.modal = Modal::None;
            if matches!(code, KeyCode::Char('y')|KeyCode::Enter) {
                let disk = app.view_vm().map(|v| v.disk.clone()).unwrap_or_default();
                match snap_delete(&disk, &sn) {
                    Err(e) => { app.modal = Modal::Error(e); }
                    Ok(_)  => {
                        app.snaps = load_snaps(&disk);
                        let sel = app.snap_ts.selected().map(|i| i.min(app.snaps.len().saturating_sub(1)));
                        app.snap_ts.select(if app.snaps.is_empty() { None } else { sel });
                    }
                }
            }
            return false;
        }
        Modal::CreateDisk { .. }  => { on_create_disk_key(app, code); return false; }
        Modal::ResizeDisk { .. }  => { on_resize_disk_key(app, code); return false; }
        Modal::RenameDisk { .. }  => { on_rename_disk_key(app, code); return false; }
        Modal::Clone(_)           => { on_clone_key(app, code); return false; }
        Modal::CreateSnap(_)      => { on_create_snap_key(app, code); return false; }
        Modal::Error(_)           => { app.modal = Modal::None; return false; }
        Modal::None => {}
    }

    match app.screen {
        Screen::List      => on_list_key(app, code),
        Screen::Form      => on_form_key(app, code, mods),
        Screen::Detail    => on_detail_key(app, code),
        Screen::Snapshots => on_snap_key(app, code),
    }
}

fn on_list_key(app: &mut App, code: KeyCode) -> bool {
    // Tab switching
    match code {
        KeyCode::Char('h') => { app.main_tab = app.main_tab.saturating_sub(1); return false; }
        KeyCode::Char('l') => { app.main_tab = (app.main_tab + 1).min(1); return false; }
        KeyCode::Char('q') => return true,
        _ => {}
    }

    if app.main_tab == 1 {
        // Disks tab
        match code {
            KeyCode::Char('j')|KeyCode::Down  => app.nav_disk(1),
            KeyCode::Char('k')|KeyCode::Up    => app.nav_disk(-1),
            KeyCode::Char('R') => app.reload_disks(),
            KeyCode::Char('n') => {
                let path = vms_dir().join("new.qcow2").to_string_lossy().into_owned();
                app.modal = Modal::CreateDisk { path, size: "20".into(), auto_start: false };
            }
            KeyCode::Char('r') => {
                if let Some(d) = app.selected_disk() {
                    if d.info.is_some() {
                        let path = d.path.clone();
                        let cur = d.info.as_ref().map(|i| (i.virtual_size >> 30).to_string())
                            .unwrap_or_else(|| "20".into());
                        app.modal = Modal::ResizeDisk { path, size: cur };
                    } else {
                        app.modal = Modal::Error("Cannot resize: disk info unavailable".into());
                    }
                }
            }
            KeyCode::Char('m') => {
                if let Some(d) = app.selected_disk() {
                    let path = d.path.clone();
                    let name = d.name.clone();
                    app.modal = Modal::RenameDisk { path, name };
                }
            }
            KeyCode::Char('d') => {
                if let Some(d) = app.selected_disk() {
                    if !d.used_by.is_empty() {
                        app.modal = Modal::Error(format!("In use by: {}", d.used_by));
                    } else {
                        app.modal = Modal::ConfirmDelDisk(d.path.clone());
                    }
                }
            }
            _ => {}
        }
    } else {
        // VMs tab
        match code {
            KeyCode::Char('j')|KeyCode::Down  => app.nav_vm(1),
            KeyCode::Char('k')|KeyCode::Up    => app.nav_vm(-1),
            KeyCode::Char('n') => { app.form = Form::new_vm(); app.screen = Screen::Form; }
            KeyCode::Char('e') => {
                if let Some(vm) = app.selected_vm().cloned() {
                    app.form = Form::from_vm(&vm);
                    app.screen = Screen::Form;
                }
            }
            KeyCode::Char('i') => {
                if let Some(idx) = app.vm_ts.selected() { app.enter_detail(idx); }
            }
            KeyCode::Char('S') => {
                if let Some(idx) = app.vm_ts.selected() { app.enter_detail(idx); app.enter_snapshots(); }
            }
            KeyCode::Char('C') => {
                if app.selected_vm().is_some() { app.modal = Modal::Clone(String::new()); }
            }
            KeyCode::Char('d') => {
                if app.selected_vm().is_some() { app.modal = Modal::ConfirmDelete; }
            }
            KeyCode::Char('s') => {
                if let Some(vm) = app.selected_vm() {
                    if vm.is_running() { app.modal = Modal::ConfirmStop; }
                    else { app.modal = Modal::Error(format!("{} is not running", vm.name)); }
                }
            }
            KeyCode::Enter => {
                if let Some(vm) = app.selected_vm() {
                    let disk = vm.disk.clone();
                    if !disk.is_empty() && !Path::new(&disk).exists() {
                        app.modal = Modal::CreateDisk { path: disk, size: "20".into(), auto_start: true };
                    } else if let Err(e) = app.start_selected() {
                        app.modal = Modal::Error(e);
                    }
                }
            }
            KeyCode::Char('r') => app.reload(),
            _ => {}
        }
    }
    false
}

fn on_detail_key(app: &mut App, code: KeyCode) -> bool {
    match code {
        KeyCode::Esc|KeyCode::Char('q') => { app.screen = Screen::List; }
        KeyCode::Char('e') => {
            if let Some(vm) = app.view_vm().cloned() { app.form = Form::from_vm(&vm); app.screen = Screen::Form; }
        }
        KeyCode::Char('S') => { app.enter_snapshots(); }
        KeyCode::Char('C') => { app.modal = Modal::Clone(String::new()); }
        _ => {}
    }
    false
}

fn on_snap_key(app: &mut App, code: KeyCode) -> bool {
    match code {
        KeyCode::Esc|KeyCode::Char('q') => { app.screen = Screen::Detail; }
        KeyCode::Char('j')|KeyCode::Down  => app.nav_snap(1),
        KeyCode::Char('k')|KeyCode::Up    => app.nav_snap(-1),
        KeyCode::Char('r') => {
            let disk = app.view_vm().map(|v| v.disk.clone()).unwrap_or_default();
            app.snaps = load_snaps(&disk);
        }
        KeyCode::Char('n') => {
            if app.view_vm().map(|v| v.is_running()).unwrap_or(false) {
                app.modal = Modal::Error("Stop the VM before taking a snapshot".into());
            } else {
                app.modal = Modal::CreateSnap(String::new());
            }
        }
        KeyCode::Enter => {
            if let Some(i) = app.snap_ts.selected() {
                if let Some(sn) = app.snaps.get(i) {
                    if app.view_vm().map(|v| v.is_running()).unwrap_or(false) {
                        app.modal = Modal::Error("Stop the VM before restoring".into());
                    } else {
                        app.modal = Modal::ConfirmRestore(sn.name.clone());
                    }
                }
            }
        }
        KeyCode::Char('d') => {
            if let Some(i) = app.snap_ts.selected() {
                if let Some(sn) = app.snaps.get(i) {
                    app.modal = Modal::ConfirmDelSnap(sn.name.clone());
                }
            }
        }
        _ => {}
    }
    false
}

fn on_form_key(app: &mut App, code: KeyCode, mods: KeyModifiers) -> bool {
    match code {
        KeyCode::Esc => { app.screen = Screen::List; }
        KeyCode::Char('[')|KeyCode::Left => {
            let t = app.form.cur_tab();
            if t > 0 { app.form.goto_tab(t - 1); }
        }
        KeyCode::Char(']')|KeyCode::Right => {
            let t = app.form.cur_tab();
            if t + 1 < TABS.len() { app.form.goto_tab(t + 1); }
        }
        KeyCode::Tab | KeyCode::Down => { app.form.tab_next_field(); }
        KeyCode::BackTab | KeyCode::Up => { app.form.tab_prev_field(); }
        KeyCode::Char('s') if mods.contains(KeyModifiers::CONTROL) => do_save(app),
        KeyCode::Enter => {
            let fi = app.form.focus;
            if Form::is_enum(fi) { Form::cycle(&mut app.form.fields, fi); }
            else { app.form.tab_next_field(); }
        }
        KeyCode::Char(' ') => {
            let fi = app.form.focus;
            if Form::is_enum(fi) { Form::cycle(&mut app.form.fields, fi); }
            else { app.form.fields[fi].push(' '); }
        }
        KeyCode::Char(c) if !Form::is_enum(app.form.focus) => {
            app.form.fields[app.form.focus].push(c);
            app.form.error.clear();
        }
        KeyCode::Backspace if !Form::is_enum(app.form.focus) => {
            app.form.fields[app.form.focus].pop();
            app.form.error.clear();
        }
        _ => {}
    }
    false
}

fn on_create_disk_key(app: &mut App, code: KeyCode) {
    let Modal::CreateDisk { ref path, ref mut size, auto_start } = app.modal else { return };
    match code {
        KeyCode::Esc => { app.modal = Modal::None; }
        KeyCode::Char(c) if c.is_ascii_digit() => { size.push(c); }
        KeyCode::Backspace => { size.pop(); }
        KeyCode::Enter => {
            let gb: u32 = size.trim().parse().unwrap_or(0);
            if gb == 0 { app.modal = Modal::Error("Size must be > 0".into()); return; }
            let path = path.clone();
            let should_start = auto_start;
            match create_disk(&path, gb) {
                Err(e) => { app.modal = Modal::Error(e); }
                Ok(_) => {
                    app.modal = Modal::None;
                    app.reload_disks();
                    if should_start {
                        if let Err(e) = app.start_selected() {
                            app.modal = Modal::Error(e);
                        }
                    }
                }
            }
        }
        _ => {}
    }
}

fn on_resize_disk_key(app: &mut App, code: KeyCode) {
    let Modal::ResizeDisk { ref path, ref mut size } = app.modal else { return };
    match code {
        KeyCode::Esc => { app.modal = Modal::None; }
        KeyCode::Char(c) if c.is_ascii_digit() => { size.push(c); }
        KeyCode::Backspace => { size.pop(); }
        KeyCode::Enter => {
            let gb: u32 = size.trim().parse().unwrap_or(0);
            if gb == 0 { app.modal = Modal::Error("Size must be > 0".into()); return; }
            let path = path.clone();
            match resize_disk(&path, gb) {
                Err(e) => { app.modal = Modal::Error(e); }
                Ok(_)  => { app.modal = Modal::None; app.reload_disks(); }
            }
        }
        _ => {}
    }
}

fn on_clone_key(app: &mut App, code: KeyCode) {
    let Modal::Clone(ref mut name) = app.modal else { return };
    match code {
        KeyCode::Esc => { app.modal = Modal::None; }
        KeyCode::Char(c) if c != '/' && c != ' ' => { name.push(c); }
        KeyCode::Backspace => { name.pop(); }
        KeyCode::Enter => {
            let new_name = match &app.modal { Modal::Clone(n) => n.trim().to_string(), _ => return };
            if new_name.is_empty() { return; }
            let src = match app.selected_vm().or_else(|| app.view_vm()) {
                Some(v) => v.clone(),
                None => { app.modal = Modal::Error("No VM selected".into()); return; }
            };
            let mut new_vm = src.clone();
            new_vm.name = new_name.clone();
            if !src.disk.is_empty() && Path::new(&src.disk).exists() {
                let new_disk = clone_disk_path(&src.disk, &new_name);
                match linked_clone(&src.disk, &new_disk) {
                    Err(e) => { app.modal = Modal::Error(e); return; }
                    Ok(_)  => { new_vm.disk = new_disk; }
                }
            }
            match save_vm(&new_vm) {
                Err(e) => { app.modal = Modal::Error(e.to_string()); }
                Ok(_)  => {
                    app.modal = Modal::None;
                    app.reload();
                    if let Some(i) = app.vms.iter().position(|v| v.name == new_name) {
                        app.vm_ts.select(Some(i));
                    }
                    app.screen = Screen::List;
                }
            }
        }
        _ => {}
    }
}

fn on_create_snap_key(app: &mut App, code: KeyCode) {
    let Modal::CreateSnap(ref mut name) = app.modal else { return };
    match code {
        KeyCode::Esc => { app.modal = Modal::None; }
        KeyCode::Char(c) => { name.push(c); }
        KeyCode::Backspace => { name.pop(); }
        KeyCode::Enter => {
            let snap_name = match &app.modal { Modal::CreateSnap(n) => n.trim().to_string(), _ => return };
            if snap_name.is_empty() { return; }
            let disk = app.view_vm().map(|v| v.disk.clone()).unwrap_or_default();
            match snap_create(&disk, &snap_name) {
                Err(e) => { app.modal = Modal::Error(e); }
                Ok(_)  => {
                    app.modal = Modal::None;
                    app.snaps = load_snaps(&disk);
                    if !app.snaps.is_empty() { app.snap_ts.select(Some(app.snaps.len()-1)); }
                }
            }
        }
        _ => {}
    }
}

fn on_rename_disk_key(app: &mut App, code: KeyCode) {
    let Modal::RenameDisk { path: _, ref mut name } = app.modal else { return };
    match code {
        KeyCode::Esc => { app.modal = Modal::None; }
        KeyCode::Char(c) => { name.push(c); }
        KeyCode::Backspace => { name.pop(); }
        KeyCode::Enter => {
            let new_name = match &app.modal { Modal::RenameDisk { name, .. } => name.trim().to_string(), _ => return };
            if new_name.is_empty() { return; }
            let old_path = match &app.modal { Modal::RenameDisk { path, .. } => path.clone(), _ => return };
            let new_path = Path::new(&old_path).parent()
                .map(|p| p.join(&new_name).to_string_lossy().into_owned())
                .unwrap_or(new_name.clone());
            match fs::rename(&old_path, &new_path) {
                Err(e) => { app.modal = Modal::Error(e.to_string()); }
                Ok(_)  => {
                    // Update any VM configs that referenced the old path
                    let vms: Vec<Vm> = app.vms.iter().map(|v| {
                        let mut v = v.clone();
                        let fix = |s: &str| if s == old_path { new_path.clone() } else { s.to_string() };
                        v.disk  = fix(&v.disk);
                        v.hdb   = fix(&v.hdb);
                        v.hdc   = fix(&v.hdc);
                        v.cdrom = fix(&v.cdrom);
                        v.fda   = fix(&v.fda);
                        v.fdb   = fix(&v.fdb);
                        v
                    }).collect();
                    for vm in &vms { let _ = save_vm(vm); }
                    app.modal = Modal::None;
                    app.reload();
                }
            }
        }
        _ => {}
    }
}

fn do_save(app: &mut App) {
    match app.form.to_vm() {
        Err(e) => { app.form.error = e; }
        Ok(vm) => {
            if app.form.is_edit && app.form.orig_name != vm.name {
                let _ = remove_vm_config(&app.form.orig_name);
            }
            match save_vm(&vm) {
                Err(e) => { app.form.error = e.to_string(); }
                Ok(_)  => {
                    let saved = vm.name.clone();
                    app.screen = Screen::List;
                    app.reload();
                    if let Some(i) = app.vms.iter().position(|v| v.name == saved) {
                        app.vm_ts.select(Some(i));
                    }
                }
            }
        }
    }
}

// ── Main ──────────────────────────────────────────────────────────────────────

fn main() -> io::Result<()> {
    enable_raw_mode()?;
    let mut stdout = io::stdout();
    execute!(stdout, EnterAlternateScreen)?;
    let mut terminal = Terminal::new(CrosstermBackend::new(stdout))?;
    let mut app = App::new();

    loop {
        terminal.draw(|f| draw(f, &mut app))?;
        if event::poll(Duration::from_millis(500))? {
            if let Event::Key(k) = event::read()? {
                if on_key(&mut app, k.code, k.modifiers) { break; }
            }
        }
    }

    disable_raw_mode()?;
    execute!(terminal.backend_mut(), LeaveAlternateScreen)?;
    Ok(())
}
