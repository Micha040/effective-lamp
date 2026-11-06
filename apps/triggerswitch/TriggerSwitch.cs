// =====================================================================================
//  TriggerSwitch – schaltet per Hotkey die Belegung des Multifunktions-Triggers der
//  Razer Basilisk V3 Pro um (Standard: Leertaste-Turbo <-> Rechtsklick-Turbo, 20/s).
// -------------------------------------------------------------------------------------
//  Ablauf:  Hotkey (RegisterHotKey – kein Tastatur-Hook) -> Maus in den Normalmodus
//           (sie führt ihre Belegung selbst aus) -> Trigger im Arbeitsprofil 0 der Maus
//           umschreiben, mit demselben Befehl wie Synapse. Es werden keine Eingaben
//           simuliert und nichts in Spiele injiziert. Das gespeicherte Synapse-Profil
//           bleibt unberührt (Schreiben dorthin ist im Code gesperrt).
//  Wächter: prüft alle paar Sekunden Gerätemodus + Trigger und stellt sie wieder her,
//           falls Synapse, Standby oder Neuverbinden sie zurückgesetzt haben.
//  Overlay: kleine Karte in einer Bildschirmecke, nimmt nie den Fokus.
//  Akku:    Ladestand + Laden werden nur gelesen (07:80 / 07:84, wie OpenRazer) und im Overlay angezeigt.
//
//  Aufrufe: TriggerSwitch.exe                 normaler Start (Autostart)
//           TriggerSwitch.exe --stop          laufende Instanz beenden
//           TriggerSwitch.exe --release       beenden + Maus an Synapse zurückgeben
//           TriggerSwitch.exe --overlay-test  Overlays ansehen, ohne die Maus anzufassen
//
//  Setup.ps1 kompiliert diese Datei mit dem C#-Compiler von .NET Framework 4.x (C# 5).
//  Deshalb bewusst ohne neuere Sprachfeatures ($"...", ?., =>-Member, out var, ...).
// =====================================================================================

using System;
using Microsoft.Win32.SafeHandles;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

[assembly: System.Runtime.Versioning.TargetFramework(".NETFramework,Version=v4.8", FrameworkDisplayName = ".NET Framework 4.8")]

namespace TriggerSwitch
{
    // #################################################################################
    // Programmstart, unsichtbares Nachrichtenfenster (Hotkey, Geräte-/Energie-Ereignisse) und Hintergrund-Thread für die Maus.
    // #################################################################################

    public static class Program
    {
        public const string Version = "1.1.0";
        const string MutexName = @"Local\TriggerSwitch.SingleInstance";
        const string StopEventName = @"Local\TriggerSwitch.Stop";

        // Starke Referenzen: Fenster dürfen nicht vom GC eingesammelt werden, solange sie Nachrichten bekommen.
        static MessageWindow _ui;
        static OverlayWindow _overlay;

        [STAThread]
        public static int Main(string[] args)
        {
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;

            // "TriggerSwitch.exe --stop": laufende Instanz beenden (Maus bleibt, wie sie ist) – für Updates/Neustart.
            if (HasArg(args, "--stop")) return SignalStop() ? 0 : 1;

            Win32.EnableDpiAwareness();
            Log.Init(Path.Combine(baseDir, "TriggerSwitch.log"));
            AppDomain.CurrentDomain.UnhandledException += delegate(object sender, UnhandledExceptionEventArgs e)
            {
                Log.Error("Unbehandelte Ausnahme: " + e.ExceptionObject);
            };
            Application.ThreadException += delegate(object sender, ThreadExceptionEventArgs e)
            {
                Log.Error("Fehler im Fenster-Thread: " + e.Exception);
            };

            AppConfig cfg = AppConfig.Load(Path.Combine(baseDir, "config.json"));
            Log.VerboseEnabled = cfg.VerboseLog;

            try
            {
                // "--overlay-test": zeigt alle Overlays nacheinander, ohne die Maus anzufassen.
                if (HasArg(args, "--overlay-test")) return OverlayTest(cfg);
                // "--release": Maus an Synapse zurückgeben (beendet vorher eine laufende Instanz).
                if (HasArg(args, "--release")) return Release(baseDir, cfg);
            }
            catch (Exception ex)
            {
                Log.Error("Unerwarteter Fehler: " + ex);
                return 1;
            }

            bool createdNew;
            Mutex mutex = new Mutex(true, MutexName, out createdNew);
            if (!createdNew)
            {
                Log.Info("TriggerSwitch läuft bereits – diese Instanz beendet sich wieder.");
                return 0;
            }
            try
            {
                return Run(baseDir, cfg);
            }
            catch (Exception ex)
            {
                Log.Error("Unerwarteter Fehler: " + ex);
                return 1;
            }
            finally
            {
                GC.KeepAlive(mutex);
            }
        }

        static int Run(string baseDir, AppConfig cfg)
        {
            foreach (string w in cfg.Warnings) Log.Warn("config.json: " + w);
            string statePath = Path.Combine(baseDir, "state.json");
            AppState state = AppState.Load(statePath, cfg.Modes);

            List<string> names = new List<string>();
            foreach (ModeDef m in cfg.Modes) names.Add(m.Name);
            Log.Info("TriggerSwitch " + Version + " gestartet. Hotkey: " + cfg.Hotkey.Display + ", Modi: " + string.Join(" / ", names.ToArray()) +
                     ", aktuell: " + cfg.Modes[state.ModeIndex].Name + ".");

            SwitchController controller = new SwitchController(cfg.Modes, state, BasiliskSession.Open, delegate { state.Save(statePath); });
            Worker worker = new Worker(controller, cfg.WatchdogMs);

            using (EventWaitHandle stop = new EventWaitHandle(false, EventResetMode.ManualReset, StopEventName))
            {
                _ui = new MessageWindow();
                _overlay = cfg.OverlayEnabled ? new OverlayWindow(cfg.OverlaySeconds, cfg.OverlayCorner) : null;
                MessageWindow ui = _ui;

                worker.ToggleCompleted = delegate(SwitchOutcome o)
                {
                    ui.Post(delegate { ShowOverlay(ContentFor(o)); });
                };
                ui.HotkeyPressed = delegate { worker.RequestToggle(); };
                ui.DevicesChanged = delegate { worker.Nudge(1500, true); };
                ui.Resumed = delegate
                {
                    Log.Info("Windows ist aus dem Energiesparmodus zurück – prüfe die Maus.");
                    worker.Nudge(3000, true);
                };
                ui.SessionEnding = delegate
                {
                    Log.Info("Windows meldet ab bzw. fährt herunter.");
                    Application.ExitThread();
                };

                int error;
                if (ui.RegisterHotkey(cfg.Hotkey, out error))
                {
                    Log.Info("Hotkey " + cfg.Hotkey.Display + " ist aktiv.");
                }
                else
                {
                    string why = error == 1409 ? "ist schon von einem anderen Programm belegt" : "konnte nicht registriert werden (Fehler " + error + ")";
                    Log.Error("Hotkey " + cfg.Hotkey.Display + " " + why + " – bitte im Setup einen anderen wählen.");
                    OverlayContent c = new OverlayContent();
                    c.Title = "Hotkey belegt";
                    c.Subtitle = cfg.Hotkey.Display + " – im Setup ändern";
                    c.Icon = OverlayIcon.Warning;
                    c.Warn = true;
                    ShowOverlay(c);
                }

                Thread stopWatcher = new Thread(delegate()
                {
                    stop.WaitOne();
                    ui.Post(delegate
                    {
                        Log.Info("Stop-Signal erhalten.");
                        Application.ExitThread();
                    });
                });
                stopWatcher.IsBackground = true;
                stopWatcher.Start();

                worker.Start();
                Application.Run();

                worker.Stop(4000);
                ui.UnregisterHotkey();
                if (_overlay != null) _overlay.Dispose();
                ui.DestroyHandle();
            }
            Log.Info("Beendet.");
            return 0;
        }

        static void ShowOverlay(OverlayContent content)
        {
            if (_overlay == null) return;
            try
            {
                _overlay.Show(content);
            }
            catch (Exception ex)
            {
                Log.Warn("Overlay konnte nicht angezeigt werden: " + ex.Message);
            }
        }

        public static OverlayContent ContentFor(SwitchOutcome o)
        {
            OverlayContent c = o.Mode.ToOverlay();
            if (o.Applied && o.Battery != null)
            {
                c.BatteryPercent = o.Battery.Percent;
                c.BatteryCharging = o.Battery.Charging;
            }
            if (o.Applied || !o.Problem.HasValue) return c;
            c.Warn = true;
            switch (o.Problem.Value)
            {
                case MouseProblem.Asleep: c.Subtitle = "Maus schläft – wird beim Aufwachen gesetzt"; break;
                case MouseProblem.NotFound:
                case MouseProblem.Disconnected: c.Subtitle = "Maus nicht verbunden – wird nachgeholt"; break;
                default: c.Subtitle = "Noch nicht übernommen – wird wiederholt"; break;
            }
            return c;
        }

        // ------------------------------------------------------------ --overlay-test

        static int OverlayTest(AppConfig cfg)
        {
            List<OverlayContent> list = new List<OverlayContent>();
            int[] samplePercent = { 82, 14, 100 };   // Beispielwerte – die Vorschau fasst die Maus nicht an
            for (int i = 0; i < cfg.Modes.Count; i++)
            {
                OverlayContent c = cfg.Modes[i].ToOverlay();
                c.BatteryPercent = samplePercent[i % samplePercent.Length];
                c.BatteryCharging = i % samplePercent.Length == 2;
                list.Add(c);
            }
            SwitchOutcome sample = new SwitchOutcome();
            sample.Mode = cfg.Modes[0];
            sample.Problem = MouseProblem.Asleep;
            list.Add(ContentFor(sample));

            _overlay = new OverlayWindow(cfg.OverlaySeconds, cfg.OverlayCorner);
            int index = 0;
            System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer();
            timer.Interval = (int)(cfg.OverlaySeconds * 1000) + 500;
            timer.Tick += delegate
            {
                index++;
                if (index < list.Count)
                {
                    ShowOverlay(list[index]);
                }
                else
                {
                    timer.Stop();
                    Application.ExitThread();
                }
            };
            ShowOverlay(list[0]);
            timer.Start();
            Application.Run();
            timer.Dispose();
            _overlay.Dispose();
            return 0;
        }

        // ------------------------------------------------------------ --release

        // Rückgabe: 0 = zurückgegeben, 2 = Maus nicht erreichbar (schläft/fehlt), 1 = Fehler
        static int Release(string baseDir, AppConfig cfg)
        {
            Log.Info("Maus an Synapse zurückgeben (vom Setup angefordert).");
            bool wasRunning = SignalStop();
            using (Mutex mutex = new Mutex(false, MutexName))
            {
                bool owned;
                try
                {
                    owned = mutex.WaitOne(wasRunning ? 10000 : 3000);
                }
                catch (AbandonedMutexException)
                {
                    owned = true;
                }
                if (!owned)
                {
                    Log.Error("Die laufende TriggerSwitch-Instanz hat sich nicht beendet – Abbruch.");
                    return 1;
                }
                try
                {
                    string statePath = Path.Combine(baseDir, "state.json");
                    AppState state = AppState.Load(statePath, cfg.Modes);
                    using (SwitchController controller = new SwitchController(cfg.Modes, state, BasiliskSession.Open, delegate { state.Save(statePath); }))
                    {
                        Stopwatch sw = Stopwatch.StartNew();
                        bool told = false;
                        while (true)
                        {
                            SwitchOutcome o = controller.Release();
                            if (o.Applied) return 0;
                            if (o.Problem == MouseProblem.Failed)
                            {
                                Log.Error("Zurückgeben fehlgeschlagen: " + o.Message);
                                return 1;
                            }
                            if (sw.ElapsedMilliseconds > 15000)
                            {
                                Log.Warn("Maus nicht erreichbar – nicht zurückgegeben (" + o.Message + "). Bitte bewegen bzw. einschalten und nochmal versuchen.");
                                return 2;
                            }
                            if (!told)
                            {
                                Log.Info("Maus antwortet nicht – bitte kurz bewegen …");
                                told = true;
                            }
                            Thread.Sleep(1000);
                        }
                    }
                }
                finally
                {
                    mutex.ReleaseMutex();
                }
            }
        }

        // ------------------------------------------------------------ Hilfen

        static bool HasArg(string[] args, string name)
        {
            foreach (string a in args)
            {
                if (string.Equals(a, name, StringComparison.OrdinalIgnoreCase)) return true;
            }
            return false;
        }

        static bool SignalStop()
        {
            try
            {
                using (EventWaitHandle ev = EventWaitHandle.OpenExisting(StopEventName))
                {
                    ev.Set();
                }
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }
    }

    // =================================================================================
    //  Unsichtbares Fenster: WM_HOTKEY, Geräte-/Energie-Ereignisse, Aufrufe aus anderen Threads
    // =================================================================================
    public sealed class MessageWindow : NativeWindow
    {
        const int HotkeyId = 0x5453;          // "TS"
        const int WM_QUERYENDSESSION = 0x0011;
        const int WM_ENDSESSION = 0x0016;
        const int WM_POWERBROADCAST = 0x0218;
        const int WM_DEVICECHANGE = 0x0219;
        const int WM_HOTKEY = 0x0312;
        const int WM_APP_RUN = 0x8001;
        const long DBT_DEVNODES_CHANGED = 0x0007;
        const long PBT_APMRESUMESUSPEND = 0x0007;
        const long PBT_APMRESUMEAUTOMATIC = 0x0012;

        readonly ConcurrentQueue<Action> _queue = new ConcurrentQueue<Action>();
        bool _hotkey;

        public Action HotkeyPressed;
        public Action DevicesChanged;
        public Action Resumed;
        public Action SessionEnding;

        public MessageWindow()
        {
            // Unsichtbares Fenster der obersten Ebene (nicht HWND_MESSAGE), damit Rundsendungen wie
            // WM_DEVICECHANGE/WM_POWERBROADCAST ankommen. Es wird nie angezeigt.
            CreateParams cp = new CreateParams();
            cp.Caption = "TriggerSwitch";
            cp.Style = Win32.WS_POPUP;
            cp.ExStyle = Win32.WS_EX_TOOLWINDOW;
            CreateHandle(cp);
        }

        public bool RegisterHotkey(HotkeyDef hotkey, out int error)
        {
            error = 0;
            _hotkey = Win32.RegisterHotKey(Handle, HotkeyId, hotkey.Modifiers | HotkeyDef.ModNoRepeat, hotkey.VirtualKey);
            if (!_hotkey) error = System.Runtime.InteropServices.Marshal.GetLastWin32Error();
            return _hotkey;
        }

        public void UnregisterHotkey()
        {
            if (_hotkey) Win32.UnregisterHotKey(Handle, HotkeyId);
            _hotkey = false;
        }

        // Aus beliebigem Thread: Aktion im Fenster-Thread ausführen
        public void Post(Action action)
        {
            _queue.Enqueue(action);
            IntPtr h = Handle;
            if (h != IntPtr.Zero) Win32.PostMessage(h, WM_APP_RUN, IntPtr.Zero, IntPtr.Zero);
        }

        protected override void WndProc(ref Message m)
        {
            switch (m.Msg)
            {
                case WM_HOTKEY:
                    if (m.WParam.ToInt64() == HotkeyId && HotkeyPressed != null) HotkeyPressed();
                    m.Result = IntPtr.Zero;
                    return;
                case WM_DEVICECHANGE:
                    if (m.WParam.ToInt64() == DBT_DEVNODES_CHANGED && DevicesChanged != null) DevicesChanged();
                    m.Result = new IntPtr(1);
                    return;
                case WM_POWERBROADCAST:
                    long ev = m.WParam.ToInt64();
                    if ((ev == PBT_APMRESUMEAUTOMATIC || ev == PBT_APMRESUMESUSPEND) && Resumed != null) Resumed();
                    m.Result = new IntPtr(1);
                    return;
                case WM_QUERYENDSESSION:
                    m.Result = new IntPtr(1);
                    return;
                case WM_ENDSESSION:
                    if (m.WParam != IntPtr.Zero && SessionEnding != null) SessionEnding();
                    m.Result = IntPtr.Zero;
                    return;
                case WM_APP_RUN:
                    Action a;
                    while (_queue.TryDequeue(out a))
                    {
                        try
                        {
                            a();
                        }
                        catch (Exception ex)
                        {
                            Log.Error("Fehler im Fenster-Thread: " + ex);
                        }
                    }
                    return;
            }
            base.WndProc(ref m);
        }
    }

    // =================================================================================
    //  Hintergrund-Thread: einziger Thread, der mit der Maus spricht
    // =================================================================================
    public sealed class Worker
    {
        readonly SwitchController _controller;
        readonly int _intervalMs;
        readonly AutoResetEvent _wake = new AutoResetEvent(false);
        readonly Thread _thread;
        int _pendingToggles;
        long _dueMs;
        volatile bool _stop;
        volatile bool _reopen;

        public Action<SwitchOutcome> ToggleCompleted;

