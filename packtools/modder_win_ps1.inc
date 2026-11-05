// modder_win_ps1.inc - the two PowerShell helpers of universal-modder (um/ps1, Rehan and universal-modder contributors, MIT),
// embedded verbatim: `um win` copies them to %LOCALAPPDATA%\\universal-modder\\tools before it runs them.
// Generated from upstream commit 9811709; do not edit by hand.
namespace packtools::modder::win {
inline const std::string& windrive_ps1() {
    static const std::string s = std::string(R"PS1(<#
  WinDrive: drive ONE game window with a line-per-command stdin protocol (one reply line each).
  No build step: Windows PowerShell 5.1 compiles the embedded C# on start (~1-2 s).

    powershell -NoProfile -ExecutionPolicy Bypass -File WinDrive.ps1 -Proc AoE2DE_s

  Commands (coordinates are CLIENT coordinates of the game window):
    focus                      bring the window to the foreground            -> ok | fail
    rect                       client area in screen coords                   -> x y w h | none
    title                      window title
    fg                         name of the foreground process
    move <x> <y>               cursor to client coords
    click <x> <y> [right]      click (left by default)
    mdown <x> <y> [right] / mup [right]    press / release a mouse button
    drag <x0> <y0> <x1> <y1>   left-drag (box select)
    rel <dx> <dy>              relative mouse move (FPS cameras / raw input)
    key <vk> [tap|down|up]     virtual-key (hex like 0x1B or decimal)
    hold <vk> <ms>             hold a key for ms (movement)
    type <text>                unicode text
    wheel <delta>              mouse wheel (120 = one notch up)
    scanmode on|off            send hardware scan codes (DirectInput / raw-input games ignore VK-only events)
    size <w> <h>               resize the window so its CLIENT area is w x h (windowed mode)
    untop                      undo the topmost flag focus may set
    sleep <ms>                 wait before the next command (timed combos in one call)
    idle                       seconds since the last real user input on this machine (doesn't wait)
  Safety: input is only sent while the target window is the foreground window (or nothing is, and the
  cursor is over the game), so keys never leak into other apps. Close stdin to quit.
#>
param([Parameter(Mandatory = $true)][string]$Proc)

$src = @"
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;

public static class WinDrive
{
    [StructLayout(LayoutKind.Sequential)] struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT { public int dx, dy; public uint data, flags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Explicit)] struct U { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
    [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public U u; }
    [StructLayout(LayoutKind.Sequential)] struct LASTINPUTINFO { public uint size, time; }

    [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] i, int size);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern bool GetCursorPos(out POINT p);
    [DllImport("user32.dll")] static extern bool AttachThreadInput(uint a, uint b, bool attach);
    [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    [DllImport("user32.dll")] static extern bool GetLastInputInfo(ref LASTINPUTINFO i);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint type);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(POINT p);
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr h, uint flags);

    static string proc;
    static bool scanMode = false;

    static IntPtr Window()
    {
        foreach (var p in Process.GetProcessesByName(proc))
            if (p.MainWindowHandle != IntPtr.Zero) return p.MainWindowHandle;
        return IntPtr.Zero;
    }

    static string ProcName(IntPtr h)
    {
        uint pid;
        GetWindowThreadProcessId(h, out pid);
        try { return Process.GetProcessById((int)pid).ProcessName; } catch { return "?"; }
    }

    static bool IsForeground()
    {
        var fg = GetForegroundWindow();
        return fg != IntPtr.Zero && ProcName(fg).Equals(proc, StringComparison.OrdinalIgnoreCase);
    }

    static INPUT Mouse(uint flags, uint data, int dx, int dy)
    {
        var i = new INPUT(); i.type = 0; i.u.mi.flags = flags; i.u.mi.data = data; i.u.mi.dx = dx; i.u.mi.dy = dy; return i;
    }

    static INPUT Key(ushort vk, bool up)
    {
        var i = new INPUT(); i.type = 1;
        ushort sc = (ushort)MapVirtualKey(vk, 0);
        bool ext = vk == 0x25 || vk == 0x26 || vk == 0x27 || vk == 0x28 || vk == 0x2D || vk == 0x2E || vk == 0x24 || vk == 0x23 || vk == 0x21 || vk == 0x22 || vk == 0xA3 || vk == 0xA5;
        if (scanMode) { i.u.ki.vk = 0; i.u.ki.scan = sc; i.u.ki.flags = 0x0008u | (up ? 2u : 0u) | (ext ? 1u : 0u); }
        else { i.u.ki.vk = vk; i.u.ki.scan = sc; i.u.ki.flags = (up ? 2u : 0u) | (ext ? 1u : 0u); }
        return i;
    }

    static bool Focus()
    {
        var h = Window();
        if (h == IntPtr.Zero) return false;
        if (IsForeground()) return true;
        keybd_event(0x12, 0, 0, IntPtr.Zero); keybd_event(0x12, 0, 2, IntPtr.Zero); // alt tap: )PS1"
        R"PS1(allowed to take the foreground
        uint dummy;
        uint target = GetWindowThreadProcessId(h, out dummy), me = GetCurrentThreadId();
        AttachThreadInput(me, target, true);
        ShowWindow(h, 9);
        SetForegroundWindow(h);
        AttachThreadInput(me, target, false);
        Thread.Sleep(300);
        if (IsForeground()) return true;
        // Foreground lock: a real click activates the window - only if the point really is on our window.
        SetWindowPos(h, new IntPtr(-1), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040);
        RECT r; GetClientRect(h, out r);
        var p = new POINT(); p.X = (r.R - r.L) / 2; p.Y = 4;
        ClientToScreen(h, ref p);
        var under = WindowFromPoint(p);
        if (GetAncestor(under, 2) != GetAncestor(h, 2) && under != h) return false;
        SetCursorPos(p.X, p.Y);
        SendInput(1, new INPUT[] { Mouse(2u, 0, 0, 0) }, Marshal.SizeOf(typeof(INPUT)));
        SendInput(1, new INPUT[] { Mouse(4u, 0, 0, 0) }, Marshal.SizeOf(typeof(INPUT)));
        Thread.Sleep(300);
        return IsForeground();
    }

    // Safe if the game is the foreground window, or if nothing is (windowed games can drop the
    // foreground on clicks) and the cursor is over the game - then input can only reach the game.
    static bool Safe()
    {
        if (IsForeground()) return true;
        if (GetForegroundWindow() != IntPtr.Zero) return false;
        POINT p; GetCursorPos(out p);
        var under = WindowFromPoint(p);
        var h = Window();
        return under == h || GetAncestor(under, 2) == GetAncestor(h, 2);
    }

    static void Send(params INPUT[] i)
    {
        if (!Safe()) throw new InvalidOperationException("target window is not in the foreground (foreground: " + ProcName(GetForegroundWindow()) + ")");
        SendInput((uint)i.Length, i, Marshal.SizeOf(typeof(INPUT)));
    }

    static void MoveTo(int x, int y)
    {
        var p = new POINT(); p.X = x; p.Y = y;
        ClientToScreen(Window(), ref p);
        SetCursorPos(p.X, p.Y);
    }

    static ushort Vk(string s) { return s.StartsWith("0x") ? Convert.ToUInt16(s, 16) : ushort.Parse(s); }

    static string Do(string line)
    {
        var a = line.Trim().Split(new char[] { ' ' }, 2);
        var rest = a.Length > 1 ? a[1] : "";
        var n = rest.Split(new char[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
        switch (a[0])
        {
            case "focus": return Focus() ? "ok" : "fail";
            case "fg": return ProcName(GetForegroundWindow());
            case "title": { var h = Window(); if (h == IntPtr.Zero) return "none"; foreach (var p in Process.GetProcessesByName(proc)) if (p.MainWindowHandle == h) return p.MainWindowTitle; return ""; }
            case "untop": SetWindowPos(Window(), new IntPtr(-2), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040); return "ok";
            case "rect":
            {
                var h = Window();
                if (h == IntPtr.Zero) return "none";
                RECT r; GetClientRect(h, out r);
                var p = new POINT(); ClientToScreen(h, ref p);
                return p.X + " " + p.Y + " " + (r.R - r.L) + " " + (r.B - r.T);
            }
            case "size":
            {
                var h = Window();
                RECT wr, cr; GetWindowRect(h, out wr); GetClientRect(h, out cr);
                int bw = (wr.R - wr.L) - (cr.R - cr.L), bh = (wr.B - wr.T) - (cr.B - cr.T);
                SetWindowPos(h, IntPtr.Zero, 0, 0, int.Parse(n[0]) + bw, int.Parse(n[1]) + bh, 0x0002 | 0x0004);
                return "ok";
            }
            case "move": MoveTo(int.Parse(n[0]), int.Parse(n[1])); return "ok";
            case "click":
            {
                MoveTo(int.Parse(n[0]), int.Parse(n[1]));
                Thread.Sleep(40);
                bool right = n.Length > 2 && n[2] == "right";
                Send(Mouse(right ? 8u : 2u, 0, 0, 0)); Thread.Sleep(60); Send(Mouse(right ? 16u : 4u, 0, 0, 0));
                return "ok";
            }
            case "mdown": { MoveTo(int.Parse(n[0]), int.Parse(n[1])); Thread.Sleep(30); Send(Mouse(n.Length > 2 && n[2] == "right" ? 8u : 2u, 0, 0, 0)); return "ok"; }
            case "mup": { Send(Mouse(n.Length > 0 && n[0] == "right" ? 16u : 4u, 0, 0, 0)); return "ok"; }
            case "rel": { Send(Mouse(0x0001, 0, int.Parse(n[0]), int.Parse(n[1]))); return "ok"; }
            case "drag":
            {
                int x0 = int.Parse(n[0]), y0 = int.Parse(n[1]), x1 = int.Parse(n[2]), y1 = int.Parse(n[3]);
                MoveTo(x0, y0); Thread.Sleep(40);
                Send(Mouse(2u, 0, 0, 0)); Thread.Sleep(80);
                for (int k = 1; k <= 10; k++) { MoveTo(x0 + (x1 - x0) * k / 10, y0 + (y1 - y0) * k / 10); Thread.Sleep(20); }
                Send(Mouse(4u, 0, 0, 0));
                return "ok";
            }
            case "key":
            {
                var vk = Vk(n[0]);
                string mode = n.Length > 1 ? n[1] : "tap";
                if (mode != "up") Send(Key(vk, false));
                if (mode == "tap") Thread.Sleep(50);
                if (mode != "down") Send(Key(vk, true));
                return "ok";
            }
            case "hold": { var vk = Vk(n[0]); Send(Key(vk, false)); Thread.Sleep(int.Parse(n[1])); Send(Key(vk, true)); return "ok"; }
            case "scanmode": scanMode = n.Length > 0 && n[0] == "on"; return "ok";
            case "type":
                foreach (char c in rest)
                {
                    var d = new INPUT(); d.type = 1; d.u.ki.scan = c; d.u.ki.flags = 4;
                    var u = new INPUT(); u.type = 1; u.u.ki.scan = c; u.u.ki.flags = 4 | 2;
                    Send(d, u);
                    Thread.Sleep(10);
                }
                return "ok";
            case "wheel": Send(Mouse(0x0800, unchecked((uint)int.Parse(n[0])), 0, 0)); return "ok";
            case "sleep": Thread.Sleep(Math.Max(0, int.Parse(n[0]))); return "ok";   // ms; -1 would be Infi)PS1"
        R"PS1(nite and hang
            case "idle":
            {
                var li = new LASTINPUTINFO(); li.size = (uint)Marshal.SizeOf(typeof(LASTINPUTINFO));
                GetLastInputInfo(ref li);
                return (unchecked((uint)Environment.TickCount - li.time) / 1000.0).ToString("F1", System.Globalization.CultureInfo.InvariantCulture);
            }
            default: return "error unknown " + a[0];
        }
    }

    public static void Run(string p)
    {
        proc = p.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) ? p.Substring(0, p.Length - 4) : p;
        // UTF-8 both ways, like um.common.text_utf8() on the Python side: window titles and "type" text are
        // not ASCII, and Console.In/Out would use the console's code page (OEM, e.g. 936 on a Chinese Windows).
        var utf8 = new System.Text.UTF8Encoding(false);
        var stdout = new System.IO.StreamWriter(Console.OpenStandardOutput(), utf8);
        var stdin = new System.IO.StreamReader(Console.OpenStandardInput(), utf8);
        stdout.WriteLine("ready " + (Window() != IntPtr.Zero ? "window" : "no-window"));
        stdout.Flush();
        string line;
        while ((line = stdin.ReadLine()) != null)
        {
            if (line.Trim().Length == 0) continue;
            string reply;
            try { reply = Do(line); } catch (Exception e) { reply = "error " + e.Message; }
            stdout.WriteLine(reply);
            stdout.Flush();
        }
    }
}
"@

Add-Type -TypeDefinition $src -Language CSharp
[WinDrive]::Run($Proc)
)PS1");
    return s;
}
inline const std::string& procloopback_ps1() {
    static const std::string s = std::string(R"PS1(<#
  ProcLoopback: record ONE process's audio (WASAPI process loopback, Windows 10 2004+) to raw float32
  stereo 48 kHz - a game capture with only the game's sound, nothing else playing on the machine.
  No build step: Windows PowerShell 5.1 compiles the embedded C# on start.

    powershell -NoProfile -ExecutionPolicy Bypass -File ProcLoopback.ps1 -TargetPid 1234 -Out C:\caps\take1.audio.raw

  Prints one JSON line when capturing: {"start_hns": <QPC in 100 ns>, "rate": 48000, "channels": 2, "format": "f32le"}.
  Sample 0 of the file is start_hns; packets are placed by their QPC timestamps and gaps are filled with
  silence, so file position = wall time. Stops when stdin gets a line / closes, or the process exits.
  (Many games - FNA/FAudio, Unity, Unreal - talk to WASAPI directly, so SDL/driver tricks can't redirect them.)
#>
param([Parameter(Mandatory = $true)][int]$TargetPid, [Parameter(Mandatory = $true)][string]$Out)

$src = @"
using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

public static class ProcLoopback
{
    const int Rate = 48000, Channels = 2, BytesPerFrame = 8;

    [StructLayout(LayoutKind.Sequential)]
    struct WaveFormatEx { public ushort Tag, Channels; public uint SamplesPerSec, AvgBytesPerSec; public ushort BlockAlign, BitsPerSample, Size; }

    [StructLayout(LayoutKind.Sequential)]
    struct ActivationParams { public int ActivationType; public uint TargetProcessId; public int LoopbackMode; }

    [StructLayout(LayoutKind.Sequential)]
    struct PropVariantBlob { public ushort Vt, R1, R2, R3; public uint Size; public IntPtr Data; }

    [ComImport, Guid("72A22D78-CDE4-431D-B8CC-843A71199B6D"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IActivateAudioInterfaceAsyncOperation
    {
        void GetActivateResult(out int result, [MarshalAs(UnmanagedType.IUnknown)] out object iface);
    }

    [ComImport, Guid("41D949AB-9862-444A-80F6-C261334DA5EB"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IActivateAudioInterfaceCompletionHandler
    {
        void ActivateCompleted(IActivateAudioInterfaceAsyncOperation op);
    }

    [ComImport, Guid("94ea2b94-e9cc-49e0-c0ff-ee64ca8f5b90"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAgileObject { }

    [ComImport, Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioClient
    {
        [PreserveSig] int Initialize(int shareMode, uint flags, long bufferDuration, long periodicity, ref WaveFormatEx format, IntPtr session);
        [PreserveSig] int GetBufferSize(out uint frames);
        [PreserveSig] int GetStreamLatency(out long latency);
        [PreserveSig] int GetCurrentPadding(out uint padding);
        [PreserveSig] int IsFormatSupported(int shareMode, IntPtr format, IntPtr closest);
        [PreserveSig] int GetMixFormat(out IntPtr format);
        [PreserveSig] int GetDevicePeriod(out long def, out long min);
        [PreserveSig] int Start();
        [PreserveSig] int Stop();
        [PreserveSig] int Reset();
        [PreserveSig] int SetEventHandle(IntPtr handle);
        [PreserveSig] int GetService([MarshalAs(UnmanagedType.LPStruct)] Guid iid, [MarshalAs(UnmanagedType.IUnknown)] out object service);
    }

    [ComImport, Guid("C8ADBD64-E71E-48a0-A4DE-185C395CD317"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioCaptureClient
    {
        [PreserveSig] int GetBuffer(out IntPtr data, out uint frames, out uint flags, out ulong devicePosition, out ulong qpcPosition);
        [PreserveSig] int ReleaseBuffer(uint frames);
        [PreserveSig] int GetNextPacketSize(out uint frames);
    }

    [DllImport("Mmdevapi.dll", ExactSpelling = true, PreserveSig = false)]
    static extern void ActivateAudioInterfaceAsync([MarshalAs(UnmanagedType.LPWStr)] string path, [MarshalAs(UnmanagedType.LPStruct)] Guid iid,
        IntPtr activationParams, IActivateAudioInterfaceCompletionHandler handler, out IActivateAudioInterfaceAsyncOperation op);

    sealed class Handler : IActivateAudioInterfaceCompletionHandler, IAgileObject
    {
        public readonly ManualResetEvent Done = new ManualResetEvent(false);
        public void ActivateCompleted(IActivateAudioInterfaceAsyncOperation op) { Done.Set(); }
    }

    static long NowHns() { return (long)(Stopwatch.GetTimestamp() * (10000000.0 / Stopwatch.Frequency)); }

    static volatile bool stop;
    static int exitCode;

    public static int Run(int pid, string outPath)
    {
        // COM activation + capture on an MTA thread (PowerShell's own thread is STA)
        var t = new Thread(delegate() { exitCode = Capture(pid, outPath); });
        t.SetApartmentState(ApartmentState.MTA);
        t.Start();
        var reader = new Thread(delegate() { try { Console.In.ReadLine(); } catch { } stop = true; });
        reader.IsBackground = true;
        reader.Start();
        t.Join();
        return exitCode;
    }

    static int Capture(int pid, string outPath)
    {
        var ap = new ActivationParams(); ap.ActivationType = 1; ap.TargetProcessId = (uint)pid; ap.LoopbackMode = 0; // include the process tree
        IntPtr apPtr = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(ActivationParams)));
        Marshal.StructureToPtr(ap, apPtr, false);
        var pv = new PropVariantBlob(); pv.Vt = 65; pv.Size = (uint)Marshal.SizeOf(typeof(ActivationParams)); pv.Data = apPtr;
        IntPtr pvPtr = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(PropVariantBlob)));
        Marshal.StructureToPtr(pv, pvPtr, false);

        var handler = new Handler();
        IActivateAudioInterfaceAsyncOperation op;
        ActivateAudioInterfaceAsync("VAD\\Process_Loopback", typeof(IAudioClient).GUID, pvPtr, handler, out op);
        if (!handler.Done.WaitOne(10000)) { Console.Error.WriteLine("activation timed out"); return 1; }
        int hr; object iface;
        op.GetActivate)PS1"
        R"PS1(Result(out hr, out iface);
        if (hr != 0) { Console.Error.WriteLine("activation failed 0x" + hr.ToString("X8")); return 1; }
        var client = (IAudioClient)iface;

        var fmt = new WaveFormatEx(); fmt.Tag = 3; fmt.Channels = Channels; fmt.SamplesPerSec = Rate; fmt.BitsPerSample = 32;
        fmt.BlockAlign = BytesPerFrame; fmt.AvgBytesPerSec = Rate * BytesPerFrame;
        const uint LOOPBACK = 0x00020000, EVENTCALLBACK = 0x00040000, AUTOCONVERTPCM = 0x80000000;
        hr = client.Initialize(0, LOOPBACK | EVENTCALLBACK | AUTOCONVERTPCM, 2000000, 0, ref fmt, IntPtr.Zero);
        if (hr != 0) { Console.Error.WriteLine("Initialize failed 0x" + hr.ToString("X8")); return 1; }
        var ready = new AutoResetEvent(false);
        client.SetEventHandle(ready.SafeWaitHandle.DangerousGetHandle());
        object svc;
        client.GetService(typeof(IAudioCaptureClient).GUID, out svc);
        var capture = (IAudioCaptureClient)svc;

        using (var file = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.Read))
        {
            long startHns = NowHns(), written = 0;
            Console.Out.WriteLine("{\"start_hns\": " + startHns + ", \"rate\": " + Rate + ", \"channels\": " + Channels + ", \"format\": \"f32le\"}");
            Console.Out.Flush();
            client.Start();
            Process target = Process.GetProcessById(pid);
            byte[] buf = new byte[Rate * BytesPerFrame];
            while (!stop && !target.HasExited)
            {
                ready.WaitOne(100);
                uint n;
                while (capture.GetNextPacketSize(out n) == 0 && n > 0)
                {
                    IntPtr data; uint frames, flags; ulong devPos, qpc;
                    capture.GetBuffer(out data, out frames, out flags, out devPos, out qpc);
                    long at = ((long)qpc - startHns) * Rate / 10000000;
                    if (at > written + Rate / 100) { WriteSilence(file, at - written, buf); written = at; }
                    int bytes = (int)frames * BytesPerFrame;
                    if (bytes > buf.Length) buf = new byte[bytes];
                    if ((flags & 2) != 0) Array.Clear(buf, 0, bytes); else Marshal.Copy(data, buf, 0, bytes);
                    file.Write(buf, 0, bytes);
                    written += frames;
                    capture.ReleaseBuffer(frames);
                }
            }
            long end = (NowHns() - startHns) * Rate / 10000000;
            if (end > written) WriteSilence(file, end - written, new byte[Rate * BytesPerFrame]);
            client.Stop();
        }
        return 0;
    }

    static void WriteSilence(Stream s, long frames, byte[] buf)
    {
        Array.Clear(buf, 0, buf.Length);
        while (frames > 0)
        {
            int n = (int)Math.Min(frames, buf.Length / BytesPerFrame);
            s.Write(buf, 0, n * BytesPerFrame);
            frames -= n;
        }
    }
}
"@

Add-Type -TypeDefinition $src -Language CSharp
exit [ProcLoopback]::Run($TargetPid, $Out)
)PS1");
    return s;
}
}  // namespace packtools::modder::win