        public Worker(SwitchController controller, int intervalMs)
        {
            _controller = controller;
            _intervalMs = intervalMs;
            _thread = new Thread(Run);
            _thread.IsBackground = true;
            _thread.Name = "TriggerSwitch-Maus";
        }

        static long NowMs()
        {
            return Stopwatch.GetTimestamp() * 1000 / Stopwatch.Frequency;
        }

        public void Start()
        {
            Interlocked.Exchange(ref _dueMs, NowMs());   // sofort einmal prüfen/anwenden
            _thread.Start();
        }

        public void RequestToggle()
        {
            Interlocked.Increment(ref _pendingToggles);
            _wake.Set();
        }

        // Nach Geräte-/Energieereignissen: in delayMs prüfen (statt erst im normalen Takt)
        public void Nudge(int delayMs, bool reopen)
        {
            if (reopen) _reopen = true;
            long due = NowMs() + delayMs;
            while (true)
            {
                long current = Interlocked.Read(ref _dueMs);
                if (current <= due) break;
                if (Interlocked.CompareExchange(ref _dueMs, due, current) == current) break;
            }
            _wake.Set();
        }

        public void Stop(int timeoutMs)
        {
            _stop = true;
            _wake.Set();
            if (_thread.IsAlive && !_thread.Join(timeoutMs)) Log.Warn("Maus-Thread hat sich nicht rechtzeitig beendet.");
        }

        void Run()
        {
            while (!_stop)
            {
                int toggles = Interlocked.Exchange(ref _pendingToggles, 0);
                if (toggles > 0)
                {
                    try
                    {
                        SwitchOutcome o = _controller.Toggle(toggles);
                        Action<SwitchOutcome> done = ToggleCompleted;
                        if (done != null) done(o);
                    }
                    catch (Exception ex)
                    {
                        Log.Error("Fehler beim Umschalten: " + ex);
                    }
                    Interlocked.Exchange(ref _dueMs, NowMs() + _intervalMs);
                    continue;
                }

                long wait = Interlocked.Read(ref _dueMs) - NowMs();
                if (wait <= 0)
                {
                    try
                    {
                        if (_reopen)
                        {
                            _reopen = false;
                            _controller.Reopen();
                        }
                        _controller.Check();
                    }
                    catch (Exception ex)
                    {
                        Log.Error("Fehler bei der Prüfung: " + ex);
                    }
                    Interlocked.Exchange(ref _dueMs, NowMs() + _intervalMs);
                    continue;
                }
                _wake.WaitOne((int)Math.Min(wait, int.MaxValue));
            }
            try
            {
                _controller.Dispose();
            }
            catch (Exception)
            {
            }
        }
    }

    // #################################################################################
    // Einstellungen, Hotkey, gespeicherter Zustand und die eigentliche Umschalt-Logik (ohne Fenster -> testbar).
    // #################################################################################

    // =================================================================================
    //  Hotkey ("Ctrl+Alt+F11", auch deutsch: "Strg+Alt+F11")
    // =================================================================================
    public sealed class HotkeyDef
    {
        public const uint ModAlt = 0x1;
        public const uint ModControl = 0x2;
        public const uint ModShift = 0x4;
        public const uint ModWin = 0x8;
        public const uint ModNoRepeat = 0x4000;

        public uint Modifiers;
        public uint VirtualKey;

        sealed class KeyName
        {
            public uint Vk;
            public string Canonical;
            public string German;
            public KeyName(uint vk, string canonical, string german) { Vk = vk; Canonical = canonical; German = german; }
        }

        static readonly KeyName[] Named =
        {
            new KeyName(0x13, "Pause", "Pause"),
            new KeyName(0x91, "ScrollLock", "Rollen"),
            new KeyName(0x2D, "Insert", "Einfg"),
            new KeyName(0x2E, "Delete", "Entf"),
            new KeyName(0x24, "Home", "Pos1"),
            new KeyName(0x23, "End", "Ende"),
            new KeyName(0x21, "PageUp", "Bild auf"),
            new KeyName(0x22, "PageDown", "Bild ab"),
            new KeyName(0x26, "Up", "Pfeil hoch"),
            new KeyName(0x28, "Down", "Pfeil runter"),
            new KeyName(0x25, "Left", "Pfeil links"),
            new KeyName(0x27, "Right", "Pfeil rechts"),
            new KeyName(0x20, "Space", "Leertaste"),
            new KeyName(0x09, "Tab", "Tab"),
            new KeyName(0x0D, "Enter", "Enter"),
            new KeyName(0x1B, "Esc", "Esc"),
            new KeyName(0x08, "Backspace", "Rücktaste"),
            new KeyName(0x2C, "PrintScreen", "Druck"),
            new KeyName(0x6A, "Multiply", "Num *"),
            new KeyName(0x6B, "Add", "Num +"),
            new KeyName(0x6D, "Subtract", "Num -"),
            new KeyName(0x6E, "Decimal", "Num ,"),
            new KeyName(0x6F, "Divide", "Num /"),
            new KeyName(0x5D, "Apps", "Menütaste"),
        };

        static readonly string[][] Aliases =
        {
            new string[] { "scroll", "ScrollLock" }, new string[] { "rollen", "ScrollLock" },
            new string[] { "einfg", "Insert" }, new string[] { "ins", "Insert" },
            new string[] { "entf", "Delete" }, new string[] { "del", "Delete" },
            new string[] { "pos1", "Home" }, new string[] { "ende", "End" },
            new string[] { "bildauf", "PageUp" }, new string[] { "pgup", "PageUp" }, new string[] { "prior", "PageUp" },
            new string[] { "bildab", "PageDown" }, new string[] { "pgdn", "PageDown" }, new string[] { "next", "PageDown" },
            new string[] { "leertaste", "Space" }, new string[] { "return", "Enter" }, new string[] { "escape", "Esc" },
            new string[] { "druck", "PrintScreen" }, new string[] { "menu", "Apps" },
        };

        public static HotkeyDef Parse(string text)
        {
            if (text == null || text.Trim().Length == 0) throw new FormatException("Kein Hotkey angegeben.");
            string[] tokens = text.Split('+');
            HotkeyDef h = new HotkeyDef();
            for (int i = 0; i < tokens.Length; i++)
            {
                string t = tokens[i].Trim();
                if (t.Length == 0) throw new FormatException("\"" + text + "\" ist kein gültiger Hotkey.");
                uint mod = ModifierOf(t);
                if (i < tokens.Length - 1)
                {
                    if (mod == 0) throw new FormatException("\"" + t + "\" ist keine Zusatztaste (erlaubt: Ctrl/Strg, Alt, Shift/Umschalt, Win).");
                    h.Modifiers |= mod;
                    continue;
                }
                if (mod != 0) throw new FormatException("Im Hotkey \"" + text + "\" fehlt die eigentliche Taste.");
                h.VirtualKey = KeyOf(t);
                if (h.VirtualKey == 0) throw new FormatException("Unbekannte Taste \"" + t + "\".");
            }
            return h;
        }

        static uint ModifierOf(string t)
        {
            switch (t.ToLowerInvariant())
            {
                case "ctrl": case "strg": case "control": case "ctl": return ModControl;
                case "alt": return ModAlt;
                case "shift": case "umschalt": case "umsch": return ModShift;
                case "win": case "windows": return ModWin;
                default: return 0;
            }
        }

        static uint KeyOf(string t)
        {
            string lower = t.ToLowerInvariant().Replace(" ", "");
            foreach (string[] a in Aliases) if (a[0] == lower) { lower = a[1].ToLowerInvariant(); break; }
            foreach (KeyName k in Named) if (k.Canonical.ToLowerInvariant() == lower) return k.Vk;

            int n;
            if (lower.Length >= 2 && lower[0] == 'f' && int.TryParse(lower.Substring(1), NumberStyles.None, CultureInfo.InvariantCulture, out n) && n >= 1 && n <= 24)
            {
                return (uint)(0x70 + n - 1);
            }
            if (lower.Length == 1 && lower[0] >= 'a' && lower[0] <= 'z') return (uint)char.ToUpperInvariant(lower[0]);
            if (lower.Length == 1 && lower[0] >= '0' && lower[0] <= '9') return (uint)lower[0];
            foreach (string prefix in new string[] { "numpad", "num" })
            {
                if (lower.StartsWith(prefix) && lower.Length == prefix.Length + 1 && char.IsDigit(lower[lower.Length - 1]))
                {
                    return (uint)(0x60 + (lower[lower.Length - 1] - '0'));
                }
            }
            string hex = null;
            if (lower.StartsWith("0x")) hex = lower.Substring(2);
            else if (lower.StartsWith("vk")) hex = lower.Substring(2);
            uint vk;
            if (hex != null && hex.Length > 0 && hex.Length <= 2 &&
                uint.TryParse(hex, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out vk) && IsUsableVk(vk))
            {
                return vk;
            }
            return 0;
        }

        // Keine Maustasten, keine Zusatztasten als "eigentliche" Taste
        static bool IsUsableVk(uint vk)
        {
            if (vk < 0x08 || vk > 0xFE) return false;
            if (vk >= 0x10 && vk <= 0x12) return false;
            if (vk >= 0xA0 && vk <= 0xA5) return false;
            if (vk == 0x5B || vk == 0x5C || vk == 0x14) return false;
            return true;
        }

        public string Canonical
        {
            get { return Join(false); }
        }

        public string Display
        {
            get { return Join(true); }
        }

        string Join(bool german)
        {
            List<string> parts = new List<string>();
            if ((Modifiers & ModControl) != 0) parts.Add(german ? "Strg" : "Ctrl");
            if ((Modifiers & ModAlt) != 0) parts.Add("Alt");
            if ((Modifiers & ModShift) != 0) parts.Add(german ? "Umschalt" : "Shift");
            if ((Modifiers & ModWin) != 0) parts.Add("Win");
            parts.Add(KeyText(german));
            return string.Join("+", parts.ToArray());
        }

        string KeyText(bool german)
        {
            uint vk = VirtualKey;
            foreach (KeyName k in Named) if (k.Vk == vk) return german ? k.German : k.Canonical;
            if (vk >= 0x70 && vk <= 0x87) return "F" + (vk - 0x70 + 1);
            if ((vk >= 0x41 && vk <= 0x5A) || (vk >= 0x30 && vk <= 0x39)) return ((char)vk).ToString();
            if (vk >= 0x60 && vk <= 0x69) return (german ? "Num " : "NumPad") + (vk - 0x60);
            return "0x" + vk.ToString("X2");
        }
    }

    // =================================================================================
    //  Modi (Belegungen, zwischen denen der Hotkey umschaltet)
    // =================================================================================
    public sealed class ModeDef
    {
        public string Name;
        public Binding Binding;
        public uint Accent;          // ARGB
        public string Detail;        // null = automatisch aus der Belegung

        public ModeDef(string name, Binding binding, uint accent)
        {
            Name = name;
            Binding = binding;
            Accent = accent;
        }

        public string Subtitle
        {
            get
            {
                string d = Detail ?? Binding.ShortDetail;
                return d.Length > 0 ? "Trigger · " + d : "Trigger";
            }
        }

        public OverlayIcon Icon
        {
            get
            {
                byte fn = Binding.FnId;
                if (fn == 2 || fn == 13) return Binding.Data[1] == 0x2C ? OverlayIcon.Space : OverlayIcon.Key;
                if (fn == 1 || fn == 14 || fn == 11)
                {
                    byte code = Binding.Data[0];
                    if (code == 1) return OverlayIcon.MouseLeft;
                    if (code == 2) return OverlayIcon.MouseRight;
                    if (code == 3) return OverlayIcon.MouseMiddle;
                    return OverlayIcon.Mouse;
                }
                return OverlayIcon.Generic;
            }
        }

        // Beschriftung für das Tasten-Symbol (nur kurze Namen passen in die Kappe)
        public string KeyLabel
        {
            get
            {
                if (Binding.FnId != 2 && Binding.FnId != 13) return null;
                string k = Names.Key(Binding.Data[1]);
                return k.Length > 0 && k.Length <= 3 ? k : null;
            }
        }

        public OverlayContent ToOverlay()
        {
            OverlayContent c = new OverlayContent();
            c.Title = Name;
            c.Subtitle = Subtitle;
            c.Accent = Accent;
            c.Icon = Icon;
            c.KeyLabel = KeyLabel;
            return c;
        }
    }

    public enum Corner { BottomRight, BottomLeft, TopRight, TopLeft, BottomCenter, TopCenter }

    // =================================================================================
    //  config.json (neben der .exe)
    // =================================================================================
    public sealed class AppConfig
    {
        public const string DefaultHotkey = "Ctrl+Alt+F11";
        static readonly uint[] Palette = { 0xFF4ADE80, 0xFF60A5FA, 0xFFF472B6, 0xFFFBBF24, 0xFFA78BFA, 0xFF2DD4BF };

        public HotkeyDef Hotkey = HotkeyDef.Parse(DefaultHotkey);
        public bool OverlayEnabled = true;
        public double OverlaySeconds = 3.0;
        public Corner OverlayCorner = Corner.BottomRight;
        public int WatchdogMs = 2500;
        public bool VerboseLog;
        public List<ModeDef> Modes = DefaultModes();
        public List<string> Warnings = new List<string>();

        public static List<ModeDef> DefaultModes()
        {
            List<ModeDef> m = new List<ModeDef>();
            m.Add(new ModeDef("Leertaste", Binding.SpaceTurbo20, Palette[0]));
            m.Add(new ModeDef("Rechtsklick", Binding.RightClickTurbo20, Palette[1]));
            return m;
        }

        public static AppConfig Load(string path)
        {
            AppConfig c = new AppConfig();
            if (!File.Exists(path))
            {
                c.Warnings.Add("nicht gefunden – verwende Standardwerte.");
                return c;
            }
            Dictionary<string, object> root;
            try
            {
                root = Json.AsObject(Json.Parse(File.ReadAllText(path, Encoding.UTF8)));
                if (root == null) throw new FormatException("Die oberste Ebene ist kein JSON-Objekt.");
            }
            catch (Exception ex)
            {
                c.Warnings.Add("fehlerhaft (" + ex.Message + ") – verwende Standardwerte.");
                return c;
            }

            string hk = Json.GetString(root, "hotkey");
            if (hk != null)
            {
                try
                {
                    c.Hotkey = HotkeyDef.Parse(hk);
                }
                catch (FormatException ex)
                {
                    c.Warnings.Add("hotkey: " + ex.Message + " – verwende " + DefaultHotkey + ".");
                }
            }

            Dictionary<string, object> ov = Json.GetObject(root, "overlay");
            if (ov != null)
            {
                bool? en = Json.GetBool(ov, "enabled");
                if (en.HasValue) c.OverlayEnabled = en.Value;
                object secs = Json.Get(ov, "seconds");
                if (secs is long || secs is double)
                {
                    double s = Convert.ToDouble(secs, CultureInfo.InvariantCulture);
                    c.OverlaySeconds = Math.Max(1.0, Math.Min(10.0, s));
                }
                string pos = Json.GetString(ov, "position");
                if (pos != null)
                {
                    Corner corner;
                    if (TryParseCorner(pos, out corner)) c.OverlayCorner = corner;
                    else c.Warnings.Add("overlay.position \"" + pos + "\" unbekannt – verwende bottomRight.");
                }
            }

            long? wd = Json.GetLong(root, "watchdogMs");
            if (wd.HasValue) c.WatchdogMs = (int)Math.Max(1000, Math.Min(60000, wd.Value));
            bool? verbose = Json.GetBool(root, "verboseLog");
            if (verbose.HasValue) c.VerboseLog = verbose.Value;

            List<object> modes = Json.GetArray(root, "modes");
            if (modes != null)
            {
                List<ModeDef> parsed = new List<ModeDef>();
                for (int i = 0; i < modes.Count; i++)
                {
                    Dictionary<string, object> m = Json.AsObject(modes[i]);
                    if (m == null)
                    {
                        c.Warnings.Add("modes[" + i + "] ist kein Objekt – übersprungen.");
                        continue;
                    }
                    try
                    {
                        parsed.Add(ParseMode(m, parsed.Count));
                    }
                    catch (FormatException ex)
                    {
                        c.Warnings.Add("modes[" + i + "]: " + ex.Message + " – übersprungen.");
                    }
                }
                if (parsed.Count == 0)
                {
                    c.Warnings.Add("keine gültigen Modi – verwende Leertaste/Rechtsklick.");
                }
                else
                {
                    if (parsed.Count == 1) c.Warnings.Add("nur ein Modus eingetragen – der Hotkey hat dann nichts zum Umschalten.");
                    c.Modes = parsed;
                }
            }
            return c;
        }

        static ModeDef ParseMode(Dictionary<string, object> m, int index)
        {
            string name = (Json.GetString(m, "name") ?? "").Trim();
            if (name.Length == 0) name = "Modus " + (index + 1);
            long? fn = Json.GetLong(m, "fn");
            long? size = Json.GetLong(m, "size");
            string data = Json.GetString(m, "data");
            if (!fn.HasValue || !size.HasValue || data == null) throw new FormatException("fn, size und data werden benötigt");
            Binding b = Binding.Parse(fn.Value, size.Value, data);
            uint accent = Palette[index % Palette.Length];
            string color = Json.GetString(m, "color");
            if (color != null)
            {
                uint parsed;
                if (!TryParseColor(color, out parsed)) throw new FormatException("color \"" + color + "\" ist keine Farbe wie #4ADE80");
                accent = parsed;
            }
            ModeDef def = new ModeDef(name, b, accent);
            string detail = Json.GetString(m, "detail");
            if (detail != null) def.Detail = detail.Trim();
            return def;
        }

        public static bool TryParseColor(string text, out uint argb)
        {
            argb = 0;
            string t = (text ?? "").Trim();
            if (t.StartsWith("#")) t = t.Substring(1);
            uint v;
            if (t.Length != 6 || !uint.TryParse(t, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out v)) return false;
            argb = 0xFF000000u | v;
            return true;
        }

        public static bool TryParseCorner(string text, out Corner corner)
        {
            string t = (text ?? "").Trim().ToLowerInvariant().Replace("-", "").Replace(" ", "");
            switch (t)
            {
                case "bottomright": case "untenrechts": corner = Corner.BottomRight; return true;
                case "bottomleft": case "untenlinks": corner = Corner.BottomLeft; return true;
                case "topright": case "obenrechts": corner = Corner.TopRight; return true;
                case "topleft": case "obenlinks": corner = Corner.TopLeft; return true;
                case "bottomcenter": case "untenmitte": corner = Corner.BottomCenter; return true;
                case "topcenter": case "obenmitte": corner = Corner.TopCenter; return true;
                default: corner = Corner.BottomRight; return false;
            }
        }
    }

    // =================================================================================
    //  state.json: zuletzt gewählter Modus + welcher Gerätemodus vorher eingestellt war
    // =================================================================================
    public sealed class AppState
    {
        public int ModeIndex;
        public string ModeName = "";
        public int ForeignMode = -1;     // z. B. 3 = Treibermodus von Synapse; -1 = nie gesehen

        public static AppState Load(string path, List<ModeDef> modes)
        {
            AppState s = new AppState();
            try
            {
                if (File.Exists(path))
                {
                    Dictionary<string, object> o = Json.AsObject(Json.Parse(File.ReadAllText(path, Encoding.UTF8)));
                    if (o != null)
                    {
                        long? idx = Json.GetLong(o, "modeIndex");
                        if (idx.HasValue && idx.Value >= 0 && idx.Value < 1000) s.ModeIndex = (int)idx.Value;
                        s.ModeName = Json.GetString(o, "modeName") ?? "";
                        long? fm = Json.GetLong(o, "foreignMode");
                        if (fm.HasValue && fm.Value >= 0 && fm.Value <= 255) s.ForeignMode = (int)fm.Value;
                    }
                }
            }
            catch (Exception ex)
            {
                Log.Warn("state.json ist fehlerhaft (" + ex.Message + ") – starte mit dem ersten Modus.");
                s = new AppState();
            }
            // Name hat Vorrang (falls Modi umsortiert wurden), sonst Index
            int byName = -1;
            for (int i = 0; i < modes.Count; i++)
            {
                if (s.ModeName.Length > 0 && string.Equals(modes[i].Name, s.ModeName, StringComparison.OrdinalIgnoreCase)) { byName = i; break; }
            }
            if (byName >= 0) s.ModeIndex = byName;
            if (s.ModeIndex >= modes.Count) s.ModeIndex = 0;
            s.ModeName = modes[s.ModeIndex].Name;
            return s;
        }

        public void Save(string path)
        {
            Dictionary<string, object> o = new Dictionary<string, object>();
            o["modeIndex"] = (long)ModeIndex;
            o["modeName"] = ModeName;
            o["foreignMode"] = (long)ForeignMode;
            try
            {
                File.WriteAllText(path, Json.Stringify(o), new UTF8Encoding(false));
            }
            catch (Exception ex)
            {
                Log.Warn("state.json konnte nicht gespeichert werden: " + ex.Message);
            }
        }
    }

    // =================================================================================
    //  Umschalt-Logik
    // =================================================================================
    public enum LinkStatus { Unknown, Ready, Asleep, NotFound, Disconnected, Failed }

    public sealed class SwitchOutcome
    {
        public bool Applied;             // Maus hat jetzt sicher die gewünschte Belegung
        public bool Changed;             // dafür musste etwas geschrieben werden
        public MouseProblem? Problem;
        public string Message;
        public int ModeIndex;
        public ModeDef Mode;
        public long ElapsedMs;
        public BatteryInfo Battery;      // Akkustand fürs Overlay (null = unbekannt)
    }

    // Akkustand der Maus (nur gelesen, wird nach dem Anlegen nicht mehr verändert)
    public sealed class BatteryInfo
    {
        public int Percent;              // 0–100
        public bool Charging;
        public DateTime At;              // Zeitpunkt der Messung

        public string Text
        {
            get { return Percent.ToString(CultureInfo.InvariantCulture) + " %" + (Charging ? " (lädt)" : ""); }
        }
    }

    public sealed class SwitchController : IDisposable
    {
        readonly List<ModeDef> _modes;
        readonly AppState _state;
        readonly Func<BasiliskSession> _connect;
        readonly Action _saveState;
        readonly List<DateTime> _reapplies = new List<DateTime>();
        BasiliskSession _session;
        int _index;
        bool _verified;                  // seit dem letzten Verbinden einmal komplett geprüft
        int _failures;
        LinkStatus _status = LinkStatus.Unknown;
        DateTime _lastFightWarning = DateTime.MinValue;
        DateTime _lastFailureLog = DateTime.MinValue;
        BatteryInfo _battery;            // zuletzt gelesener Akkustand
        DateTime _batteryTried = DateTime.MinValue;
        BasiliskSession _batterySession; // Verbindung, über die zuletzt gelesen wurde
        bool _batteryLogged;

        const int BatteryEverySeconds = 60;    // der Wächter liest den Akku höchstens einmal pro Minute
        const int BatteryFreshSeconds = 120;   // beim Umschalten nur neu lesen, wenn der letzte Versuch älter ist
        const int BatteryShowMinutes = 15;     // ältere Werte zeigt das Overlay nicht mehr

        public Func<DateTime> Clock = delegate { return DateTime.Now; };

        public SwitchController(List<ModeDef> modes, AppState state, Func<BasiliskSession> connect, Action saveState)
        {
            _modes = modes;
            _state = state;
            _connect = connect;
            _saveState = saveState;
            _index = Math.Max(0, Math.Min(modes.Count - 1, state.ModeIndex));
        }

        public int Index { get { return _index; } }
        public ModeDef Current { get { return _modes[_index]; } }
        public LinkStatus Status { get { return _status; } }

        // Nach Geräteänderungen (Dongle/Kabel umgesteckt) neu suchen
        public void Reopen()
        {
            CloseSession();
        }

        public SwitchOutcome Toggle(int steps)
        {
            int n = _modes.Count;
            _index = ((_index + steps) % n + n) % n;
            _state.ModeIndex = _index;
            _state.ModeName = Current.Name;
            SaveState();

            Stopwatch sw = Stopwatch.StartNew();
            SwitchOutcome o = Run(false);
            o.ElapsedMs = sw.ElapsedMilliseconds;
            if (o.Applied)
            {
                Log.Info(string.Format(CultureInfo.InvariantCulture, "Hotkey → {0}: übernommen ({1} ms).", Current.Name, o.ElapsedMs));
                RefreshBattery(BatteryFreshSeconds);
                o.Battery = RecentBattery();
            }
            else
            {
                Log.Warn("Hotkey → " + Current.Name + ": noch nicht übernommen – " + o.Message + " Wird automatisch nachgeholt.");
            }
            return o;
        }

        // Wächter: Gerätemodus + Trigger prüfen und bei Bedarf wieder setzen.
        public SwitchOutcome Check()
        {
            SwitchOutcome o = Run(true);
            if (o.Applied) RefreshBattery(BatteryEverySeconds);
            return o;
        }

        // Akkustand höchstens alle maxAgeSeconds lesen. Auch Fehlversuche zählen, damit eine Maus, die den Befehl nicht
        // beantwortet, nicht ständig gefragt wird. Fehler hier ändern nichts am Umschalten (der Wächter merkt echte Probleme).
        void RefreshBattery(int maxAgeSeconds)
        {
            if (_session == null) return;
            DateTime now = Clock();
            // neue Verbindung (z. B. Kabel ein-/ausgesteckt): sofort lesen, damit "lädt" stimmt
            bool sameSession = ReferenceEquals(_session, _batterySession);
            if (sameSession && _batteryTried != DateTime.MinValue && (now - _batteryTried).TotalSeconds < maxAgeSeconds) return;
            _batteryTried = now;
            _batterySession = _session;
            try
            {
                BatteryInfo b = _session.ReadBattery();
                if (b == null)
                {
                    if (!_batteryLogged) Log.Info("Akkustand: von der Maus nicht geliefert – das Overlay zeigt ihn dann nicht.");
                    _batteryLogged = true;
                    return;
                }
                b.At = now;
                if (!_batteryLogged) Log.Info("Akkustand: " + b.Text + ".");
                else Log.Debug("Akkustand: " + b.Text + ".");
                _batteryLogged = true;
                _battery = b;
            }
            catch (Exception ex)
            {
                // nie das Umschalten oder das Overlay gefährden
                Log.Debug("Akkustand nicht lesbar: " + ex.Message);
            }
        }

        BatteryInfo RecentBattery()
        {
            if (_battery == null || (Clock() - _battery.At).TotalMinutes > BatteryShowMinutes) return null;
            return _battery;
        }

        SwitchOutcome Run(bool watchdog)
        {
            SwitchOutcome o = new SwitchOutcome();
            o.ModeIndex = _index;
            o.Mode = Current;
            try
            {
                o.Changed = Ensure(watchdog);
                o.Applied = true;
                OnHealthy();
            }
            catch (MouseException ex)
            {
                o.Problem = ex.Problem;
                o.Message = ex.Message;
                OnProblem(ex);
            }
            return o;
        }

        bool Ensure(bool watchdog)
        {
            if (_session == null)
            {
                _session = _connect();
                _verified = false;
                Log.Info("Maus erreichbar: " + _session.Label + ", Firmware " + _session.Firmware + ".");
            }
            bool reverted = watchdog && _verified;   // war schon einmal korrekt -> jemand hat zurückgestellt
            string why = reverted ? " (war zurückgesetzt, vermutlich durch Synapse)" : "";
            bool changed = false;

            int mode = _session.ReadMode();
            if (mode != 0x00)
            {
                if (mode != _state.ForeignMode)
                {
                    _state.ForeignMode = mode;
                    SaveState();
                }
                _session.SetMode(0x00);
                Log.Info("Gerätemodus " + BasiliskSession.ModeName(mode) + " → 0x00 (Maus führt ihre Belegung selbst aus)" + why + ".");
                changed = true;
            }

            Binding current = _session.ReadTrigger(0x00);
            Binding wanted = Current.Binding;
            if (!current.Matches(wanted))
            {
                _session.WriteTrigger(wanted);
                string line = "Trigger → " + Current.Name + " (" + wanted.Text + "), vorher: " + current.Text + why + ".";
                if (watchdog) Log.Info(line); else Log.Debug(line);
                changed = true;
            }
            else
            {
                Log.Debug("Prüfung ok: Gerätemodus 0x00, Trigger = " + Current.Name + ".");
            }

            if (reverted && changed) NoteReapply();
            _verified = true;
            return changed;
        }

        void NoteReapply()
        {
            DateTime now = Clock();
            _reapplies.Add(now);
            _reapplies.RemoveAll(delegate(DateTime t) { return (now - t).TotalMinutes > 10; });
            if (_reapplies.Count >= 6 && (now - _lastFightWarning).TotalMinutes >= 60)
            {
                _lastFightWarning = now;
                Log.Warn("Die Belegung wurde in 10 Minuten " + _reapplies.Count + "× zurückgestellt – vermutlich setzt Synapse sie immer wieder. " +
                         "TriggerSwitch stellt sie jedes Mal wieder her; wenn das stört: Synapse-Einstellungen prüfen oder TriggerSwitch pausieren.");
            }
        }

        void OnHealthy()
        {
            _failures = 0;
            if (_status != LinkStatus.Ready && _status != LinkStatus.Unknown) Log.Info("Maus wieder bereit – Trigger: " + Current.Name + ".");
            else if (_status == LinkStatus.Unknown) Log.Info("Bereit – Trigger: " + Current.Name + ".");
            _status = LinkStatus.Ready;
        }

        void OnProblem(MouseException ex)
        {
            _verified = false;
            LinkStatus next;
            switch (ex.Problem)
            {
                case MouseProblem.Asleep:
                    // Verbindung zum Dongle bleibt offen (kein ständiges Neu-Suchen). Kabel ein-/ausstecken meldet
                    // Windows als Geräteänderung -> der Worker ruft Reopen() auf.
                    next = LinkStatus.Asleep;
                    if (_status != next) Log.Info("Maus antwortet nicht (schläft oder ist aus) – die Belegung wird gesetzt, sobald sie wieder da ist.");
                    break;
                case MouseProblem.NotFound:
                    CloseSession();
                    next = LinkStatus.NotFound;
                    if (_status != next) Log.Warn(ex.Message + " Warte …");
                    break;
                case MouseProblem.Disconnected:
                    CloseSession();
                    next = LinkStatus.Disconnected;
                    if (_status != next) Log.Warn("Verbindung zur Maus verloren (" + ex.Message + ") – suche neu.");
                    break;
                default:
                    _failures++;
                    next = LinkStatus.Failed;
                    DateTime now = Clock();
                    if (_status != next || (now - _lastFailureLog).TotalMinutes >= 10)
                    {
                        _lastFailureLog = now;
                        Log.Warn("Maus: " + ex.Message);
                    }
                    if (_failures >= 3)
                    {
                        CloseSession();   // frisch verbinden, falls die Sitzung hängt
                        _failures = 0;
                    }
                    break;
            }
            _status = next;
        }

        // Gibt die Maus an Synapse zurück: Trigger wie im gespeicherten Profil, Gerätemodus wie vorgefunden.
        public SwitchOutcome Release()
        {
            SwitchOutcome o = new SwitchOutcome();
            try
            {
                if (_session == null) _session = _connect();
                int active = _session.ReadActiveProfile();
                byte source = (byte)(active > 0 ? active : 1);
                Binding stored = _session.ReadTrigger(source);
                Binding current = _session.ReadTrigger(0x00);
                if (!current.Matches(stored)) _session.WriteTrigger(stored);

                int mode = _session.ReadMode();
                string modeText;
                if (_state.ForeignMode == 0x03)
                {
                    if (mode != 0x03) _session.SetMode(0x03);
                    modeText = "Gerätemodus 0x03 – Synapse steuert wieder";
                }
                else
                {
                    modeText = "Gerätemodus bleibt " + BasiliskSession.ModeName(mode);
                }
                Log.Info("Maus zurückgegeben: Trigger wie im gespeicherten Profil " + source + " (" + stored.Text + "), " + modeText + ".");
                o.Applied = true;
            }
            catch (MouseException ex)
            {
                o.Problem = ex.Problem;
                o.Message = ex.Message;
                if (ex.Problem != MouseProblem.Failed) CloseSession();
            }
            return o;
        }

        void SaveState()
        {
            if (_saveState != null) _saveState();
        }

        void CloseSession()
        {
            if (_session == null) return;
            try
            {
                _session.Dispose();
            }
            catch (Exception)
            {
            }
            _session = null;
        }

        public void Dispose()
        {
            CloseSession();
        }
    }

    // #################################################################################
    // Razer-Protokoll (90-Byte-Feature-Reports wie Synapse/OpenRazer) + Sitzung mit der Basilisk V3 Pro.
    // Schreibzugriffe gibt es genau zwei: Trigger-Belegung (Taste 0x0F, normale Ebene) und Gerätemodus 0x00/0x03.
    // #################################################################################

    // ---------------------------------------------------------------- Protokoll

    public sealed class RazerResponse
    {
        public byte Status;
        public byte TransactionId;
        public byte DataSize;
        public byte CommandClass;
        public byte CommandId;
        public byte[] Args = new byte[80];
        public string Error;            // Transport-/Ablauffehler (dann ist Status bedeutungslos)
        public bool TransportFailed;    // Senden/Empfangen selbst ging schief (Gerät weg?)

        public bool Ok
        {
            get { return Error == null && Status == RazerReport.StatusSuccess; }
        }

        public string Describe()
        {
            if (Error != null) return Error;
            return RazerReport.StatusName(Status);
        }
    }

    public static class RazerReport
    {
        public const int Length = 90;
        public const byte StatusNew = 0x00;
        public const byte StatusBusy = 0x01;
        public const byte StatusSuccess = 0x02;
        public const byte StatusFailed = 0x03;
        public const byte StatusTimeout = 0x04;
        public const byte StatusNotSupported = 0x05;

        public static byte[] Build(byte transactionId, byte commandClass, byte commandId, byte dataSize, byte[] args)
        {
            byte[] r = new byte[Length];
            r[0] = 0x00;                 // Status: neuer Befehl
            r[1] = transactionId;
            r[2] = 0x00;                 // verbleibende Pakete (Big Endian)
            r[3] = 0x00;
            r[4] = 0x00;                 // Protokolltyp
            r[5] = dataSize;
            r[6] = commandClass;
            r[7] = commandId;
            if (args != null) Array.Copy(args, 0, r, 8, Math.Min(args.Length, 80));
            r[88] = Crc(r);
            r[89] = 0x00;
            return r;
        }

        // XOR über Bytes 2..87 (wie OpenRazer und Synapse)
        public static byte Crc(byte[] report)
        {
            byte crc = 0;
            for (int i = 2; i < 88; i++) crc ^= report[i];
            return crc;
        }

        public static RazerResponse Parse(byte[] buffer, int offset)
        {
            RazerResponse r = new RazerResponse();
            r.Status = buffer[offset + 0];
            r.TransactionId = buffer[offset + 1];
            r.DataSize = buffer[offset + 5];
            r.CommandClass = buffer[offset + 6];
            r.CommandId = buffer[offset + 7];
            Array.Copy(buffer, offset + 8, r.Args, 0, 80);
            return r;
        }

        public static string StatusName(byte status)
        {
            switch (status)
            {
                case StatusNew: return "keine Antwort (neu)";
                case StatusBusy: return "beschäftigt";
                case StatusSuccess: return "OK";
                case StatusFailed: return "abgelehnt";
                case StatusTimeout: return "Zeitüberschreitung (Maus schläft oder ist aus)";
                case StatusNotSupported: return "nicht unterstützt";
                default: return "Status 0x" + status.ToString("X2");
            }
        }
    }

    public sealed class RazerClient
    {
        readonly IHidChannel _channel;
        public byte TransactionId;
        public int WaitMs = 35;
        public int Attempts = 5;
        public int Collisions;

        public RazerClient(IHidChannel channel, byte transactionId)
        {
            _channel = channel;
            TransactionId = transactionId;
        }

        // Lese-Befehle: Bei Razer haben alle "Get"-Befehle das Bit 0x80 in der Befehls-ID.
        // Alles andere wird hier verweigert.
        public RazerResponse Get(byte commandClass, byte commandId, byte dataSize, params byte[] args)
        {
            if ((commandId & 0x80) == 0)
            {
                throw new InvalidOperationException(string.Format(CultureInfo.InvariantCulture,
                    "Schreibbefehl {0:X2}:{1:X2} blockiert – Get() liest nur.", commandClass, commandId));
            }
            return Transact(commandClass, commandId, dataSize, args);
        }

        public const byte TriggerButton = 0x0F;   // Multifunktions-Trigger der Basilisk V3 Pro (per Diagnose ermittelt)

        // EINZIGER Schreibweg für Belegungen: Trigger-Taste 0x0F (normale Ebene) im Arbeitsprofil 0x00.
        // Befehl 02:0C "Set Single Button Assignment" (wie Synapse): [Profil, Taste, Ebene, fnId, Größe, Daten[5]].
        // Profil 0x00 ist der flüchtige Arbeitsspeicher der Maus – das gespeicherte Synapse-Profil bleibt unberührt.
        public RazerResponse SetTriggerAssignment(byte profile, byte fnId, byte size, byte[] data)
        {
            if (profile != 0x00) throw new InvalidOperationException("Nur das Arbeitsprofil 0x00 darf geschrieben werden.");
            if (data == null || data.Length != 5 || size > 5) throw new InvalidOperationException("Ungültige Belegungsdaten.");
            if (fnId > 20) throw new InvalidOperationException("Unbekannte Funktion " + fnId + " wird nicht geschrieben.");
            byte[] args = new byte[] { profile, TriggerButton, 0x00, fnId, size, data[0], data[1], data[2], data[3], data[4] };
            return Transact(0x02, 0x0C, 0x50, args);
        }

        // ZWEITER erlaubter Schreibweg: Gerätemodus 00:04 – nur 0x00 (normal: Maus führt ihre Belegung selbst aus)
        // oder 0x03 (Treibermodus: Synapse übernimmt). Wie OpenRazer; alles andere ist gesperrt.
        public RazerResponse SetDeviceMode(byte mode)
        {
            if (mode != 0x00 && mode != 0x03) throw new InvalidOperationException("Nur Gerätemodus 0x00 oder 0x03 erlaubt.");
            return Transact(0x00, 0x04, 0x02, new byte[] { mode, 0x00 });
        }

        RazerResponse Transact(byte commandClass, byte commandId, byte dataSize, byte[] args)
        {
            if (_channel.FeatureLength < RazerReport.Length + 1)
            {
                RazerResponse bad = new RazerResponse();
                bad.Error = "Feature-Report zu kurz (" + _channel.FeatureLength + " Bytes)";
                return bad;
            }

            byte[] report = RazerReport.Build(TransactionId, commandClass, commandId, dataSize, args);
            string lastError = "keine Antwort";
            bool lastWasTransport = false;
            RazerResponse lastFailed = null;
            int failedRetries = 0;
            for (int attempt = 0; attempt < Attempts; attempt++)
            {
                if (attempt > 0) Thread.Sleep(40);
                byte[] send = new byte[_channel.FeatureLength];
                send[0] = 0x00;  // Report-ID
                Array.Copy(report, 0, send, 1, RazerReport.Length);
                if (!_channel.SetFeature(send))
                {
                    lastError = "Senden fehlgeschlagen (Win32-Fehler " + _channel.LastError + ")";
                    lastWasTransport = true;
                    continue;
                }

                RazerResponse r = null;
                bool receiveFailed = false;
                for (int poll = 0; poll < 12; poll++)
                {
                    if (WaitMs > 0) Thread.Sleep(WaitMs);
                    byte[] receive = new byte[_channel.FeatureLength];
                    receive[0] = 0x00;
                    if (!_channel.GetFeature(receive))
                    {
                        lastError = "Empfangen fehlgeschlagen (Win32-Fehler " + _channel.LastError + ")";
                        receiveFailed = true;
                        break;
                    }
                    RazerResponse candidate = RazerReport.Parse(receive, 1);
                    if (candidate.Status == RazerReport.StatusBusy || candidate.Status == RazerReport.StatusNew) continue;
                    r = candidate;
                    break;
                }
                if (r == null)
                {
                    lastWasTransport = receiveFailed;
                    if (!receiveFailed) lastError = "keine Antwort";
                    continue;
                }

                if (r.CommandClass != commandClass || r.CommandId != commandId)
                {
                    // Antwort gehört zu einem anderen Programm (z. B. Synapse) -> neu versuchen
                    Collisions++;
                    lastError = "Antwort gehörte zu einem anderen Befehl";
                    lastWasTransport = false;
                    continue;
                }
                if (r.Status == RazerReport.StatusFailed && failedRetries < 1)
                {
                    // Gleichzeitiger Zugriff anderer Software liefert gelegentlich "abgelehnt": einmal wiederholen
                    failedRetries++;
                    lastFailed = r;
                    continue;
                }
                return r;
            }
            if (lastFailed != null && !lastWasTransport) return lastFailed;
            RazerResponse failed = new RazerResponse();
            failed.Error = lastError;
            failed.TransportFailed = lastWasTransport;
            return failed;
        }
    }

    // ---------------------------------------------------------------- Klartext (für Log und Overlay)

    public static class Names
    {
        public static string MouseCode(byte code)
        {
            switch (code)
            {
                case 1: return "Linksklick";
                case 2: return "Rechtsklick";
                case 3: return "Mittelklick";
                case 4: return "Taste 4 (zurück)";
                case 5: return "Taste 5 (vor)";
                case 9: return "Mausrad hoch";
                case 10: return "Mausrad runter";
                case 0x68: return "Mausrad links";
                case 0x69: return "Mausrad rechts";
                default: return "Maustaste 0x" + code.ToString("X2");
            }
        }

        public static string Key(byte usage)
        {
            if (usage >= 0x04 && usage <= 0x1D) return ((char)('A' + usage - 0x04)).ToString();
            if (usage >= 0x1E && usage <= 0x26) return ((char)('1' + usage - 0x1E)).ToString();
            if (usage == 0x27) return "0";
            if (usage >= 0x3A && usage <= 0x45) return "F" + (usage - 0x3A + 1);
            if (usage >= 0x68 && usage <= 0x73) return "F" + (usage - 0x68 + 13);
            switch (usage)
            {
                case 0x00: return "";
                case 0x28: return "Enter";
                case 0x29: return "Esc";
                case 0x2A: return "Rücktaste";
                case 0x2B: return "Tab";
                case 0x2C: return "Leertaste";
                case 0x39: return "Feststell";
                case 0x48: return "Pause";
                case 0x49: return "Einfg";
                case 0x4A: return "Pos1";
                case 0x4C: return "Entf";
                case 0x4D: return "Ende";
                case 0x4F: return "Pfeil rechts";
                case 0x50: return "Pfeil links";
                case 0x51: return "Pfeil runter";
                case 0x52: return "Pfeil hoch";
                case 0xE0: return "Strg";
                case 0xE1: return "Umschalt";
                case 0xE2: return "Alt";
                default: return "Taste 0x" + usage.ToString("X2");
            }
        }

        public static string Modifiers(byte bits)
        {
            List<string> parts = new List<string>();
            if ((bits & 0x11) != 0) parts.Add("Strg");
            if ((bits & 0x22) != 0) parts.Add("Umschalt");
            if ((bits & 0x04) != 0) parts.Add("Alt");
            if ((bits & 0x40) != 0) parts.Add("AltGr");
            if ((bits & 0x88) != 0) parts.Add("Win");
            return string.Join("+", parts.ToArray());
        }

        public static string KeyWithModifiers(byte mods, byte usage)
        {
            string m = Modifiers(mods);
            string k = Key(usage);
            if (m.Length == 0) return k;
            if (k.Length == 0) return m;
            return m + "+" + k;
        }

        public static string Rate(int intervalMs)
        {
            if (intervalMs <= 0) return "?/s";
            return string.Format(CultureInfo.InvariantCulture, "{0:0.#}/s", 1000.0 / intervalMs);
        }

        // fnId/Datenformat laut Synapse-4-Code (razerctl docs/onboard-button-protocol.md), per Diagnose bestätigt
        public static string Function(byte fnId, byte size, byte[] d)
        {
            switch (fnId)
            {
                case 0: return "deaktiviert";
                case 1: return "Maus: " + MouseCode(d[0]);
                case 2: return "Taste: " + KeyWithModifiers(d[0], d[1]);
                case 6: return "DPI-Funktion " + d[0];
                case 10: return "Multimedia 0x" + ((d[0] << 8) | d[1]).ToString("X4");
                case 11: return "Doppelklick";
                case 12: return "Hypershift";
                case 13: return "Turbo-Taste " + KeyWithModifiers(d[0], d[1]) + ", " + Rate((d[2] << 8) | d[3]);
                case 14: return "Turbo-Maus " + MouseCode(d[0]) + ", " + Rate((d[1] << 8) | d[2]);
                default: return "Funktion " + fnId;
            }
        }

        public static string Hex(byte[] data, int offset, int count)
        {
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < count; i++)
            {
                if (i > 0) sb.Append(' ');
                sb.Append(data[offset + i].ToString("X2"));
            }
            return sb.ToString();
        }
    }

    // ---------------------------------------------------------------- Belegung

    public sealed class Binding
    {
        public byte FnId;
        public byte Size;
        public byte[] Data = new byte[5];

        public Binding()
        {
        }

        public Binding(byte fnId, byte size, params byte[] data)
        {
            FnId = fnId;
            Size = size;
            for (int i = 0; i < 5 && data != null && i < data.Length; i++) Data[i] = data[i];
        }

        public static readonly Binding SpaceTurbo20 = new Binding(13, 4, 0x00, 0x2C, 0x00, 0x32, 0x00);
        public static readonly Binding RightClickTurbo20 = new Binding(14, 3, 0x02, 0x00, 0x32, 0x00, 0x00);

        public bool Matches(Binding other)
        {
            if (other == null || FnId != other.FnId || Size != other.Size) return false;
            for (int i = 0; i < Size && i < 5; i++) if (Data[i] != other.Data[i]) return false;
            return true;
        }

        public string Text
        {
            get { return Names.Function(FnId, Size, Data); }
        }

        public string Hex
        {
            get { return string.Format(CultureInfo.InvariantCulture, "fn {0}, Größe {1}, Daten {2}", FnId, Size, Names.Hex(Data, 0, 5)); }
        }

        // Kurzbeschreibung für das Overlay ("Turbo 20/s"); leer, wenn es nichts Sinnvolles gibt.
        public string ShortDetail
        {
            get
            {
                switch (FnId)
                {
                    case 13: return "Turbo " + Names.Rate((Data[2] << 8) | Data[3]);
                    case 14: return "Turbo " + Names.Rate((Data[1] << 8) | Data[2]);
                    case 11: return "Doppelklick";
                    default: return "";
                }
            }
        }

        // "00 2C 00 32 00" -> 5 Bytes
        public static Binding Parse(long fnId, long size, string dataHex)
        {
            if (fnId < 0 || fnId > 20) throw new FormatException("fn muss zwischen 0 und 20 liegen.");
            if (size < 0 || size > 5) throw new FormatException("size muss zwischen 0 und 5 liegen.");
            string[] parts = (dataHex ?? "").Split(new char[] { ' ', ',', '-' }, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length != 5) throw new FormatException("data braucht genau 5 Hex-Bytes, z. B. \"00 2C 00 32 00\".");
            Binding b = new Binding();
            b.FnId = (byte)fnId;
            b.Size = (byte)size;
            for (int i = 0; i < 5; i++)
            {
                string p = parts[i];
                if (p.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) p = p.Substring(2);
                byte v;
                if (p.Length == 0 || p.Length > 2 || !byte.TryParse(p, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out v))
                {
                    throw new FormatException("\"" + parts[i] + "\" ist kein Hex-Byte.");
                }
                b.Data[i] = v;
            }
            return b;
        }
    }

    // ---------------------------------------------------------------- Sitzung

    public enum MouseProblem
    {
        NotFound,       // kein passendes Gerät / Dongle nicht eingesteckt
        Asleep,         // Dongle da, Maus antwortet nicht (schläft oder ist aus)
        Disconnected,   // Gerät während der Sitzung verschwunden
        Failed          // Maus hat abgelehnt bzw. Wert nicht übernommen
    }

    public sealed class MouseException : Exception
    {
        public readonly MouseProblem Problem;

        public MouseException(MouseProblem problem, string message) : base(message)
        {
            Problem = problem;
        }
    }

    public sealed class BasiliskSession : IDisposable
    {
        // Wartezeiten zentral, damit die Tests ohne Pausen laufen können
        public static Action<int> Delay = delegate(int ms) { Thread.Sleep(ms); };

        static readonly ushort[] WiredIds = { 0x00AA, 0x00CC, 0x00D6 };
        static readonly ushort[] WirelessIds = { 0x00AB, 0x00CD, 0x00D7 };
        static readonly byte[] TransactionIds = { 0x1F, 0xFF, 0x3F };

        readonly IHidChannel _channel;
        readonly RazerClient _client;
        public readonly string Label;
        public readonly string Firmware;

        BasiliskSession(IHidChannel channel, RazerClient client, string label, string firmware)
        {
            _channel = channel;
            _client = client;
            Label = label;
            Firmware = firmware;
        }

        public static string ProductLabel(ushort pid)
        {
            switch (pid)
            {
                case 0x00AA: return "Basilisk V3 Pro (Kabel)";
                case 0x00AB: return "Basilisk V3 Pro (2,4-GHz-Dongle)";
                case 0x00CC: return "Basilisk V3 Pro 35K (Kabel)";
                case 0x00CD: return "Basilisk V3 Pro 35K (2,4-GHz-Dongle)";
                case 0x00D6: return "Basilisk V3 Pro 35K Phantom Green (Kabel)";
                case 0x00D7: return "Basilisk V3 Pro 35K Phantom Green (Dongle)";
                default: return "Razer-Gerät 0x" + pid.ToString("X4");
            }
        }

        // Transaktions-ID über die Firmware-Abfrage ermitteln. null = antwortet nicht auf das Protokoll.
        public static BasiliskSession FromChannel(IHidChannel channel, string label, int waitMs)
        {
            foreach (byte tid in TransactionIds)
            {
                RazerClient c = new RazerClient(channel, tid);
                c.WaitMs = waitMs;
                RazerResponse r = c.Get(0x00, 0x81, 0x02);
                if (r.Ok)
                {
                    return new BasiliskSession(channel, c, label,
                        string.Format(CultureInfo.InvariantCulture, "{0}.{1:00}", r.Args[0], r.Args[1]));
                }
                if (r.Error == null && r.Status == RazerReport.StatusTimeout)
                {
                    throw new MouseException(MouseProblem.Asleep, label + ": Maus antwortet nicht (schläft oder ist aus).");
                }
                if (r.TransportFailed) return null;
            }
            return null;
        }

        // Sucht die Maus: Kabel vor Dongle (beim Laden per Kabel spricht die Maus über USB),
        // Maus-Schnittstelle (Usage 0001:0002) zuerst. Wirft MouseException(NotFound/Asleep).
        public static BasiliskSession Open()
        {
            List<HidInfo> candidates = new List<HidInfo>();
            foreach (HidInfo d in HidEnumerator.FindDevices(0x1532))
            {
                bool basilisk = Array.IndexOf(WiredIds, d.ProductId) >= 0 || Array.IndexOf(WirelessIds, d.ProductId) >= 0;
                if (basilisk && d.FeatureLength >= RazerReport.Length + 1) candidates.Add(d);
            }
            if (candidates.Count == 0)
            {
                throw new MouseException(MouseProblem.NotFound, "Keine Basilisk V3 Pro gefunden (Dongle eingesteckt?).");
            }
            candidates.Sort(delegate(HidInfo a, HidInfo b)
            {
                int c = Rank(a).CompareTo(Rank(b));
                return c != 0 ? c : string.CompareOrdinal(a.Path, b.Path);
            });

            MouseException asleep = null;
            foreach (HidInfo d in candidates)
            {
                WinHidChannel channel;
                try
                {
                    channel = HidEnumerator.Open(d);
                }
                catch (Exception)
                {
                    continue;
                }
                BasiliskSession s = null;
                try
                {
                    s = FromChannel(channel, ProductLabel(d.ProductId), 35);
                }
                catch (MouseException ex)
                {
                    if (ex.Problem == MouseProblem.Asleep && asleep == null) asleep = ex;
                }
                if (s != null) return s;
                channel.Dispose();
            }
            if (asleep != null) throw asleep;
            throw new MouseException(MouseProblem.NotFound, "Basilisk V3 Pro gefunden, antwortet aber nicht.");
        }

        static int Rank(HidInfo d)
        {
            int r = Array.IndexOf(WiredIds, d.ProductId) >= 0 ? 0 : 2;
            if (!(d.UsagePage == 0x0001 && d.Usage == 0x0002)) r += 1;
            return r;
        }

        public static string ModeName(int mode)
        {
            if (mode == 0x00) return "0x00 (Maus führt ihre Belegung selbst aus)";
            if (mode == 0x03) return "0x03 (Treibermodus, Synapse steuert)";
            return "0x" + mode.ToString("X2");
        }

        // ------------------------------------------------------------ Befehle

        // Wirft bei schlafender/verschwundener Maus; liefert false bei "abgelehnt" & Co. (darf wiederholt werden).
        static bool Accept(RazerResponse r, string what)
        {
            if (r.Ok) return true;
            if (r.Error != null && r.TransportFailed)
            {
                throw new MouseException(MouseProblem.Disconnected, what + ": " + r.Error);
            }
            if (r.Error == null && r.Status == RazerReport.StatusTimeout)
            {
                throw new MouseException(MouseProblem.Asleep, what + ": Maus antwortet nicht (schläft oder ist aus).");
            }
            return false;
        }

        static void Require(RazerResponse r, string what)
        {
            if (!Accept(r, what)) throw new MouseException(MouseProblem.Failed, what + ": " + r.Describe());
        }

        public int ReadMode()
        {
            RazerResponse r = _client.Get(0x00, 0x84, 0x02);
            Require(r, "Gerätemodus lesen");
            return r.Args[0];
        }

        // Setzt den Gerätemodus und prüft ihn durch Zurücklesen (bis zu 3 Versuche).
        public void SetMode(byte mode)
        {
            for (int attempt = 0; attempt < 3; attempt++)
            {
                Accept(_client.SetDeviceMode(mode), "Gerätemodus setzen");
                Delay(100);
                if (ReadMode() == mode) return;
            }
            throw new MouseException(MouseProblem.Failed, "Gerätemodus " + ModeName(mode) + " wurde nicht übernommen.");
        }

        public Binding ReadTrigger(byte profile)
        {
            RazerResponse r = _client.Get(0x02, 0x8C, 0x0A, profile, RazerClient.TriggerButton, 0x00);
            Require(r, "Trigger lesen (Profil " + profile + ")");
            if (r.Args[0] != profile || r.Args[1] != RazerClient.TriggerButton)
            {
                throw new MouseException(MouseProblem.Failed, "Trigger lesen: Antwort passt nicht zur Anfrage.");
            }
            Binding b = new Binding();
            b.FnId = r.Args[3];
            b.Size = r.Args[4];
            Array.Copy(r.Args, 5, b.Data, 0, 5);
            return b;
        }

        // Schreibt das Arbeitsprofil 0x00 und prüft durch Zurücklesen (bis zu 3 Versuche; das Setzen ist idempotent).
        public void WriteTrigger(Binding b)
        {
            for (int attempt = 0; attempt < 3; attempt++)
            {
                Accept(_client.SetTriggerAssignment(0x00, b.FnId, b.Size, b.Data), "Trigger schreiben");
                Delay(60);
                if (ReadTrigger(0x00).Matches(b)) return;
            }
            throw new MouseException(MouseProblem.Failed, "Trigger-Belegung wurde nicht übernommen (" + b.Text + ").");
        }

        // Akkustand (07:80, Wert 0–255 in Args[1]) und Ladezustand (07:84, Args[1] = 1: lädt). Reine Lesebefehle,
        // dieselben wie bei OpenRazer. null = die Maus liefert keinen Wert.
        public BatteryInfo ReadBattery()
        {
            RazerResponse r = _client.Get(0x07, 0x80, 0x02);
            if (!Accept(r, "Akkustand lesen")) return null;
            BatteryInfo b = new BatteryInfo();
            b.Percent = Math.Max(0, Math.Min(100, (int)Math.Round(r.Args[1] * 100.0 / 255.0, MidpointRounding.AwayFromZero)));
            RazerResponse c = _client.Get(0x07, 0x84, 0x02);
            b.Charging = Accept(c, "Ladezustand lesen") && c.Args[1] == 0x01;
            return b;
        }

        // Aktives gespeichertes Profil (1..5) oder -1, falls nicht lesbar.
        public int ReadActiveProfile()
        {
            RazerResponse r = _client.Get(0x05, 0x84, 0x01);
            if (!Accept(r, "Aktives Profil lesen")) return -1;
            int p = r.Args[0];
            return p >= 1 && p <= 5 ? p : -1;
        }

        public void Dispose()
        {
            IDisposable d = _channel as IDisposable;
            if (d != null) d.Dispose();
        }
    }

    // #################################################################################
    // HID-Zugriff (Feature-Reports) – unverändert aus RazerDiagnose/Modustest übernommen (dort auf deinem PC getestet)
    // #################################################################################

    public interface IHidChannel
    {
        int FeatureLength { get; }           // inkl. Report-ID-Byte (erwartet: 91)
        bool SetFeature(byte[] buffer);
        bool GetFeature(byte[] buffer);
        int LastError { get; }
    }

    public sealed class HidInfo
    {
        public string Path;
        public ushort VendorId;
        public ushort ProductId;
        public ushort Version;
        public ushort UsagePage;
        public ushort Usage;
        public int FeatureLength;
    }

    internal static class Native
    {
        public const uint DIGCF_PRESENT = 0x02;
        public const uint DIGCF_DEVICEINTERFACE = 0x10;
        public const uint FILE_SHARE_READ = 0x01;
        public const uint FILE_SHARE_WRITE = 0x02;
        public const uint OPEN_EXISTING = 3;
        public const int HIDP_STATUS_SUCCESS = 0x00110000;

        [StructLayout(LayoutKind.Sequential)]
        public struct SP_DEVICE_INTERFACE_DATA
        {
            public int cbSize;
            public Guid InterfaceClassGuid;
            public int Flags;
            public IntPtr Reserved;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct HIDD_ATTRIBUTES
        {
            public int Size;
            public ushort VendorID;
            public ushort ProductID;
            public ushort VersionNumber;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct HIDP_CAPS
        {
            public ushort Usage;
            public ushort UsagePage;
            public ushort InputReportByteLength;
            public ushort OutputReportByteLength;
            public ushort FeatureReportByteLength;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)]
            public ushort[] Reserved;
            public ushort NumberLinkCollectionNodes;
            public ushort NumberInputButtonCaps;
            public ushort NumberInputValueCaps;
            public ushort NumberInputDataIndices;
            public ushort NumberOutputButtonCaps;
            public ushort NumberOutputValueCaps;
            public ushort NumberOutputDataIndices;
            public ushort NumberFeatureButtonCaps;
            public ushort NumberFeatureValueCaps;
            public ushort NumberFeatureDataIndices;
        }

        [DllImport("hid.dll")]
        public static extern void HidD_GetHidGuid(out Guid hidGuid);

        [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern IntPtr SetupDiGetClassDevs(ref Guid classGuid, IntPtr enumerator, IntPtr hwndParent, uint flags);

        [DllImport("setupapi.dll", SetLastError = true)]
        public static extern bool SetupDiEnumDeviceInterfaces(IntPtr deviceInfoSet, IntPtr deviceInfoData, ref Guid interfaceClassGuid,
            uint memberIndex, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData);

        [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr deviceInfoSet, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData,
            IntPtr deviceInterfaceDetailData, int deviceInterfaceDetailDataSize, out int requiredSize, IntPtr deviceInfoData);

        [DllImport("setupapi.dll", SetLastError = true)]
        public static extern bool SetupDiDestroyDeviceInfoList(IntPtr deviceInfoSet);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern SafeFileHandle CreateFile(string fileName, uint desiredAccess, uint shareMode, IntPtr securityAttributes,
            uint creationDisposition, uint flagsAndAttributes, IntPtr templateFile);

        [DllImport("hid.dll", SetLastError = true)]
        public static extern bool HidD_GetAttributes(SafeFileHandle device, ref HIDD_ATTRIBUTES attributes);

        [DllImport("hid.dll", SetLastError = true)]
        public static extern bool HidD_GetPreparsedData(SafeFileHandle device, out IntPtr preparsedData);

        [DllImport("hid.dll", SetLastError = true)]
        public static extern bool HidD_FreePreparsedData(IntPtr preparsedData);

        [DllImport("hid.dll", SetLastError = true)]
        public static extern int HidP_GetCaps(IntPtr preparsedData, out HIDP_CAPS capabilities);

        [DllImport("hid.dll", SetLastError = true)]
        public static extern bool HidD_SetFeature(SafeFileHandle device, byte[] reportBuffer, int reportBufferLength);

        [DllImport("hid.dll", SetLastError = true)]
        public static extern bool HidD_GetFeature(SafeFileHandle device, byte[] reportBuffer, int reportBufferLength);
    }

    public static class HidEnumerator
    {
        // Mäuse/Tastaturen gehören exklusiv Windows. Ohne Lese-/Schreibrecht (Zugriff 0) geöffnet
        // lassen sich trotzdem Feature-Reports senden/abfragen (so macht es auch hidapi/OpenRGB).
        static SafeFileHandle OpenShared(string path)
        {
            return Native.CreateFile(path, 0, Native.FILE_SHARE_READ | Native.FILE_SHARE_WRITE, IntPtr.Zero,
                Native.OPEN_EXISTING, 0, IntPtr.Zero);
        }

        public static List<HidInfo> FindDevices(ushort vendorId)
        {
            List<HidInfo> result = new List<HidInfo>();
            Guid hidGuid;
            Native.HidD_GetHidGuid(out hidGuid);
            IntPtr set = Native.SetupDiGetClassDevs(ref hidGuid, IntPtr.Zero, IntPtr.Zero,
                Native.DIGCF_PRESENT | Native.DIGCF_DEVICEINTERFACE);
            if (set == IntPtr.Zero || set == new IntPtr(-1)) return result;
            try
            {
                for (uint index = 0; index < 1024; index++)
                {
                    Native.SP_DEVICE_INTERFACE_DATA data = new Native.SP_DEVICE_INTERFACE_DATA();
                    data.cbSize = Marshal.SizeOf(typeof(Native.SP_DEVICE_INTERFACE_DATA));
                    if (!Native.SetupDiEnumDeviceInterfaces(set, IntPtr.Zero, ref hidGuid, index, ref data)) break;

                    int required;
                    Native.SetupDiGetDeviceInterfaceDetail(set, ref data, IntPtr.Zero, 0, out required, IntPtr.Zero);
                    if (required <= 0) continue;
                    IntPtr detail = Marshal.AllocHGlobal(required);
                    string path;
                    try
                    {
                        Marshal.WriteInt32(detail, IntPtr.Size == 8 ? 8 : 6);   // cbSize von SP_DEVICE_INTERFACE_DETAIL_DATA_W
                        if (!Native.SetupDiGetDeviceInterfaceDetail(set, ref data, detail, required, out required, IntPtr.Zero)) continue;
                        path = Marshal.PtrToStringUni(new IntPtr(detail.ToInt64() + 4));
                    }
                    finally
                    {
                        Marshal.FreeHGlobal(detail);
                    }

                    // Nur Razer-Schnittstellen öffnen (USB: "vid_1532", Bluetooth: "...&00021532")
                    if (path == null || path.IndexOf(vendorId.ToString("x4"), StringComparison.OrdinalIgnoreCase) < 0) continue;
                    HidInfo info = Describe(path);
                    if (info != null && info.VendorId == vendorId) result.Add(info);
                }
            }
            finally
            {
                Native.SetupDiDestroyDeviceInfoList(set);
            }
            return result;
        }

        static HidInfo Describe(string path)
        {
            using (SafeFileHandle h = OpenShared(path))
            {
                if (h.IsInvalid) return null;
                Native.HIDD_ATTRIBUTES attr = new Native.HIDD_ATTRIBUTES();
                attr.Size = Marshal.SizeOf(typeof(Native.HIDD_ATTRIBUTES));
                if (!Native.HidD_GetAttributes(h, ref attr)) return null;

                HidInfo info = new HidInfo();
                info.Path = path;
                info.VendorId = attr.VendorID;
                info.ProductId = attr.ProductID;
                info.Version = attr.VersionNumber;

                IntPtr pp;
                if (Native.HidD_GetPreparsedData(h, out pp))
                {
                    try
                    {
                        Native.HIDP_CAPS caps;
                        if (Native.HidP_GetCaps(pp, out caps) == Native.HIDP_STATUS_SUCCESS)
                        {
                            info.UsagePage = caps.UsagePage;
                            info.Usage = caps.Usage;
                            info.FeatureLength = caps.FeatureReportByteLength;
                        }
                    }
                    finally
                    {
                        Native.HidD_FreePreparsedData(pp);
                    }
                }

                // Bewusst ohne HidD_GetProductString: das wäre jedes Mal eine Anfrage an das Gerät selbst.
                return info;
            }
        }

        public static WinHidChannel Open(HidInfo info)
        {
            SafeFileHandle h = OpenShared(info.Path);
            if (h.IsInvalid)
            {
                int err = Marshal.GetLastWin32Error();
                h.Dispose();
                throw new IOException("Gerät lässt sich nicht öffnen (Win32-Fehler " + err + ").");
            }
            return new WinHidChannel(h, info.FeatureLength);
        }
    }

    public sealed class WinHidChannel : IHidChannel, IDisposable
    {
        readonly SafeFileHandle _handle;
        readonly int _length;
        int _lastError;

        public WinHidChannel(SafeFileHandle handle, int featureLength)
        {
            _handle = handle;
            _length = featureLength;
        }

        public int FeatureLength { get { return _length; } }
        public int LastError { get { return _lastError; } }

        public bool SetFeature(byte[] buffer)
        {
            bool ok = Native.HidD_SetFeature(_handle, buffer, buffer.Length);
            _lastError = ok ? 0 : Marshal.GetLastWin32Error();
            return ok;
        }

        public bool GetFeature(byte[] buffer)
        {
            bool ok = Native.HidD_GetFeature(_handle, buffer, buffer.Length);
            _lastError = ok ? 0 : Marshal.GetLastWin32Error();
            return ok;
        }

        public void Dispose()
        {
            _handle.Dispose();
        }
    }

    // #################################################################################
    // Overlay: kleine Karte in einer Bildschirmecke. Eigenes Fenster mit Pro-Pixel-Transparenz, das nie den Fokus
    // bekommt (WS_EX_NOACTIVATE), Klicks durchlässt (WS_EX_TRANSPARENT) und nicht in Taskleiste/Alt+Tab erscheint.
    // Kein Hook, keine Injection ins Spiel – im exklusiven Vollbild zeigt Windows es deshalb einfach nicht an.
    // #################################################################################

    public enum OverlayIcon { Space, Key, MouseLeft, MouseRight, MouseMiddle, Mouse, Generic, Warning }

    public sealed class OverlayContent
    {
        public string Title = "";
        public string Subtitle = "";
        public uint Accent = 0xFF4ADE80;
        public OverlayIcon Icon = OverlayIcon.Generic;
        public string KeyLabel;          // Beschriftung im Tasten-Symbol (optional)
        public bool Warn;                // Untertitel als Hinweis (gelb)
        public int BatteryPercent = -1;  // Akkustand der Maus 0–100 (rechts in der Karte), -1 = nicht anzeigen
        public bool BatteryCharging;
    }

    // =================================================================================
    //  Zeichnen (reines GDI+, ohne Fenster -> auch unter Mono testbar)
    // =================================================================================
    public static class OverlayRenderer
    {
        public const uint WarnColor = 0xFFFBBF24;

        // Maße in DIP (bei 100 % Skalierung = Pixel)
        const float CardHeight = 64f;
        const float Radius = 14f;
        const float Tile = 38f;
        const float TileRadius = 11f;
        const float Inset = 13f;          // Abstand Kachel <-> Kartenrand
        const float Gap = 13f;            // Abstand Kachel <-> Text
        const float RightPad = 20f;
        const float MinWidth = 210f;
        const float MaxWidth = 440f;
        const float TitlePx = 15.5f;
        const float SubtitlePx = 12f;
        const float LineGap = 2f;
        public const float PadSide = 18f;  // Rand für den Schatten
        public const float PadTop = 14f;
        public const float PadBottom = 24f;
        const float ShadowBlur = 9f;
        const float ShadowOffset = 4f;
        const int ShadowAlpha = 120;
        // Akku-Anzeige rechts in der Karte: "82 %" + Batteriesymbol
        const float BatteryGap = 16f;      // Abstand Text <-> Akku-Anzeige
        const float BatteryW = 22f;        // Batteriekörper
        const float BatteryH = 11.5f;
        const float BatteryNub = 2.2f;     // Pluspol
        const float BatteryTextGap = 6f;   // Prozentzahl <-> Symbol
        public const uint LowColor = 0xFFF87171;      // Akku ≤ 10 %
        public const uint ChargeColor = 0xFF4ADE80;   // Akku lädt

        static FontFamily _titleFamily, _textFamily;
        static FontStyle _titleStyle;

        static void EnsureFonts()
        {
            if (_titleFamily != null) return;
            _titleFamily = TryFamily("Segoe UI Semibold");
            _titleStyle = FontStyle.Regular;
            if (_titleFamily == null)
            {
                _titleFamily = TryFamily("Segoe UI") ?? FontFamily.GenericSansSerif;
                _titleStyle = FontStyle.Bold;
            }
            _textFamily = TryFamily("Segoe UI") ?? FontFamily.GenericSansSerif;
        }

        static FontFamily TryFamily(string name)
        {
            try
            {
                FontFamily f = new FontFamily(name);
                // libgdiplus liefert bei unbekannten Namen still eine Ersatzschrift
                return string.Equals(f.Name, name, StringComparison.OrdinalIgnoreCase) ? f : null;
            }
            catch (ArgumentException)
            {
                return null;
            }
        }

        static Color C(uint argb)
        {
            return Color.FromArgb(unchecked((int)argb));
        }

        static Color WithAlpha(uint argb, int alpha)
        {
            Color c = C(argb);
            return Color.FromArgb(alpha, c.R, c.G, c.B);
        }

        // Liefert eine 32-Bit-Grafik mit vormultipliziertem Alpha (wie UpdateLayeredWindow sie braucht).
        // card = Lage der Karte innerhalb der Grafik (der Rest ist Schatten).
        public static Bitmap Render(OverlayContent content, float scale, out Rectangle card)
        {
            EnsureFonts();
            float s = scale <= 0 ? 1f : scale;
            using (Font titleFont = new Font(_titleFamily, TitlePx * s, _titleStyle, GraphicsUnit.Pixel))
            using (Font subFont = new Font(_textFamily, SubtitlePx * s, FontStyle.Regular, GraphicsUnit.Pixel))
            using (StringFormat fmt = (StringFormat)StringFormat.GenericTypographic.Clone())
            {
                fmt.FormatFlags |= StringFormatFlags.NoWrap;

                bool hasBattery = content.BatteryPercent >= 0;
                string batteryText = hasBattery ? Math.Min(100, content.BatteryPercent).ToString(CultureInfo.InvariantCulture) + " %" : "";
                float titleW, subW, titleH, subH, batteryTextW = 0;
                using (Bitmap probe = new Bitmap(1, 1, PixelFormat.Format32bppPArgb))
                using (Graphics g = Graphics.FromImage(probe))
                {
                    g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
                    titleW = g.MeasureString(content.Title ?? "", titleFont, PointF.Empty, fmt).Width;
                    subW = string.IsNullOrEmpty(content.Subtitle) ? 0 : g.MeasureString(content.Subtitle, subFont, PointF.Empty, fmt).Width;
                    if (hasBattery) batteryTextW = g.MeasureString(batteryText, subFont, PointF.Empty, fmt).Width;
                    titleH = titleFont.GetHeight(g);
                    subH = subFont.GetHeight(g);
                }

                float textX = Inset + Tile + Gap;
                // Platz für die Akku-Anzeige kommt zur Textbreite dazu (der Text behält seine Höchstbreite)
                float batteryBlock = hasBattery ? (BatteryGap + BatteryTextGap + BatteryW + BatteryNub) * s + batteryTextW : 0f;
                float width = Math.Max(MinWidth * s, Math.Min(MaxWidth * s + batteryBlock, (textX + RightPad) * s + Math.Max(titleW, subW) + 2 + batteryBlock));
                width = (float)Math.Ceiling(width);
                float height = (float)Math.Ceiling(CardHeight * s);

                int padSide = (int)Math.Ceiling(PadSide * s);
                int padTop = (int)Math.Ceiling(PadTop * s);
                int padBottom = (int)Math.Ceiling(PadBottom * s);
                int bw = (int)width + 2 * padSide;
                int bh = (int)height + padTop + padBottom;
                card = new Rectangle(padSide, padTop, (int)width, (int)height);

                Bitmap bmp = new Bitmap(bw, bh, PixelFormat.Format32bppPArgb);
                try
                {
                    RectangleF cardF = new RectangleF(card.X, card.Y, card.Width, card.Height);
                    DrawShadow(bmp, cardF, Radius * s, ShadowBlur * s, ShadowOffset * s, ShadowAlpha);

                    using (Graphics g = Graphics.FromImage(bmp))
                    {
                        g.SmoothingMode = SmoothingMode.AntiAlias;
                        g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                        g.CompositingQuality = CompositingQuality.HighQuality;
                        g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;

                        // Karte: fast deckend dunkel, feiner heller Rand
                        using (GraphicsPath path = RoundRect(cardF, Radius * s))
                        {
                            using (SolidBrush fill = new SolidBrush(Color.FromArgb(240, 22, 22, 26)))
                            {
                                g.FillPath(fill, path);
                            }
                            RectangleF top = new RectangleF(cardF.X, cardF.Y, cardF.Width, cardF.Height * 0.55f);
                            using (LinearGradientBrush sheen = new LinearGradientBrush(new PointF(0, top.Top), new PointF(0, top.Bottom + 1),
                                       Color.FromArgb(14, 255, 255, 255), Color.FromArgb(0, 255, 255, 255)))
                            using (GraphicsPath topPath = RoundRect(cardF, Radius * s))
                            {
                                g.SetClip(top);
                                g.FillPath(sheen, topPath);
                                g.ResetClip();
                            }
                        }
                        RectangleF border = new RectangleF(cardF.X + 0.5f, cardF.Y + 0.5f, cardF.Width - 1f, cardF.Height - 1f);
                        using (GraphicsPath bpath = RoundRect(border, Radius * s - 0.5f))
                        using (Pen pen = new Pen(Color.FromArgb(30, 255, 255, 255), 1f))
                        {
                            g.DrawPath(pen, bpath);
                        }

                        // Symbol-Kachel
                        uint accent = content.Icon == OverlayIcon.Warning ? WarnColor : content.Accent;
                        RectangleF tile = new RectangleF(cardF.X + Inset * s, cardF.Y + (cardF.Height - Tile * s) / 2f, Tile * s, Tile * s);
                        using (GraphicsPath tpath = RoundRect(tile, TileRadius * s))
                        using (SolidBrush tfill = new SolidBrush(WithAlpha(accent, 38)))
                        {
                            g.FillPath(tfill, tpath);
                        }
                        DrawGlyph(g, content, tile, accent, s);

                        // Akku: rechts, vertikal mittig
                        if (hasBattery)
                        {
                            DrawBattery(g, content, batteryText, batteryTextW, subFont, subH, fmt, cardF.Right - RightPad * s, cardF.Y + cardF.Height / 2f, s);
                        }

                        // Text: zwei Zeilen, gemeinsam vertikal zentriert
                        float tx = cardF.X + textX * s;
                        float maxText = cardF.Right - RightPad * s - tx - batteryBlock;
                        bool hasSub = !string.IsNullOrEmpty(content.Subtitle);
                        float block = hasSub ? titleH + LineGap * s + subH : titleH;
                        float y = cardF.Y + (cardF.Height - block) / 2f;
                        using (SolidBrush titleBrush = new SolidBrush(Color.FromArgb(255, 244, 244, 246)))
                        {
                            g.DrawString(Fit(g, content.Title, titleFont, maxText, fmt), titleFont, titleBrush, new PointF(tx, y), fmt);
                        }
                        if (hasSub)
                        {
                            Color subColor = content.Warn ? C(WarnColor) : Color.FromArgb(255, 160, 160, 170);
                            using (SolidBrush subBrush = new SolidBrush(subColor))
                            {
                                g.DrawString(Fit(g, content.Subtitle, subFont, maxText, fmt), subFont, subBrush,
                                    new PointF(tx, y + titleH + LineGap * s), fmt);
                            }
                        }
                    }
                    return bmp;
                }
                catch
                {
                    bmp.Dispose();
                    throw;
                }
            }
        }

        // "82 %" + Batteriesymbol, rechtsbündig an 'right', vertikal um 'cy' zentriert.
        // Farben: hell; gelb bis 20 %, rot bis 10 %; beim Laden grün mit Blitz.
        static void DrawBattery(Graphics g, OverlayContent content, string text, float textW, Font font, float fontH,
                                StringFormat fmt, float right, float cy, float s)
        {
            int percent = Math.Max(0, Math.Min(100, content.BatteryPercent));
            bool low = !content.BatteryCharging && percent <= 20;
            Color level = content.BatteryCharging ? C(ChargeColor)
                        : percent <= 10 ? C(LowColor)
                        : percent <= 20 ? C(WarnColor)
                        : Color.FromArgb(255, 228, 228, 234);
            Color outline = low ? Color.FromArgb(220, level.R, level.G, level.B) : Color.FromArgb(255, 150, 150, 160);
            Color textColor = low ? level : Color.FromArgb(255, 200, 200, 208);

            float bw = BatteryW * s, bh = BatteryH * s, nub = BatteryNub * s;
            float stroke = Math.Max(1f, 1.4f * s);
            RectangleF body = new RectangleF(right - nub - bw, cy - bh / 2f, bw, bh);

            // Körper (Umriss), Pluspol, Füllung je nach Ladestand
            RectangleF outer = new RectangleF(body.X + stroke / 2f, body.Y + stroke / 2f, body.Width - stroke, body.Height - stroke);
            using (GraphicsPath p = RoundRect(outer, 3f * s))
            using (Pen pen = new Pen(outline, stroke))
            {
                g.DrawPath(pen, p);
            }
            RectangleF tip = new RectangleF(body.Right + 0.6f * s, cy - 2.4f * s, nub - 0.6f * s, 4.8f * s);
            using (GraphicsPath p = RoundRect(tip, 1f * s))
            using (SolidBrush b = new SolidBrush(outline))
            {
                g.FillPath(b, p);
            }
            float inset = stroke + 1.4f * s;
            RectangleF inner = new RectangleF(body.X + inset, body.Y + inset, body.Width - 2 * inset, body.Height - 2 * inset);
            float fillW = percent <= 0 ? 0f : Math.Max(1.6f * s, inner.Width * percent / 100f);
            if (fillW > 0f)
            {
                using (GraphicsPath p = RoundRect(new RectangleF(inner.X, inner.Y, fillW, inner.Height), 1.4f * s))
                using (SolidBrush b = new SolidBrush(level))
                {
                    g.FillPath(b, p);
                }
            }
            if (content.BatteryCharging)
            {
                // Blitz mittig, mit dunklem Rand – lesbar auf der Füllung und auf dem leeren Teil
                float bx = body.X + body.Width / 2f, by = cy, u = s;
                PointF[] bolt =
                {
                    new PointF(bx + 1.3f * u, by - 4.9f * u), new PointF(bx - 3.0f * u, by + 0.7f * u), new PointF(bx - 0.3f * u, by + 0.7f * u),
                    new PointF(bx - 1.3f * u, by + 4.9f * u), new PointF(bx + 3.0f * u, by - 0.7f * u), new PointF(bx + 0.3f * u, by - 0.7f * u)
                };
                using (GraphicsPath p = new GraphicsPath())
                {
                    p.AddPolygon(bolt);
                    using (Pen edge = new Pen(Color.FromArgb(255, 22, 22, 26), 1.6f * s))
                    {
                        edge.LineJoin = LineJoin.Round;
                        g.DrawPath(edge, p);
                    }
                    using (SolidBrush fill = new SolidBrush(Color.FromArgb(255, 250, 250, 250)))
                    {
                        g.FillPath(fill, p);
                    }
                }
            }

            // Prozentzahl links neben dem Symbol
            float tx = body.X - BatteryTextGap * s - textW;
            using (SolidBrush b = new SolidBrush(textColor))
            {
                g.DrawString(text, font, b, new PointF(tx, cy - fontH / 2f), fmt);
            }
        }

        // Zu lange Texte mit "…" kürzen (selbst gemessen – verhält sich überall gleich)
        static string Fit(Graphics g, string text, Font font, float maxWidth, StringFormat fmt)
        {
            if (string.IsNullOrEmpty(text)) return "";
            if (g.MeasureString(text, font, PointF.Empty, fmt).Width <= maxWidth) return text;
            const string Ellipsis = "\u2026";
            int lo = 0, hi = text.Length;
            while (lo < hi)
            {
                int mid = (lo + hi + 1) / 2;
                string candidate = text.Substring(0, mid).TrimEnd() + Ellipsis;
                if (g.MeasureString(candidate, font, PointF.Empty, fmt).Width <= maxWidth) lo = mid;
                else hi = mid - 1;
            }
            return text.Substring(0, lo).TrimEnd() + Ellipsis;
        }

        static void DrawGlyph(Graphics g, OverlayContent content, RectangleF tile, uint accent, float s)
        {
            float u = tile.Width / Tile * 1f;          // 1 DIP in der Kachel
            float cx = tile.X + tile.Width / 2f;
            float cy = tile.Y + tile.Height / 2f;
            Color color = C(accent);
            using (SolidBrush brush = new SolidBrush(color))
            using (Pen pen = new Pen(color, 2.2f * u))
            {
                pen.StartCap = LineCap.Round;
                pen.EndCap = LineCap.Round;
                pen.LineJoin = LineJoin.Round;
                switch (content.Icon)
                {
                    case OverlayIcon.Space:
                    {
                        // ␣ – das Symbol für die Leertaste
                        float w = 20f * u, h = 7f * u;
                        float x0 = cx - w / 2f, x1 = cx + w / 2f;
                        float yt = cy - h / 2f + 1f * u, yb = yt + h;
                        g.DrawLines(pen, new PointF[] { new PointF(x0, yt), new PointF(x0, yb), new PointF(x1, yb), new PointF(x1, yt) });
                        break;
                    }
                    case OverlayIcon.Key:
                    {
                        RectangleF cap = new RectangleF(cx - 10.5f * u, cy - 9.5f * u, 21f * u, 19f * u);
                        pen.Width = 1.8f * u;
                        using (GraphicsPath p = RoundRect(cap, 4.5f * u)) g.DrawPath(pen, p);
                        if (!string.IsNullOrEmpty(content.KeyLabel))
                        {
                            float px = content.KeyLabel.Length > 1 ? 8.5f : 10.5f;
                            using (Font f = new Font(_titleFamily, px * u, _titleStyle, GraphicsUnit.Pixel))
                            using (StringFormat sf = new StringFormat())
                            {
                                sf.Alignment = StringAlignment.Center;
                                sf.LineAlignment = StringAlignment.Center;
                                g.DrawString(content.KeyLabel, f, brush, new RectangleF(cap.X, cap.Y + 0.5f * u, cap.Width, cap.Height), sf);
                            }
                        }
                        else
                        {
                            g.DrawLine(pen, cx - 4.5f * u, cy + 3.5f * u, cx + 4.5f * u, cy + 3.5f * u);
                        }
                        break;
                    }
                    case OverlayIcon.MouseLeft:
                    case OverlayIcon.MouseRight:
                    case OverlayIcon.MouseMiddle:
                    case OverlayIcon.Mouse:
                        DrawMouse(g, content.Icon, cx, cy, u, pen, brush);
                        break;
                    case OverlayIcon.Warning:
                    {
                        pen.Width = 2.6f * u;
                        g.DrawLine(pen, cx, cy - 8f * u, cx, cy + 2.5f * u);
                        float r = 1.7f * u;
                        g.FillEllipse(brush, cx - r, cy + 7.5f * u - r, 2 * r, 2 * r);
                        break;
                    }
                    default:
                    {
                        float r = 5f * u;
                        g.FillEllipse(brush, cx - r, cy - r, 2 * r, 2 * r);
                        break;
                    }
                }
            }
        }

        static void DrawMouse(Graphics g, OverlayIcon icon, float cx, float cy, float u, Pen pen, Brush brush)
        {
            float w = 15f * u, h = 22f * u, r = w / 2f;
            float left = cx - r, top = cy - h / 2f, right = cx + r;
            float split = top + 8.5f * u;                     // Unterkante der Tasten
            RectangleF arc = new RectangleF(left, top, w, w);

            if (icon == OverlayIcon.MouseRight || icon == OverlayIcon.MouseLeft)
            {
                using (GraphicsPath button = new GraphicsPath())
                {
                    if (icon == OverlayIcon.MouseRight)
                    {
                        button.AddArc(arc, 270f, 90f);              // oben Mitte -> rechts
                        button.AddLine(right, top + r, right, split);
                        button.AddLine(right, split, cx, split);
                    }
                    else
                    {
                        button.AddArc(arc, 180f, 90f);              // links -> oben Mitte
                        button.AddLine(cx, top, cx, split);
                        button.AddLine(cx, split, left, split);
                    }
                    button.CloseFigure();
                    g.FillPath(brush, button);
                }
            }

            pen.Width = 1.8f * u;
            using (GraphicsPath body = RoundRect(new RectangleF(left, top, w, h), r))
            {
                g.DrawPath(pen, body);
            }
            g.DrawLine(pen, left + 0.6f * u, split, right - 0.6f * u, split);
            g.DrawLine(pen, cx, top + 0.6f * u, cx, split);
            if (icon == OverlayIcon.MouseMiddle)
            {
                RectangleF wheel = new RectangleF(cx - 1.6f * u, top + 2.6f * u, 3.2f * u, 4.6f * u);
                using (GraphicsPath p = RoundRect(wheel, 1.6f * u)) g.FillPath(brush, p);
            }
        }

        public static GraphicsPath RoundRect(RectangleF r, float radius)
        {
            GraphicsPath p = new GraphicsPath();
            float d = Math.Min(radius * 2f, Math.Min(r.Width, r.Height));
            if (d <= 0.5f)
            {
                p.AddRectangle(r);
                return p;
            }
            p.AddArc(r.X, r.Y, d, d, 180f, 90f);
            p.AddArc(r.Right - d, r.Y, d, d, 270f, 90f);
            p.AddArc(r.Right - d, r.Bottom - d, d, d, 0f, 90f);
            p.AddArc(r.X, r.Bottom - d, d, d, 90f, 90f);
            p.CloseFigure();
            return p;
        }

        // Weicher Schatten: Kartenform als Maske, dreifach box-gefiltert (≈ Gauß), als Schwarz mit Alpha eingetragen.
        static void DrawShadow(Bitmap target, RectangleF card, float radius, float blur, float offsetY, int maxAlpha)
        {
            int w = target.Width, h = target.Height;
            byte[] mask = new byte[w * h];
            using (Bitmap m = new Bitmap(w, h, PixelFormat.Format32bppArgb))
            {
                using (Graphics g = Graphics.FromImage(m))
                {
                    g.SmoothingMode = SmoothingMode.AntiAlias;
                    RectangleF shadowRect = new RectangleF(card.X + 1f, card.Y + offsetY, card.Width - 2f, card.Height);
                    using (GraphicsPath p = RoundRect(shadowRect, radius))
                    using (SolidBrush b = new SolidBrush(Color.Black))
                    {
                        g.FillPath(b, p);
                    }
                }
                byte[] px = ReadPixels(m, PixelFormat.Format32bppArgb);
                for (int i = 0; i < mask.Length; i++) mask[i] = px[i * 4 + 3];
            }

            int radiusPx = Math.Max(1, (int)Math.Round(blur / 2f));
            for (int pass = 0; pass < 3; pass++)
            {
                BoxBlurHorizontal(mask, w, h, radiusPx);
                BoxBlurVertical(mask, w, h, radiusPx);
            }

            byte[] outPx = new byte[w * h * 4];
            for (int i = 0; i < mask.Length; i++)
            {
                outPx[i * 4 + 3] = (byte)(mask[i] * maxAlpha / 255);   // vormultipliziertes Schwarz: B=G=R=0
            }
            WritePixels(target, outPx);
        }

        static void BoxBlurHorizontal(byte[] a, int w, int h, int r)
        {
            byte[] row = new byte[w];
            int span = 2 * r + 1;
            for (int y = 0; y < h; y++)
            {
                int o = y * w;
                Array.Copy(a, o, row, 0, w);
                int sum = 0;
                for (int i = -r; i <= r; i++) sum += At(row, i);
                for (int x = 0; x < w; x++)
                {
                    a[o + x] = (byte)(sum / span);
                    sum += At(row, x + r + 1) - At(row, x - r);
                }
            }
        }

        static void BoxBlurVertical(byte[] a, int w, int h, int r)
        {
            byte[] col = new byte[h];
            int span = 2 * r + 1;
            for (int x = 0; x < w; x++)
            {
                for (int y = 0; y < h; y++) col[y] = a[y * w + x];
                int sum = 0;
                for (int i = -r; i <= r; i++) sum += At(col, i);
                for (int y = 0; y < h; y++)
                {
                    a[y * w + x] = (byte)(sum / span);
                    sum += At(col, y + r + 1) - At(col, y - r);
                }
            }
        }

        // Außerhalb der Grafik ist alles transparent (0) – deshalb nicht am Rand spiegeln, sondern 0 liefern.
        static int At(byte[] a, int i)
        {
            return i < 0 || i >= a.Length ? 0 : a[i];
        }

        public static byte[] ReadPixels(Bitmap bmp, PixelFormat format)
        {
            Rectangle r = new Rectangle(0, 0, bmp.Width, bmp.Height);
            BitmapData data = bmp.LockBits(r, ImageLockMode.ReadOnly, format);
            try
            {
                int rowBytes = bmp.Width * 4;
                byte[] result = new byte[rowBytes * bmp.Height];
                for (int y = 0; y < bmp.Height; y++)
                {
                    Marshal.Copy(new IntPtr(data.Scan0.ToInt64() + (long)y * data.Stride), result, y * rowBytes, rowBytes);
                }
                return result;
            }
            finally
            {
                bmp.UnlockBits(data);
            }
        }

        static void WritePixels(Bitmap bmp, byte[] pixels)
        {
            Rectangle r = new Rectangle(0, 0, bmp.Width, bmp.Height);
            BitmapData data = bmp.LockBits(r, ImageLockMode.WriteOnly, PixelFormat.Format32bppPArgb);
            try
            {
                int rowBytes = bmp.Width * 4;
                for (int y = 0; y < bmp.Height; y++)
                {
                    Marshal.Copy(pixels, y * rowBytes, new IntPtr(data.Scan0.ToInt64() + (long)y * data.Stride), rowBytes);
                }
            }
            finally
            {
                bmp.UnlockBits(data);
            }
        }
    }

    // =================================================================================
    //  Windows-Funktionen für Overlay, Hotkey und DPI
    // =================================================================================
    internal static class Win32
    {
        public const int WS_POPUP = unchecked((int)0x80000000);
        public const int WS_EX_TOPMOST = 0x00000008;
        public const int WS_EX_TRANSPARENT = 0x00000020;
        public const int WS_EX_TOOLWINDOW = 0x00000080;
        public const int WS_EX_LAYERED = 0x00080000;
        public const int WS_EX_NOACTIVATE = 0x08000000;
        public const int SW_HIDE = 0;
        public const int SW_SHOWNOACTIVATE = 4;
        public const uint SWP_NOSIZE = 0x0001;
        public const uint SWP_NOMOVE = 0x0002;
        public const uint SWP_NOACTIVATE = 0x0010;
        public const uint SWP_SHOWWINDOW = 0x0040;
        public const uint SWP_NOOWNERZORDER = 0x0200;
        public static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);
        public const uint ULW_ALPHA = 0x02;
        public const byte AC_SRC_OVER = 0x00;
        public const byte AC_SRC_ALPHA = 0x01;
        public const uint MONITOR_DEFAULTTOPRIMARY = 1;
        public const int WM_NCHITTEST = 0x0084;
        public const int WM_MOUSEACTIVATE = 0x0021;
        public const int HTTRANSPARENT = -1;
        public const int MA_NOACTIVATE = 3;

        [StructLayout(LayoutKind.Sequential)]
        public struct POINT
        {
            public int X;
            public int Y;
            public POINT(int x, int y) { X = x; Y = y; }
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct SIZE
        {
            public int CX;
            public int CY;
            public SIZE(int cx, int cy) { CX = cx; CY = cy; }
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct RECT
        {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct MONITORINFO
        {
            public int cbSize;
            public RECT rcMonitor;
            public RECT rcWork;
            public uint dwFlags;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public struct BLENDFUNCTION
        {
            public byte BlendOp;
            public byte BlendFlags;
            public byte SourceConstantAlpha;
            public byte AlphaFormat;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct BITMAPINFOHEADER
        {
            public int biSize;
            public int biWidth;
            public int biHeight;
            public short biPlanes;
            public short biBitCount;
            public int biCompression;
            public int biSizeImage;
            public int biXPelsPerMeter;
            public int biYPelsPerMeter;
            public int biClrUsed;
            public int biClrImportant;
        }

        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool UpdateLayeredWindow(IntPtr hwnd, IntPtr hdcDst, ref POINT pptDst, ref SIZE psize, IntPtr hdcSrc,
            ref POINT pptSrc, uint crKey, ref BLENDFUNCTION pblend, uint dwFlags);

        [DllImport("user32.dll")]
        public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int x, int y, int cx, int cy, uint flags);

        [DllImport("user32.dll")]
        public static extern IntPtr GetForegroundWindow();

        [DllImport("user32.dll")]
        public static extern IntPtr MonitorFromWindow(IntPtr hwnd, uint flags);

        [DllImport("user32.dll")]
        public static extern IntPtr MonitorFromPoint(POINT pt, uint flags);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        public static extern bool GetMonitorInfo(IntPtr hMonitor, ref MONITORINFO info);

        [DllImport("user32.dll")]
        public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        public static extern int GetClassName(IntPtr hWnd, StringBuilder className, int maxCount);

        [DllImport("shcore.dll")]
        public static extern int GetDpiForMonitor(IntPtr hmonitor, int dpiType, out uint dpiX, out uint dpiY);

        [DllImport("user32.dll")]
        public static extern bool SetProcessDpiAwarenessContext(IntPtr value);

        [DllImport("shcore.dll")]
        public static extern int SetProcessDpiAwareness(int value);

        [DllImport("user32.dll")]
        public static extern bool SetProcessDPIAware();

        [DllImport("gdi32.dll")]
        public static extern IntPtr CreateCompatibleDC(IntPtr hdc);

        [DllImport("gdi32.dll")]
        public static extern bool DeleteDC(IntPtr hdc);

        [DllImport("gdi32.dll")]
        public static extern IntPtr SelectObject(IntPtr hdc, IntPtr obj);

        [DllImport("gdi32.dll")]
        public static extern bool DeleteObject(IntPtr obj);

        [DllImport("gdi32.dll")]
        public static extern IntPtr CreateDIBSection(IntPtr hdc, ref BITMAPINFOHEADER bmi, uint usage, out IntPtr bits, IntPtr section, uint offset);

        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool RegisterHotKey(IntPtr hWnd, int id, uint modifiers, uint vk);

        [DllImport("user32.dll")]
        public static extern bool UnregisterHotKey(IntPtr hWnd, int id);

        [DllImport("user32.dll")]
        public static extern bool PostMessage(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);

        // Pro-Monitor-DPI (scharf auf 4K/150 %); muss vor dem ersten Fenster passieren.
        public static void EnableDpiAwareness()
        {
            try
            {
                if (SetProcessDpiAwarenessContext(new IntPtr(-4))) return;     // PER_MONITOR_AWARE_V2 (Win10 1703+)
            }
            catch (EntryPointNotFoundException)
            {
            }
            try
            {
                if (SetProcessDpiAwareness(2) == 0) return;                    // PROCESS_PER_MONITOR_DPI_AWARE (Win8.1+)
            }
            catch (DllNotFoundException)
            {
            }
            catch (EntryPointNotFoundException)
            {
            }
            try
            {
                SetProcessDPIAware();
            }
            catch (EntryPointNotFoundException)
            {
            }
        }
    }

    // =================================================================================
    //  Fenster (nur Windows)
    // =================================================================================
    public sealed class OverlayWindow : NativeWindow, IDisposable
    {
        const double FadeInMs = 160;
        const double FadeOutMs = 320;
        const float MarginDip = 24f;
        const float SlideDip = 10f;

        readonly System.Windows.Forms.Timer _timer = new System.Windows.Forms.Timer();
        readonly Stopwatch _clock = new Stopwatch();
        readonly double _totalMs;
        readonly Corner _corner;
        IntPtr _memDc = IntPtr.Zero;
        IntPtr _dib = IntPtr.Zero;
        IntPtr _oldObject = IntPtr.Zero;
        int _w, _h, _x, _y;
        float _scale = 1f;
        bool _visible;
        bool _skipFadeIn;

        public OverlayWindow(double seconds, Corner corner)
        {
            _totalMs = Math.Max(1.0, seconds) * 1000.0;
            _corner = corner;
            CreateParams cp = new CreateParams();
            cp.Caption = "TriggerSwitch";
            cp.Style = Win32.WS_POPUP;
            cp.ExStyle = Win32.WS_EX_LAYERED | Win32.WS_EX_TRANSPARENT | Win32.WS_EX_TOOLWINDOW | Win32.WS_EX_NOACTIVATE | Win32.WS_EX_TOPMOST;
            cp.X = 0;
            cp.Y = 0;
            cp.Width = 1;
            cp.Height = 1;
            CreateHandle(cp);
            _timer.Interval = 15;
            _timer.Tick += OnTick;
        }

        public void Show(OverlayContent content)
        {
            Placement place = Placement.Find();
            _scale = place.Scale;
            Rectangle card;
            using (Bitmap bmp = OverlayRenderer.Render(content, _scale, out card))
            {
                SetBitmap(bmp);
            }

            int margin = (int)Math.Round(MarginDip * _scale);
            Win32.RECT a = place.Area;
            int cardX, cardY;
            switch (_corner)
            {
                case Corner.BottomLeft: case Corner.TopLeft: cardX = a.Left + margin; break;
                case Corner.BottomCenter: case Corner.TopCenter: cardX = a.Left + (a.Right - a.Left - card.Width) / 2; break;
                default: cardX = a.Right - margin - card.Width; break;
            }
            switch (_corner)
            {
                case Corner.TopLeft: case Corner.TopRight: case Corner.TopCenter: cardY = a.Top + margin; break;
                default: cardY = a.Bottom - margin - card.Height; break;
            }
            _x = cardX - card.X;
            _y = cardY - card.Y;

            // Schon sichtbar (Hotkey schnell hintereinander): nur Inhalt tauschen und Zeit neu starten
            _skipFadeIn = _visible;
            _clock.Reset();
            _clock.Start();
            Apply(_skipFadeIn ? 1.0 : 0.0, _skipFadeIn ? 0 : SlideOffset(1.0));
            if (!_visible)
            {
                Win32.ShowWindow(Handle, Win32.SW_SHOWNOACTIVATE);
                _visible = true;
            }
            // ganz nach oben (auch über "immer oben"-Fenstern wie Spielen im Vollbild), ohne Aktivierung
            Win32.SetWindowPos(Handle, Win32.HWND_TOPMOST, 0, 0, 0, 0,
                Win32.SWP_NOMOVE | Win32.SWP_NOSIZE | Win32.SWP_NOACTIVATE | Win32.SWP_NOOWNERZORDER | Win32.SWP_SHOWWINDOW);
            _timer.Start();
        }

        int SlideOffset(double remaining)
        {
            int px = (int)Math.Round(SlideDip * _scale * remaining);
            bool top = _corner == Corner.TopLeft || _corner == Corner.TopRight || _corner == Corner.TopCenter;
            return top ? -px : px;
        }

        void OnTick(object sender, EventArgs e)
        {
            double t = _clock.Elapsed.TotalMilliseconds;
            double fadeOutStart = Math.Max(FadeInMs, _totalMs - FadeOutMs);
            if (!_skipFadeIn && t < FadeInMs)
            {
                double p = EaseOut(t / FadeInMs);
                Apply(p, SlideOffset(1.0 - p));
            }
            else if (t < fadeOutStart)
            {
                Apply(1.0, 0);
            }
            else if (t < fadeOutStart + FadeOutMs)
            {
                double p = (t - fadeOutStart) / FadeOutMs;
                Apply(1.0 - p * p, 0);
            }
            else
            {
                HideNow();
            }
        }

        static double EaseOut(double p)
        {
            p = Math.Max(0, Math.Min(1, p));
            double q = 1 - p;
            return 1 - q * q * q;
        }

        void Apply(double opacity, int offsetY)
        {
            if (_memDc == IntPtr.Zero) return;
            Win32.POINT dst = new Win32.POINT(_x, _y + offsetY);
            Win32.SIZE size = new Win32.SIZE(_w, _h);
            Win32.POINT src = new Win32.POINT(0, 0);
            Win32.BLENDFUNCTION blend = new Win32.BLENDFUNCTION();
            blend.BlendOp = Win32.AC_SRC_OVER;
            blend.SourceConstantAlpha = (byte)Math.Max(0, Math.Min(255, (int)Math.Round(opacity * 255)));
            blend.AlphaFormat = Win32.AC_SRC_ALPHA;
            Win32.UpdateLayeredWindow(Handle, IntPtr.Zero, ref dst, ref size, _memDc, ref src, 0, ref blend, Win32.ULW_ALPHA);
        }

        void SetBitmap(Bitmap bmp)
        {
            FreeBitmap();
            _w = bmp.Width;
            _h = bmp.Height;
            Win32.BITMAPINFOHEADER bi = new Win32.BITMAPINFOHEADER();
            bi.biSize = Marshal.SizeOf(typeof(Win32.BITMAPINFOHEADER));
            bi.biWidth = _w;
            bi.biHeight = -_h;                 // von oben nach unten
            bi.biPlanes = 1;
            bi.biBitCount = 32;
            bi.biCompression = 0;              // BI_RGB
            IntPtr bits;
            _dib = Win32.CreateDIBSection(IntPtr.Zero, ref bi, 0, out bits, IntPtr.Zero, 0);
            if (_dib == IntPtr.Zero || bits == IntPtr.Zero) throw new InvalidOperationException("CreateDIBSection fehlgeschlagen.");
            byte[] px = OverlayRenderer.ReadPixels(bmp, PixelFormat.Format32bppPArgb);   // BGRA, vormultipliziert
            Marshal.Copy(px, 0, bits, px.Length);
            _memDc = Win32.CreateCompatibleDC(IntPtr.Zero);
            _oldObject = Win32.SelectObject(_memDc, _dib);
        }

        void FreeBitmap()
        {
            if (_memDc != IntPtr.Zero)
            {
                Win32.SelectObject(_memDc, _oldObject);
                Win32.DeleteDC(_memDc);
                _memDc = IntPtr.Zero;
            }
            if (_dib != IntPtr.Zero)
            {
                Win32.DeleteObject(_dib);
                _dib = IntPtr.Zero;
            }
        }

        void HideNow()
        {
            _timer.Stop();
            _clock.Stop();
            if (_visible) Win32.ShowWindow(Handle, Win32.SW_HIDE);
            _visible = false;
            FreeBitmap();
        }

        protected override void WndProc(ref Message m)
        {
            if (m.Msg == Win32.WM_NCHITTEST)
            {
                m.Result = new IntPtr(Win32.HTTRANSPARENT);
                return;
            }
            if (m.Msg == Win32.WM_MOUSEACTIVATE)
            {
                m.Result = new IntPtr(Win32.MA_NOACTIVATE);
                return;
            }
            base.WndProc(ref m);
        }

        public void Dispose()
        {
            HideNow();
            _timer.Dispose();
            if (Handle != IntPtr.Zero) DestroyHandle();
        }

        // Bildschirm, auf dem das aktive Fenster (meist das Spiel) liegt
        sealed class Placement
        {
            public Win32.RECT Area;
            public float Scale = 1f;

            public static Placement Find()
            {
                Placement p = new Placement();
                IntPtr fg = Win32.GetForegroundWindow();
                IntPtr mon = fg != IntPtr.Zero
                    ? Win32.MonitorFromWindow(fg, Win32.MONITOR_DEFAULTTOPRIMARY)
                    : Win32.MonitorFromPoint(new Win32.POINT(0, 0), Win32.MONITOR_DEFAULTTOPRIMARY);
                Win32.MONITORINFO mi = new Win32.MONITORINFO();
                mi.cbSize = Marshal.SizeOf(typeof(Win32.MONITORINFO));
                if (mon != IntPtr.Zero && Win32.GetMonitorInfo(mon, ref mi))
                {
                    // Vollbild (Spiel deckt den ganzen Monitor ab): an die echte Ecke statt über die (verdeckte) Taskleiste
                    p.Area = IsFullscreen(fg, mi.rcMonitor) ? mi.rcMonitor : mi.rcWork;
                }
                else
                {
                    Rectangle wa = Screen.PrimaryScreen.WorkingArea;
                    p.Area.Left = wa.Left;
                    p.Area.Top = wa.Top;
                    p.Area.Right = wa.Right;
                    p.Area.Bottom = wa.Bottom;
                }
                try
                {
                    uint dx, dy;
                    if (mon != IntPtr.Zero && Win32.GetDpiForMonitor(mon, 0, out dx, out dy) == 0 && dx >= 48) p.Scale = dx / 96f;
                }
                catch (DllNotFoundException)
                {
                }
                catch (EntryPointNotFoundException)
                {
                }
                return p;
            }

            static bool IsFullscreen(IntPtr fg, Win32.RECT monitor)
            {
                if (fg == IntPtr.Zero) return false;
                StringBuilder cls = new StringBuilder(64);
                Win32.GetClassName(fg, cls, cls.Capacity);
                string c = cls.ToString();
                if (c == "Progman" || c == "WorkerW" || c == "Shell_TrayWnd") return false;   // Desktop/Taskleiste
                Win32.RECT r;
                if (!Win32.GetWindowRect(fg, out r)) return false;
                return r.Left <= monitor.Left && r.Top <= monitor.Top && r.Right >= monitor.Right && r.Bottom >= monitor.Bottom;
            }
        }
    }

    // #################################################################################
    // Aus WaveMuteSync übernommen (dort getestet): Logdatei + minimaler JSON-Parser/-Serializer
    // #################################################################################

    // =================================================================================
    //  Logdatei (TriggerSwitch.log neben der .exe, rotiert bei 1 MB)
    // =================================================================================
    public static class Log
    {
        static readonly object Sync = new object();
        static string _path;
        public static volatile bool VerboseEnabled;
        const long MaxBytes = 1024 * 1024;

        public static void Init(string path) { _path = path; }
        public static void Info(string message) { Write("INFO  ", message); }
        public static void Warn(string message) { Write("WARN  ", message); }
        public static void Error(string message) { Write("FEHLER", message); }
        public static void Debug(string message) { if (VerboseEnabled) Write("DEBUG ", message); }

        static void Write(string level, string message)
        {
            string line = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff", CultureInfo.InvariantCulture) + "  " + level + "  " + message;
            lock (Sync)
            {
                if (_path == null) return;
                try
                {
                    FileInfo fi = new FileInfo(_path);
                    if (fi.Exists && fi.Length > MaxBytes)
                    {
                        string old = _path + ".old";
                        if (File.Exists(old)) File.Delete(old);
                        File.Move(_path, old);
                    }
                    File.AppendAllText(_path, line + Environment.NewLine, Encoding.UTF8);
                }
                catch (Exception)
                {
                    // Logging darf nie das Programm stören.
                }
            }
        }

        public static string Shorten(string text, int max)
        {
            if (text == null) return "";
            return text.Length <= max ? text : text.Substring(0, max) + " …";
        }
    }

    // =================================================================================
    //  Minimaler JSON-Parser/-Serializer (keine externen Abhängigkeiten)
    //  Objekte -> Dictionary<string, object>, Arrays -> List<object>,
    //  Zahlen -> long bzw. double, dazu string, bool und null.
    // =================================================================================
    public static class Json
    {
        public static object Parse(string text)
        {
            if (text == null) throw new ArgumentNullException("text");
            int i = 0;
            if (text.Length > 0 && text[0] == '\uFEFF') i = 1;
            object value = ParseValue(text, ref i, 0);
            SkipWhitespace(text, ref i);
            if (i != text.Length) throw Fail("unerwartete Zeichen nach dem Ende", i);
            return value;
        }

        public static string Stringify(object value)
        {
            StringBuilder sb = new StringBuilder();
            WriteValue(sb, value, 0);
            return sb.ToString();
        }

        public static Dictionary<string, object> AsObject(object o)
        {
            return o as Dictionary<string, object>;
        }

        public static object Get(Dictionary<string, object> o, string key)
        {
            object v;
            if (o != null && o.TryGetValue(key, out v)) return v;
            return null;
        }

        public static string GetString(Dictionary<string, object> o, string key)
        {
            return Get(o, key) as string;
        }

        public static bool? GetBool(Dictionary<string, object> o, string key)
        {
            object v = Get(o, key);
            if (v is bool) return (bool)v;
            return null;
        }

        public static long? GetLong(Dictionary<string, object> o, string key)
        {
            object v = Get(o, key);
            if (v is long) return (long)v;
            if (v is double)
            {
                double d = (double)v;
                if (d >= long.MinValue && d <= long.MaxValue) return (long)d;
            }
            return null;
        }

        public static Dictionary<string, object> GetObject(Dictionary<string, object> o, string key)
        {
            return Get(o, key) as Dictionary<string, object>;
        }

        public static List<object> GetArray(Dictionary<string, object> o, string key)
        {
            return Get(o, key) as List<object>;
        }

        // ------------------------------------------------------------------ Parser

        static FormatException Fail(string what, int position)
        {
            return new FormatException("Ungültiges JSON: " + what + " (Position " + position.ToString(CultureInfo.InvariantCulture) + ")");
        }

        static void SkipWhitespace(string s, ref int i)
        {
            while (i < s.Length)
            {
                char c = s[i];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n') i++;
                else break;
            }
        }

        static object ParseValue(string s, ref int i, int depth)
        {
            if (depth > 200) throw Fail("zu tief verschachtelt", i);
            SkipWhitespace(s, ref i);
            if (i >= s.Length) throw Fail("Text endet unerwartet", i);
            char c = s[i];
            switch (c)
            {
                case '{':
                    return ParseObject(s, ref i, depth);
                case '[':
                    return ParseArray(s, ref i, depth);
                case '"':
                    return ParseString(s, ref i);
                case 't':
                    Expect(s, ref i, "true");
                    return true;
                case 'f':
                    Expect(s, ref i, "false");
                    return false;
                case 'n':
                    Expect(s, ref i, "null");
                    return null;
                default:
                    if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(s, ref i);
                    throw Fail("unerwartetes Zeichen '" + c + "'", i);
            }
        }

        static void Expect(string s, ref int i, string word)
        {
            if (i + word.Length > s.Length || string.CompareOrdinal(s, i, word, 0, word.Length) != 0)
            {
                throw Fail("'" + word + "' erwartet", i);
            }
            i += word.Length;
        }

        static Dictionary<string, object> ParseObject(string s, ref int i, int depth)
        {
            Dictionary<string, object> result = new Dictionary<string, object>(StringComparer.Ordinal);
            i++;
            SkipWhitespace(s, ref i);
            if (i < s.Length && s[i] == '}')
            {
                i++;
                return result;
            }
            while (true)
            {
                SkipWhitespace(s, ref i);
                if (i >= s.Length || s[i] != '"') throw Fail("Schlüssel erwartet", i);
                string key = ParseString(s, ref i);
                SkipWhitespace(s, ref i);
                if (i >= s.Length || s[i] != ':') throw Fail("':' erwartet", i);
                i++;
                result[key] = ParseValue(s, ref i, depth + 1);
                SkipWhitespace(s, ref i);
                if (i >= s.Length) throw Fail("Text endet unerwartet", i);
                if (s[i] == ',')
                {
                    i++;
                    continue;
                }
                if (s[i] == '}')
                {
                    i++;
                    return result;
                }
                throw Fail("',' oder '}' erwartet", i);
            }
        }

        static List<object> ParseArray(string s, ref int i, int depth)
        {
            List<object> result = new List<object>();
            i++;
            SkipWhitespace(s, ref i);
            if (i < s.Length && s[i] == ']')
            {
                i++;
                return result;
            }
            while (true)
            {
                result.Add(ParseValue(s, ref i, depth + 1));
                SkipWhitespace(s, ref i);
                if (i >= s.Length) throw Fail("Text endet unerwartet", i);
                if (s[i] == ',')
                {
                    i++;
                    continue;
                }
                if (s[i] == ']')
                {
                    i++;
                    return result;
                }
                throw Fail("',' oder ']' erwartet", i);
            }
        }

        static string ParseString(string s, ref int i)
        {
            i++;   // öffnendes "
            StringBuilder sb = null;
            int start = i;
            while (true)
            {
                if (i >= s.Length) throw Fail("String nicht abgeschlossen", i);
                char c = s[i];
                if (c == '"')
                {
                    string result;
                    if (sb == null) result = s.Substring(start, i - start);
                    else result = sb.Append(s, start, i - start).ToString();
                    i++;
                    return result;
                }
                if (c != '\\')
                {
                    i++;
                    continue;
                }

                if (sb == null) sb = new StringBuilder();
                sb.Append(s, start, i - start);
                i++;
                if (i >= s.Length) throw Fail("String nicht abgeschlossen", i);
                char e = s[i];
                switch (e)
                {
                    case '"': sb.Append('"'); break;
                    case '\\': sb.Append('\\'); break;
                    case '/': sb.Append('/'); break;
                    case 'b': sb.Append('\b'); break;
                    case 'f': sb.Append('\f'); break;
                    case 'n': sb.Append('\n'); break;
                    case 'r': sb.Append('\r'); break;
                    case 't': sb.Append('\t'); break;
                    case 'u':
                        int code;
                        if (i + 4 >= s.Length
                            || !int.TryParse(s.Substring(i + 1, 4), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out code))
                        {
                            throw Fail("ungültige \\u-Sequenz", i);
                        }
                        sb.Append((char)code);
                        i += 4;
                        break;
                    default:
                        throw Fail("ungültige Escape-Sequenz", i);
                }
                i++;
                start = i;
            }
        }

        static object ParseNumber(string s, ref int i)
        {
            int start = i;
            if (s[i] == '-') i++;
            while (i < s.Length)
            {
                char c = s[i];
                if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') i++;
                else break;
            }
            string number = s.Substring(start, i - start);
            if (number.IndexOf('.') < 0 && number.IndexOf('e') < 0 && number.IndexOf('E') < 0)
            {
                long l;
                if (long.TryParse(number, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out l)) return l;
            }
            double d;
            if (double.TryParse(number, NumberStyles.Float, CultureInfo.InvariantCulture, out d)) return d;
            throw Fail("ungültige Zahl '" + number + "'", start);
        }

        // ------------------------------------------------------------------ Serializer

        static void WriteValue(StringBuilder sb, object v, int depth)
        {
            if (depth > 100) throw new InvalidOperationException("JSON ist zu tief verschachtelt.");
            if (v == null)
            {
                sb.Append("null");
                return;
            }
            string str = v as string;
            if (str != null)
            {
                WriteString(sb, str);
                return;
            }
            if (v is bool)
            {
                sb.Append((bool)v ? "true" : "false");
                return;
            }
            if (v is int || v is long || v is short || v is byte || v is sbyte || v is ushort || v is uint || v is ulong)
            {
                sb.Append(Convert.ToString(v, CultureInfo.InvariantCulture));
                return;
            }
            if (v is double || v is float || v is decimal)
            {
                double d = Convert.ToDouble(v, CultureInfo.InvariantCulture);
                if (double.IsNaN(d) || double.IsInfinity(d)) sb.Append("null");
                else sb.Append(d.ToString("R", CultureInfo.InvariantCulture));
                return;
            }
            IDictionary<string, object> dict = v as IDictionary<string, object>;
            if (dict != null)
            {
                sb.Append('{');
                bool first = true;
                foreach (KeyValuePair<string, object> kv in dict)
                {
                    if (!first) sb.Append(',');
                    first = false;
                    WriteString(sb, kv.Key);
                    sb.Append(':');
                    WriteValue(sb, kv.Value, depth + 1);
                }
                sb.Append('}');
                return;
            }
            System.Collections.IEnumerable list = v as System.Collections.IEnumerable;
            if (list != null)
            {
                sb.Append('[');
                bool first = true;
                foreach (object item in list)
                {
                    if (!first) sb.Append(',');
                    first = false;
                    WriteValue(sb, item, depth + 1);
                }
                sb.Append(']');
                return;
            }
            WriteString(sb, Convert.ToString(v, CultureInfo.InvariantCulture));
        }

        static void WriteString(StringBuilder sb, string s)
        {
            sb.Append('"');
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    case '\b': sb.Append("\\b"); break;
                    case '\f': sb.Append("\\f"); break;
                    default:
                        if (c < ' ') sb.Append("\\u").Append(((int)c).ToString("x4", CultureInfo.InvariantCulture));
                        else sb.Append(c);
                        break;
                }
            }
            sb.Append('"');
        }
    }
}
