// =====================================================================================
//  WaveMuteSync – spiegelt den Hardware-Mute des Elgato Wave:3 nach Discord und Teams
// -------------------------------------------------------------------------------------
//  Quelle:  Elgato Wave Link 3 – lokale WebSocket/JSON-RPC-Schnittstelle (dieselbe, die
//           das offizielle Stream-Deck-Plugin nutzt). Wave Link meldet "isMuted" des
//           Mikrofon-Eingangs, also genau das Antippen oben am Wave:3.
//  Discord: lokales RPC über die Named Pipe \\.\pipe\discord-ipc-N
//           (SET_VOICE_SETTINGS mute=true/false). Braucht eine eigene Discord-App
//           (Client-ID + Secret) und eine einmalige Bestätigung in Discord.
//  Teams:   Windows UI Automation – liest und klickt den Mikrofon-Button
//           (AutomationId "microphone-button") im laufenden Anruf.
//
//  Regeln:
//   - Antippen (echte Änderung)        -> Discord/Teams bekommen denselben Zustand.
//   - Programmstart, Neuverbindung oder
//     Beitritt zu einem Teams-Anruf    -> nur "stumm" wird übernommen; es wird nie
//                                         automatisch entstummt.
//   - Discord ist taub geschaltet      -> beim Entstummen wird Discord nicht angefasst.
//
//  Setup.ps1 kompiliert diese Datei mit dem C#-Compiler von .NET Framework 4.x (C# 5).
//  Deshalb bewusst ohne neuere Sprachfeatures ($"...", ?., =>-Member, out var, ...).
// =====================================================================================

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Pipes;
using System.Net;
using System.Net.WebSockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Runtime.InteropServices;
using System.Windows.Automation;
using Microsoft.Win32;

// Aktuelles .NET-Framework-Verhalten verwenden (u. a. TLS-Standards des Betriebssystems).
[assembly: System.Runtime.Versioning.TargetFramework(".NETFramework,Version=v4.8", FrameworkDisplayName = ".NET Framework 4.8")]

namespace WaveMuteSync
{
    // =================================================================================
    //  Programmstart
    // =================================================================================
    public static class Program
    {
        public const string Version = "1.1.0";
        const string MutexName = @"Local\WaveMuteSync.SingleInstance";
        const string StopEventName = @"Local\WaveMuteSync.Stop";

        [MTAThread]
        public static int Main(string[] args)
        {
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;

            // "WaveMuteSync.exe --stop" beendet eine laufende Instanz sauber (nutzt Setup.ps1).
            if (HasArg(args, "--stop"))
            {
                return SignalStop() ? 0 : 1;
            }

            Log.Init(Path.Combine(baseDir, "WaveMuteSync.log"));

            bool createdNew;
            Mutex mutex = new Mutex(true, MutexName, out createdNew);
            if (!createdNew)
            {
                Log.Info("Es läuft bereits eine Instanz – diese hier beendet sich wieder.");
                return 0;
            }

            try
            {
                return Run(baseDir);
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

        static int Run(string baseDir)
        {
            AppDomain.CurrentDomain.UnhandledException += delegate(object sender, UnhandledExceptionEventArgs e)
            {
                Log.Error("Unbehandelte Ausnahme: " + e.ExceptionObject);
            };
            TaskScheduler.UnobservedTaskException += delegate(object sender, UnobservedTaskExceptionEventArgs e)
            {
                Log.Debug("Unbeobachtete Task-Ausnahme: " + e.Exception.GetBaseException().Message);
                e.SetObserved();
            };

            Config cfg = Config.Load(Path.Combine(baseDir, "config.json"));
            Log.VerboseEnabled = cfg.VerboseLog;
            Log.Info("WaveMuteSync " + Version + " gestartet.");

            using (EventWaitHandle stop = new EventWaitHandle(false, EventResetMode.ManualReset, StopEventName))
            using (CancellationTokenSource cts = new CancellationTokenSource())
            {
                MicStateHub hub = new MicStateHub();
                List<Task> tasks = new List<Task>();

                if (!cfg.DiscordEnabled)
                {
                    Log.Info("Discord-Sync ist in der config.json ausgeschaltet.");
                }
                else if (cfg.DiscordClientId.Length == 0 || cfg.DiscordClientSecret.Length == 0)
                {
                    Log.Warn("Discord-Sync aus: Client-ID oder Client-Secret fehlen in der config.json (Setup → „Discord einrichten“).");
                }
                else
                {
                    DiscordSink discord = new DiscordSink(cfg, hub, baseDir);
                    hub.AddListener(discord);
                    CancellationToken token = cts.Token;
                    tasks.Add(Task.Run(() => discord.RunAsync(token)));
                }

                TeamsSink teams = null;
                if (cfg.TeamsEnabled)
                {
                    try
                    {
                        teams = new TeamsSink(cfg, hub, new UiaTeamsUi());
                        hub.AddListener(teams);
                        teams.Start(cts.Token);
                    }
                    catch (Exception ex)
                    {
                        teams = null;
                        Log.Error("Teams-Sync konnte nicht gestartet werden: " + ex.Message);
                    }
                }
                else
                {
                    Log.Info("Teams-Sync ist in der config.json ausgeschaltet.");
                }

                WaveLinkSource wave = new WaveLinkSource(cfg, hub);
                CancellationToken waveToken = cts.Token;
                tasks.Add(Task.Run(() => wave.RunAsync(waveToken)));

#if WMS_TEST
                // Nur im Testaufbau (Mono kennt keine prozessübergreifenden benannten Events).
                string stopFile = Path.Combine(baseDir, "stop.request");
                ThreadPool.QueueUserWorkItem(delegate
                {
                    while (!File.Exists(stopFile)) Thread.Sleep(100);
                    stop.Set();
                });
#endif

                stop.WaitOne();
                Log.Info("Stop-Signal erhalten – beende WaveMuteSync.");
                cts.Cancel();
                try
                {
                    Task.WaitAll(tasks.ToArray(), 3000);
                }
                catch (Exception)
                {
                }
                if (teams != null)
                {
                    teams.Join(2000);
                }
            }
            Log.Info("Beendet.");
            return 0;
        }

        static bool HasArg(string[] args, string name)
        {
            foreach (string a in args)
            {
                if (string.Equals(a, name, StringComparison.OrdinalIgnoreCase))
                {
                    return true;
                }
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
    //  Einstellungen (config.json neben der .exe)
    // =================================================================================
    public sealed class Config
    {
        public const string DefaultTeamsMutedPattern = @"(?i)unmute|aufheben|einschalten|turn on";
        public const string DefaultTeamsLivePattern = @"(?i)^\s*mute|stummschalten|stumm schalten|ausschalten|turn off";

        public bool DiscordEnabled = true;
        public string DiscordClientId = "";
        public string DiscordClientSecret = "";
        public string DiscordRedirectUri = "http://localhost";
        public string DiscordApiBase = "https://discord.com/api";
        public int DiscordPipeIndex = -1;           // -1 = automatisch (discord-ipc-0 bis -9)

        public bool TeamsEnabled = true;
        public string TeamsMutedNamePattern = DefaultTeamsMutedPattern;
        public string TeamsLiveNamePattern = DefaultTeamsLivePattern;
        public int TeamsPollMs = 1000;              // Takt, solange Teams das Mikro benutzt
        public int TeamsIdlePollMs = 2000;          // Takt ohne Anruf (nur billige Registry-Abfrage)
        public int TeamsInCallCheckMs = 15000;      // Kontrollblick in den UI-Baum während des Anrufs

        public string WaveDeviceNameFilter = "";    // leer = erstes Wave-Gerät
        public int WaveLinkPort = 0;                // 0 = automatisch (ws-info.json bzw. 1884-1893)

        public bool VerboseLog = false;

        public static Config Load(string path)
        {
            Config c = new Config();
            if (!File.Exists(path))
            {
                Log.Warn("config.json nicht gefunden (" + path + ") – verwende Standardwerte.");
                c.Sanitize();
                return c;
            }
            try
            {
                Dictionary<string, object> root = Json.AsObject(Json.Parse(File.ReadAllText(path, Encoding.UTF8)));
                if (root == null)
                {
                    throw new FormatException("Die oberste Ebene ist kein JSON-Objekt.");
                }

                Dictionary<string, object> d = Json.GetObject(root, "discord");
                ReadBool(d, "enabled", ref c.DiscordEnabled);
                ReadString(d, "clientId", ref c.DiscordClientId);
                ReadString(d, "clientSecret", ref c.DiscordClientSecret);
                ReadString(d, "redirectUri", ref c.DiscordRedirectUri);
                ReadString(d, "apiBase", ref c.DiscordApiBase);
                ReadInt(d, "pipeIndex", ref c.DiscordPipeIndex);

                Dictionary<string, object> t = Json.GetObject(root, "teams");
                ReadBool(t, "enabled", ref c.TeamsEnabled);
                ReadString(t, "mutedNamePattern", ref c.TeamsMutedNamePattern);
                ReadString(t, "liveNamePattern", ref c.TeamsLiveNamePattern);
                ReadInt(t, "pollMs", ref c.TeamsPollMs);
                ReadInt(t, "idlePollMs", ref c.TeamsIdlePollMs);
                ReadInt(t, "inCallCheckMs", ref c.TeamsInCallCheckMs);

                Dictionary<string, object> w = Json.GetObject(root, "waveLink");
                ReadString(w, "deviceNameFilter", ref c.WaveDeviceNameFilter);
                ReadInt(w, "port", ref c.WaveLinkPort);

                ReadBool(root, "verboseLog", ref c.VerboseLog);
            }
            catch (Exception ex)
            {
                Log.Error("config.json ist fehlerhaft (" + ex.Message + ") – verwende Standardwerte.");
                c = new Config();
            }
            c.Sanitize();
            return c;
        }

        void Sanitize()
        {
            DiscordClientId = (DiscordClientId ?? "").Trim();
            DiscordClientSecret = (DiscordClientSecret ?? "").Trim();
            DiscordRedirectUri = string.IsNullOrWhiteSpace(DiscordRedirectUri) ? "http://localhost" : DiscordRedirectUri.Trim();
            DiscordApiBase = string.IsNullOrWhiteSpace(DiscordApiBase) ? "https://discord.com/api" : DiscordApiBase.Trim().TrimEnd('/');
            if (DiscordPipeIndex > 9) DiscordPipeIndex = 9;
            if (string.IsNullOrWhiteSpace(TeamsMutedNamePattern)) TeamsMutedNamePattern = DefaultTeamsMutedPattern;
            if (string.IsNullOrWhiteSpace(TeamsLiveNamePattern)) TeamsLiveNamePattern = DefaultTeamsLivePattern;
            TeamsPollMs = Math.Max(250, TeamsPollMs);
            TeamsIdlePollMs = Math.Max(500, TeamsIdlePollMs);
            TeamsInCallCheckMs = Math.Max(2000, TeamsInCallCheckMs);
            WaveDeviceNameFilter = (WaveDeviceNameFilter ?? "").Trim();
            if (WaveLinkPort < 0 || WaveLinkPort > 65535) WaveLinkPort = 0;
        }

        static void ReadBool(Dictionary<string, object> o, string key, ref bool target)
        {
            bool? v = Json.GetBool(o, key);
            if (v.HasValue) target = v.Value;
        }

        static void ReadString(Dictionary<string, object> o, string key, ref string target)
        {
            object v = Json.Get(o, key);
            if (v is string) target = (string)v;
            else if (v is long || v is double) target = Convert.ToString(v, CultureInfo.InvariantCulture);
        }

        static void ReadInt(Dictionary<string, object> o, string key, ref int target)
        {
            long? v = Json.GetLong(o, key);
            if (v.HasValue && v.Value >= int.MinValue && v.Value <= int.MaxValue) target = (int)v.Value;
        }
    }

    // =================================================================================
    //  Logdatei (WaveMuteSync.log neben der .exe, rotiert bei 1 MB)
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
    //  Mute-Zustand des Mikros – verteilt Änderungen an Discord und Teams
    // =================================================================================
    public interface IMicStateListener
    {
        // muted:    aktueller Hardware-Zustand
        // isChange: true  = echtes Antippen (Änderung am überwachten Mikrofon während laufender Verbindung)
        //           false = nur Zustandsabgleich (Programmstart, Neuverbindung, Gerät neu angesteckt).
        //                   Ein Abgleich darf stummschalten, aber nie entstummen.
        void OnMicState(bool muted, bool isChange);
    }

    public sealed class MicStateHub
    {
        readonly object _sync = new object();
        readonly List<IMicStateListener> _listeners = new List<IMicStateListener>();
        bool? _muted;

        public bool? Muted
        {
            get { lock (_sync) { return _muted; } }
        }

        public void AddListener(IMicStateListener listener)
        {
            lock (_sync) { _listeners.Add(listener); }
        }

        public void Report(bool muted)
        {
            IMicStateListener[] listeners;
            bool isChange;
            lock (_sync)
            {
                if (_muted.HasValue && _muted.Value == muted) return;   // keine Änderung
                isChange = _muted.HasValue;
                _muted = muted;
                listeners = _listeners.ToArray();
            }
            Log.Info(muted ? "Wave:3 ist STUMM." : "Wave:3 ist AKTIV (nicht stumm).");
            foreach (IMicStateListener l in listeners)
            {
                try
                {
                    l.OnMicState(muted, isChange);
                }
                catch (Exception ex)
                {
                    Log.Error("Fehler beim Weitergeben des Mute-Status: " + ex.Message);
                }
            }
        }
    }

    // =================================================================================
    //  Quelle: Elgato Wave Link 3 (ws://127.0.0.1:<port>, JSON-RPC 2.0)
    // =================================================================================
    public sealed class WaveLinkSource
    {
        readonly Config _cfg;
        readonly MicStateHub _hub;
        readonly byte[] _buffer = new byte[64 * 1024];
        int _nextId;
        string _deviceId;
        string _inputId;
        string _lastDeviceSummary;
        bool _warnedNoDevice;
        bool _needRefresh;
        DateTime _lastRefreshUtc = DateTime.MinValue;

        public WaveLinkSource(Config cfg, MicStateHub hub)
        {
            _cfg = cfg;
            _hub = hub;
        }

        public async Task RunAsync(CancellationToken ct)
        {
            int failures = 0;
            string lastProblem = null;
            while (!ct.IsCancellationRequested)
            {
                string problem;
                ClientWebSocket ws = null;
                try
                {
                    ws = await ConnectAsync(ct);
                    if (ws == null)
                    {
                        problem = "Wave Link 3 ist nicht erreichbar – läuft Wave Link? Neuer Versuch folgt automatisch.";
                    }
                    else
                    {
                        failures = 0;
                        lastProblem = null;
                        await SessionAsync(ws, ct);
                        problem = "Verbindung zu Wave Link wurde getrennt.";
                    }
                }
                catch (OperationCanceledException)
                {
                    if (ct.IsCancellationRequested) return;
                    problem = "Wave Link antwortet nicht (Zeitüberschreitung).";
                }
                catch (Exception ex)
                {
                    problem = "Wave Link: " + ex.Message;
                }
                finally
                {
                    if (ws != null)
                    {
                        try { ws.Abort(); } catch (Exception) { }
                        ws.Dispose();
                    }
                }

                if (ct.IsCancellationRequested) return;
                if (problem != lastProblem)
                {
                    Log.Warn(problem);
                    lastProblem = problem;
                }
                failures++;
                try
                {
                    await Task.Delay(Math.Min(10000, 1000 * failures), ct);
                }
                catch (OperationCanceledException)
                {
                    return;
                }
            }
        }

        async Task<ClientWebSocket> ConnectAsync(CancellationToken ct)
        {
            foreach (int port in CandidatePorts())
            {
                ct.ThrowIfCancellationRequested();
                ClientWebSocket ws = new ClientWebSocket();
                bool keep = false;
                try
                {
                    // Wave Link akzeptiert Verbindungen mit dem Origin des Stream-Deck-Plugins.
                    ws.Options.SetRequestHeader("Origin", "streamdeck://");
                    ws.Options.Proxy = null;
                    ws.Options.KeepAliveInterval = TimeSpan.FromSeconds(20);
                    using (CancellationTokenSource timeout = CancellationTokenSource.CreateLinkedTokenSource(ct))
                    {
                        timeout.CancelAfter(3000);
                        await ws.ConnectAsync(new Uri("ws://127.0.0.1:" + port.ToString(CultureInfo.InvariantCulture) + "/"), timeout.Token);
                        Dictionary<string, object> info = Json.AsObject(await CallAsync(ws, "getApplicationInfo", null, timeout.Token));
                        if (Json.GetString(info, "appID") != "EWL")
                        {
                            Log.Debug("Port " + port + " ist nicht Wave Link.");
                            continue;
                        }
                        Log.Info(string.Format(CultureInfo.InvariantCulture,
                            "Mit {0} {1} verbunden (Port {2}, Schnittstelle {3}).",
                            Json.GetString(info, "name") ?? "Wave Link",
                            Json.GetString(info, "version") ?? "",
                            port,
                            Convert.ToString(Json.Get(info, "interfaceRevision"), CultureInfo.InvariantCulture)));
                        keep = true;
                        return ws;
                    }
                }
                catch (Exception ex)
                {
                    if (ct.IsCancellationRequested) throw new OperationCanceledException(ct);
                    Log.Debug("Wave Link auf Port " + port + " nicht erreichbar: " + ex.Message);
                }
                finally
                {
                    if (!keep) ws.Dispose();
                }
            }
            return null;
        }

        List<int> CandidatePorts()
        {
            List<int> ports = new List<int>();
            if (_cfg.WaveLinkPort > 0)
            {
                ports.Add(_cfg.WaveLinkPort);
                return ports;
            }

            // Wave Link 3 schreibt seinen Port in ws-info.json (App-Paket unter %LOCALAPPDATA%\Packages).
            List<string> files = new List<string>();
            try
            {
                string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
                string packages = Path.Combine(local, "Packages");
                files.Add(Path.Combine(Path.Combine(Path.Combine(packages, "Elgato.WaveLink_g54w8ztgkx496"), "LocalState"), "ws-info.json"));
                if (Directory.Exists(packages))
                {
                    foreach (string dir in Directory.GetDirectories(packages, "Elgato.WaveLink*"))
                    {
                        string f = Path.Combine(Path.Combine(dir, "LocalState"), "ws-info.json");
                        if (!files.Contains(f)) files.Add(f);
                    }
                }
            }
            catch (Exception)
            {
            }

            foreach (string f in files)
            {
                try
                {
                    if (!File.Exists(f)) continue;
                    long? p = Json.GetLong(Json.AsObject(Json.Parse(File.ReadAllText(f, Encoding.UTF8))), "port");
                    if (p.HasValue && p.Value > 0 && p.Value < 65536 && !ports.Contains((int)p.Value))
                    {
                        ports.Add((int)p.Value);
                    }
                }
                catch (Exception ex)
                {
                    Log.Debug("ws-info.json nicht lesbar: " + ex.Message);
                }
            }

            // Wie das offizielle Stream-Deck-Plugin: Port aus ws-info.json, sonst den Fallback-Bereich
            // von Wave Link 3 absuchen (unter Windows kostet jeder geschlossene Port ~2 s).
            if (ports.Count > 0) return ports;
            for (int p = 1884; p <= 1893; p++)
            {
                ports.Add(p);
            }
            return ports;
        }

        async Task SessionAsync(ClientWebSocket ws, CancellationToken ct)
        {
            _deviceId = null;
            _inputId = null;
            await RefreshDevicesAsync(ws, ct);

            while (!ct.IsCancellationRequested)
            {
                string text = await ReceiveTextAsync(ws, ct);
                if (text == null) return;   // Wave Link hat die Verbindung geschlossen
                Dictionary<string, object> msg = ParseMessage(text);
                if (msg != null && Json.Get(msg, "id") == null)
                {
                    HandleNotification(msg);
                }

                if (_needRefresh && (DateTime.UtcNow - _lastRefreshUtc).TotalSeconds >= 5)
                {
                    await RefreshDevicesAsync(ws, ct);
                }
            }
        }

        async Task RefreshDevicesAsync(ClientWebSocket ws, CancellationToken ct)
        {
            _needRefresh = false;
            _lastRefreshUtc = DateTime.UtcNow;
            object result;
            using (CancellationTokenSource timeout = CancellationTokenSource.CreateLinkedTokenSource(ct))
            {
                timeout.CancelAfter(5000);
                result = await CallAsync(ws, "getInputDevices", null, timeout.Token);
            }
            ApplyDeviceList(Json.GetArray(Json.AsObject(result), "inputDevices"));
        }

        // Sendet eine Anfrage und wartet auf die passende Antwort. Dazwischen eintreffende
        // Ereignisse werden normal verarbeitet.
        async Task<object> CallAsync(ClientWebSocket ws, string method, object parameters, CancellationToken ct)
        {
            int id = Interlocked.Increment(ref _nextId);
            Dictionary<string, object> request = new Dictionary<string, object>();
            request["id"] = id;
            request["jsonrpc"] = "2.0";
            request["method"] = method;
            request["params"] = parameters;
            byte[] bytes = Encoding.UTF8.GetBytes(Json.Stringify(request));
            await ws.SendAsync(new ArraySegment<byte>(bytes), WebSocketMessageType.Text, true, ct);

            while (true)
            {
                string text = await ReceiveTextAsync(ws, ct);
                if (text == null) throw new IOException("Wave Link hat die Verbindung geschlossen.");
                Dictionary<string, object> msg = ParseMessage(text);
                if (msg == null) continue;
                if (Json.Get(msg, "id") != null)
                {
                    long? responseId = Json.GetLong(msg, "id");
                    if (!responseId.HasValue || responseId.Value != id) continue;
                    Dictionary<string, object> error = Json.GetObject(msg, "error");
                    if (error != null)
                    {
                        throw new InvalidOperationException("Wave Link meldet einen Fehler bei " + method + ": " + (Json.GetString(error, "message") ?? "unbekannt"));
                    }
                    return Json.Get(msg, "result");
                }
                HandleNotification(msg);
            }
        }

        async Task<string> ReceiveTextAsync(ClientWebSocket ws, CancellationToken ct)
        {
            using (MemoryStream ms = new MemoryStream())
            {
                while (true)
                {
                    WebSocketReceiveResult r = await ws.ReceiveAsync(new ArraySegment<byte>(_buffer), ct);
                    if (r.MessageType == WebSocketMessageType.Close) return null;
                    ms.Write(_buffer, 0, r.Count);
                    if (ms.Length > 64L * 1024 * 1024) throw new InvalidDataException("Nachricht von Wave Link ist zu groß.");
                    if (r.EndOfMessage) break;
                }
                return Encoding.UTF8.GetString(ms.ToArray());
            }
        }

        static Dictionary<string, object> ParseMessage(string text)
        {
            try
            {
                return Json.AsObject(Json.Parse(text));
            }
            catch (Exception ex)
            {
                Log.Debug("Unlesbare Nachricht von Wave Link: " + ex.Message);
                return null;
            }
        }

        void HandleNotification(Dictionary<string, object> msg)
        {
            string method = Json.GetString(msg, "method");
            if (method == null) return;
            Dictionary<string, object> p = Json.GetObject(msg, "params");
            if (method == "inputDevicesChanged")
            {
                Log.Debug("Wave Link: inputDevicesChanged");
                ApplyDeviceList(Json.GetArray(p, "inputDevices"));
            }
            else if (method == "inputDeviceChanged")
            {
                if (Log.VerboseEnabled) Log.Debug("Wave Link: inputDeviceChanged " + Log.Shorten(Json.Stringify(p), 400));
                ApplyDeviceUpdate(p);
            }
            else
            {
                Log.Debug("Wave Link Ereignis: " + method);
            }
        }

        void ApplyDeviceList(List<object> devices)
        {
            if (devices == null)
            {
                Log.Debug("Wave Link hat keine Geräteliste geliefert.");
                return;
            }

            StringBuilder summary = new StringBuilder();
            foreach (object o in devices)
            {
                Dictionary<string, object> dev = Json.AsObject(o);
                if (dev == null) continue;
                if (summary.Length > 0) summary.Append(", ");
                summary.Append(Json.GetString(dev, "name") ?? Json.GetString(dev, "id") ?? "?");
                if (Json.GetBool(dev, "isWaveDevice") == true) summary.Append(" [Wave]");
            }
            string s = summary.ToString();
            if (s != _lastDeviceSummary)
            {
                Log.Info("Eingabegeräte in Wave Link: " + (s.Length > 0 ? s : "(keine)"));
                _lastDeviceSummary = s;
            }

            // Pass 0: Namensfilter bzw. offizielle Wave-Geräte, Pass 1: Name enthält "Wave"
            string filter = _cfg.WaveDeviceNameFilter;
            string chosenDevice = null, chosenInput = null, label = null;
            bool muted = false;
            for (int pass = 0; pass < 2 && chosenDevice == null; pass++)
            {
                if (pass == 1 && filter.Length > 0) break;
                foreach (object o in devices)
                {
                    Dictionary<string, object> dev = Json.AsObject(o);
                    if (dev == null) continue;
                    string devId = Json.GetString(dev, "id");
                    string name = Json.GetString(dev, "name") ?? "";
                    if (devId == null) continue;
                    bool match;
                    if (filter.Length > 0) match = name.IndexOf(filter, StringComparison.OrdinalIgnoreCase) >= 0;
                    else if (pass == 0) match = Json.GetBool(dev, "isWaveDevice") == true;
                    else match = name.IndexOf("Wave", StringComparison.OrdinalIgnoreCase) >= 0;
                    if (!match) continue;

                    List<object> inputs = Json.GetArray(dev, "inputs");
                    if (inputs == null) continue;
                    foreach (object io in inputs)
                    {
                        Dictionary<string, object> input = Json.AsObject(io);
                        string inputId = Json.GetString(input, "id");
                        bool? isMuted = Json.GetBool(input, "isMuted");
                        if (inputId == null || !isMuted.HasValue) continue;
                        chosenDevice = devId;
                        chosenInput = inputId;
                        muted = isMuted.Value;
                        string inputName = Json.GetString(input, "name");
                        label = (name.Length > 0 ? name : devId) + (string.IsNullOrEmpty(inputName) ? "" : " / " + inputName);
                        break;
                    }
                    if (chosenDevice != null) break;
                }
            }

            if (chosenDevice == null)
            {
                if (!_warnedNoDevice)
                {
                    Log.Warn("Kein Wave-Mikrofon mit Mute-Status in Wave Link gefunden"
                        + (filter.Length > 0 ? " (Filter: \"" + filter + "\")" : "")
                        + " – ist das Wave:3 angeschlossen?");
                    _warnedNoDevice = true;
                }
                _deviceId = null;
                _inputId = null;
                return;
            }

            _warnedNoDevice = false;
            if (chosenDevice != _deviceId || chosenInput != _inputId)
            {
                Log.Info("Überwache den Mute-Status von: " + label);
            }
            _deviceId = chosenDevice;
            _inputId = chosenInput;
            _hub.Report(muted);
        }

        void ApplyDeviceUpdate(Dictionary<string, object> update)
        {
            string devId = Json.GetString(update, "id");
            if (devId == null) return;
            if (_deviceId == null)
            {
                _needRefresh = true;   // evtl. wurde das Wave:3 gerade angesteckt
                return;
            }
            if (devId != _deviceId) return;
            List<object> inputs = Json.GetArray(update, "inputs");
            if (inputs == null) return;
            foreach (object io in inputs)
            {
                Dictionary<string, object> input = Json.AsObject(io);
                if (Json.GetString(input, "id") != _inputId) continue;
                bool? isMuted = Json.GetBool(input, "isMuted");
                if (isMuted.HasValue) _hub.Report(isMuted.Value);
            }
        }
    }

    // =================================================================================
    //  Ziel: Discord (lokales RPC über Named Pipe)
    // =================================================================================
    public sealed class DiscordRpcException : Exception
    {
        public readonly int Code;
        public DiscordRpcException(int code, string message) : base(message) { Code = code; }
    }

    // Fehler, bei denen ein erneuter Versuch sinnlos ist (z. B. falsche Client-ID).
    public sealed class DiscordFatalException : Exception
    {
        public DiscordFatalException(string message) : base(message) { }
    }

    public sealed class DiscordFrame
    {
        public int Op;
        public string Payload;
    }

    // Eine einzelne Verbindung zur Discord-Pipe: Frames lesen/schreiben, Anfragen zuordnen.
    public sealed class DiscordConnection : IDisposable
    {
        public const int OpHandshake = 0, OpFrame = 1, OpClose = 2, OpPing = 3, OpPong = 4;

        readonly Stream _pipe;
        readonly SemaphoreSlim _writeLock = new SemaphoreSlim(1, 1);
        readonly ConcurrentDictionary<string, TaskCompletionSource<Dictionary<string, object>>> _pending =
            new ConcurrentDictionary<string, TaskCompletionSource<Dictionary<string, object>>>();
        volatile Exception _error;
        Task _reader;

        // Wird für DISPATCH-Ereignisse aufgerufen (evt, data) – im Lesethread.
        public Action<string, Dictionary<string, object>> OnDispatch;

        public DiscordConnection(Stream pipe)
        {
            _pipe = pipe;
        }

        public Task Completion
        {
            get { return _reader; }
        }

        public async Task<Dictionary<string, object>> HandshakeAsync(string clientId, int timeoutMs)
        {
            Dictionary<string, object> hello = new Dictionary<string, object>();
            hello["v"] = 1;
            hello["client_id"] = clientId;
            await WriteFrameAsync(OpHandshake, Json.Stringify(hello), CancellationToken.None);

            Task<DiscordFrame> read = ReadFrameAsync(_pipe);
            Task finished = await Task.WhenAny(read, Task.Delay(timeoutMs));
            if (finished != read) throw new TimeoutException("Discord antwortet nicht auf den Handshake.");
            DiscordFrame frame = await read;
            if (frame.Op == OpClose) throw CloseToException(frame.Payload);
            Dictionary<string, object> msg = Json.AsObject(Json.Parse(frame.Payload));
            if (Json.GetString(msg, "evt") != "READY") throw new IOException("Unerwartete Antwort beim Discord-Handshake.");
            _reader = Task.Run(() => ReadLoopAsync());
            return Json.GetObject(msg, "data");
        }

        public async Task<Dictionary<string, object>> CommandAsync(string cmd, Dictionary<string, object> args, string evt, int timeoutMs, CancellationToken ct)
        {
            string nonce = Guid.NewGuid().ToString();
            TaskCompletionSource<Dictionary<string, object>> tcs =
                new TaskCompletionSource<Dictionary<string, object>>(TaskCreationOptions.RunContinuationsAsynchronously);
            _pending[nonce] = tcs;
            try
            {
                Exception error = _error;
                if (error != null) throw new IOException("Verbindung zu Discord getrennt.", error);

                Dictionary<string, object> msg = new Dictionary<string, object>();
                msg["cmd"] = cmd;
                msg["args"] = args ?? new Dictionary<string, object>();
                msg["nonce"] = nonce;
                if (evt != null) msg["evt"] = evt;
                await WriteFrameAsync(OpFrame, Json.Stringify(msg), ct);

                Task finished = await Task.WhenAny(tcs.Task, Task.Delay(timeoutMs, ct));
                if (finished != tcs.Task)
                {
                    ct.ThrowIfCancellationRequested();
                    throw new TimeoutException("Discord antwortet nicht auf " + cmd + ".");
                }
                return await tcs.Task;
            }
            finally
            {
                TaskCompletionSource<Dictionary<string, object>> removed;
                _pending.TryRemove(nonce, out removed);
            }
        }

        async Task ReadLoopAsync()
        {
            Exception error = null;
            try
            {
                while (true)
                {
                    DiscordFrame frame = await ReadFrameAsync(_pipe);
                    if (frame.Op == OpPing)
                    {
                        await WriteFrameAsync(OpPong, frame.Payload, CancellationToken.None);
                        continue;
                    }
                    if (frame.Op == OpClose)
                    {
                        error = CloseToException(frame.Payload);
                        break;
                    }
                    if (frame.Op != OpFrame) continue;

                    Dictionary<string, object> msg = null;
                    try { msg = Json.AsObject(Json.Parse(frame.Payload)); }
                    catch (Exception ex) { Log.Debug("Unlesbare Nachricht von Discord: " + ex.Message); }
                    if (msg != null) HandleMessage(msg);
                }
            }
            catch (Exception ex)
            {
                error = ex;
            }

            _error = error ?? new IOException("Discord-Verbindung beendet.");
            foreach (KeyValuePair<string, TaskCompletionSource<Dictionary<string, object>>> kv in _pending)
            {
                TaskCompletionSource<Dictionary<string, object>> tcs;
                if (_pending.TryRemove(kv.Key, out tcs)) tcs.TrySetException(_error);
            }
        }

        void HandleMessage(Dictionary<string, object> msg)
        {
            string cmd = Json.GetString(msg, "cmd");
            string evt = Json.GetString(msg, "evt");
            Dictionary<string, object> data = Json.GetObject(msg, "data");
            if (cmd == "DISPATCH")
            {
                Action<string, Dictionary<string, object>> handler = OnDispatch;
                if (handler != null && evt != null)
                {
                    try { handler(evt, data); }
                    catch (Exception ex) { Log.Error("Fehler bei Discord-Ereignis " + evt + ": " + ex.Message); }
                }
                return;
            }

            string nonce = Json.GetString(msg, "nonce");
            if (nonce == null) return;
            TaskCompletionSource<Dictionary<string, object>> tcs;
            if (!_pending.TryRemove(nonce, out tcs)) return;
            if (evt == "ERROR")
            {
                long? code = Json.GetLong(data, "code");
                tcs.TrySetException(new DiscordRpcException(code.HasValue ? (int)code.Value : 0, Json.GetString(data, "message") ?? "unbekannter Fehler"));
            }
            else
            {
                tcs.TrySetResult(data ?? new Dictionary<string, object>());
            }
        }

        static Exception CloseToException(string payload)
        {
            int code = 0;
            string message = "Discord hat die Verbindung geschlossen.";
            try
            {
                Dictionary<string, object> o = Json.AsObject(Json.Parse(payload));
                long? c = Json.GetLong(o, "code");
                if (c.HasValue) code = (int)c.Value;
                string m = Json.GetString(o, "message");
                if (!string.IsNullOrEmpty(m)) message = m;
            }
            catch (Exception)
            {
            }
            return new DiscordRpcException(code, message);
        }

        async Task WriteFrameAsync(int op, string payload, CancellationToken ct)
        {
            byte[] body = Encoding.UTF8.GetBytes(payload ?? "");
            byte[] frame = new byte[8 + body.Length];
            WriteInt32(frame, 0, op);
            WriteInt32(frame, 4, body.Length);
            Buffer.BlockCopy(body, 0, frame, 8, body.Length);
            await _writeLock.WaitAsync(ct);
            try
            {
                await _pipe.WriteAsync(frame, 0, frame.Length, ct);
                await _pipe.FlushAsync(ct);
            }
            finally
            {
                _writeLock.Release();
            }
        }

        static async Task<DiscordFrame> ReadFrameAsync(Stream pipe)
        {
            byte[] header = await ReadExactAsync(pipe, 8);
            int op = ReadInt32(header, 0);
            int length = ReadInt32(header, 4);
            if (length < 0 || length > 16 * 1024 * 1024) throw new InvalidDataException("Ungültige Nachrichtenlänge von Discord: " + length);
            byte[] body = length == 0 ? new byte[0] : await ReadExactAsync(pipe, length);
            DiscordFrame frame = new DiscordFrame();
            frame.Op = op;
            frame.Payload = Encoding.UTF8.GetString(body);
            return frame;
        }

        static async Task<byte[]> ReadExactAsync(Stream pipe, int count)
        {
            byte[] buffer = new byte[count];
            int read = 0;
            while (read < count)
            {
                int n = await pipe.ReadAsync(buffer, read, count - read);
                if (n <= 0) throw new EndOfStreamException("Discord hat die Verbindung geschlossen.");
                read += n;
            }
            return buffer;
        }

        static void WriteInt32(byte[] b, int offset, int value)
        {
            b[offset] = (byte)value;
            b[offset + 1] = (byte)(value >> 8);
            b[offset + 2] = (byte)(value >> 16);
            b[offset + 3] = (byte)(value >> 24);
        }

        static int ReadInt32(byte[] b, int offset)
        {
            return b[offset] | (b[offset + 1] << 8) | (b[offset + 2] << 16) | (b[offset + 3] << 24);
        }

        public void Dispose()
        {
            try { _pipe.Dispose(); } catch (Exception) { }
        }
    }

    public sealed class DiscordSink : IMicStateListener
    {
        static readonly string[] Scopes = { "rpc", "identify", "rpc.voice.read", "rpc.voice.write" };
        static readonly byte[] TokenEntropy = Encoding.UTF8.GetBytes("WaveMuteSync.DiscordToken.v1");
        static readonly DateTime UnixEpoch = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc);

        readonly Config _cfg;
        readonly MicStateHub _hub;
        readonly string _tokenPath;
        readonly object _sync = new object();
        readonly SemaphoreSlim _signal = new SemaphoreSlim(0, int.MaxValue);

        bool _hasPending;
        bool _pendingMuted;
        bool _pendingChange;
        bool? _discordMute;
        bool? _discordDeaf;
        bool _sessionEstablished;

        public DiscordSink(Config cfg, MicStateHub hub, string dataDir)
        {
            _cfg = cfg;
            _hub = hub;
            _tokenPath = Path.Combine(dataDir, "discord-token.dat");
        }

        public void OnMicState(bool muted, bool isChange)
        {
            lock (_sync)
            {
                _hasPending = true;
                _pendingMuted = muted;
                _pendingChange = _pendingChange || isChange;
            }
            _signal.Release();
        }

        public async Task RunAsync(CancellationToken ct)
        {
            string lastProblem = null;
            int failures = 0;
            while (!ct.IsCancellationRequested)
            {
                string problem;
                bool fatal = false;
                try
                {
                    Stream pipe = ConnectPipe();
                    if (pipe == null)
                    {
                        problem = "Discord läuft nicht (oder ist noch nicht bereit) – neuer Versuch folgt automatisch.";
                    }
                    else
                    {
                        failures = 0;
                        _sessionEstablished = false;
                        using (DiscordConnection conn = new DiscordConnection(pipe))
                        {
                            await SessionAsync(conn, ct);
                        }
                        problem = "Verbindung zu Discord getrennt.";
                    }
                }
                catch (OperationCanceledException)
                {
                    if (ct.IsCancellationRequested) return;
                    problem = "Discord: Zeitüberschreitung.";
                }
                catch (DiscordFatalException ex)
                {
                    problem = ex.Message;
                    fatal = true;
                }
                catch (DiscordRpcException ex)
                {
                    if (ex.Code == 4000)
                    {
                        problem = "Discord lehnt die Client-ID ab (" + ex.Message + ") – bitte Client-ID in der config.json prüfen.";
                        fatal = true;
                    }
                    else
                    {
                        if (ex.Code == 4003) DeleteToken();   // Token widerrufen
                        problem = "Discord: " + ex.Message + " (Code " + ex.Code + ")";
                    }
                }
                catch (Exception ex)
                {
                    problem = "Discord: " + ex.Message;
                }

                lock (_sync)
                {
                    _discordMute = null;
                    _discordDeaf = null;
                }
                if (ct.IsCancellationRequested) return;
                if (_sessionEstablished)
                {
                    lastProblem = null;   // nach einer funktionierenden Verbindung jede Trennung protokollieren
                    _sessionEstablished = false;
                }
                if (problem != lastProblem)
                {
                    if (fatal) Log.Error(problem); else Log.Warn(problem);
                    lastProblem = problem;
                }
                if (fatal)
                {
                    Log.Error("Discord-Sync pausiert bis zum nächsten Start (Setup → „Discord einrichten“ bzw. „Neu starten“).");
                    return;
                }
                failures++;
                try
                {
                    await Task.Delay(Math.Min(30000, 3000 * failures), ct);
                }
                catch (OperationCanceledException)
                {
                    return;
                }
            }
        }

        Stream ConnectPipe()
        {
            int first = 0, last = 9;
            if (_cfg.DiscordPipeIndex >= 0)
            {
                first = last = _cfg.DiscordPipeIndex;
            }
            else if (!IsDiscordRunning())
            {
                return null;   // spart unnötige Verbindungsversuche
            }

            for (int i = first; i <= last; i++)
            {
                NamedPipeClientStream pipe = new NamedPipeClientStream(".", "discord-ipc-" + i.ToString(CultureInfo.InvariantCulture),
                    PipeDirection.InOut, PipeOptions.Asynchronous);
                try
                {
                    pipe.Connect(i == first ? 1000 : 150);
                    Log.Debug("Discord-Pipe discord-ipc-" + i + " verbunden.");
                    return pipe;
                }
                catch (Exception)
                {
                    pipe.Dispose();
                }
            }
            return null;
        }

        static bool IsDiscordRunning()
        {
            string[] names = { "Discord", "DiscordPTB", "DiscordCanary", "DiscordDevelopment" };
            foreach (string n in names)
            {
                Process[] found = Process.GetProcessesByName(n);
                bool any = found.Length > 0;
                foreach (Process p in found) p.Dispose();
                if (any) return true;
            }
            return false;
        }

        async Task SessionAsync(DiscordConnection conn, CancellationToken ct)
        {
            conn.OnDispatch = delegate(string evt, Dictionary<string, object> data)
            {
                if (evt == "VOICE_SETTINGS_UPDATE") UpdateVoiceState(data);
            };

            await conn.HandshakeAsync(_cfg.DiscordClientId, 10000);
            await AuthenticateAsync(conn, ct);

            await conn.CommandAsync("SUBSCRIBE", new Dictionary<string, object>(), "VOICE_SETTINGS_UPDATE", 15000, ct);
            UpdateVoiceState(await conn.CommandAsync("GET_VOICE_SETTINGS", null, null, 15000, ct));

            bool? mute, deaf;
            lock (_sync)
            {
                mute = _discordMute;
                deaf = _discordDeaf;
                _hasPending = false;       // Startabgleich übernimmt den aktuellen Zustand
                _pendingChange = false;
            }
            while (_signal.CurrentCount > 0) _signal.Wait(0);
            _sessionEstablished = true;
            Log.Info("Discord verbunden (Discord-Mikro " + (mute == true ? "stumm" : "aktiv") + (deaf == true ? ", taub" : "") + ").");

            // Startabgleich: Ist das Wave:3 stumm, wird Discord auch stumm geschaltet.
            if (_hub.Muted == true) await ApplyAsync(conn, true, false, ct);

            using (CancellationTokenSource session = CancellationTokenSource.CreateLinkedTokenSource(ct))
            {
                try
                {
                    while (true)
                    {
                        Task wait = _signal.WaitAsync(session.Token);
                        Task finished = await Task.WhenAny(wait, conn.Completion);
                        if (finished == conn.Completion) return;   // Verbindung beendet
                        await wait;

                        bool has, target, change;
                        lock (_sync)
                        {
                            has = _hasPending;
                            target = _pendingMuted;
                            change = _pendingChange;
                            _hasPending = false;
                            _pendingChange = false;
                        }
                        if (has) await ApplyAsync(conn, target, change, ct);
                    }
                }
                finally
                {
                    session.Cancel();   // offenes WaitAsync abmelden
                }
            }
        }

        async Task ApplyAsync(DiscordConnection conn, bool targetMuted, bool isChange, CancellationToken ct)
        {
            bool? current, deaf;
            lock (_sync)
            {
                current = _discordMute;
                deaf = _discordDeaf;
            }
            if (current.HasValue && current.Value == targetMuted) return;
            if (!targetMuted)
            {
                if (!isChange) return;   // nie automatisch entstummen
                if (deaf == true)
                {
                    Log.Info("Discord ist taub geschaltet – Stummschaltung in Discord bleibt, wie sie ist.");
                    return;
                }
            }

            Dictionary<string, object> args = new Dictionary<string, object>();
            args["mute"] = targetMuted;
            try
            {
                UpdateVoiceState(await conn.CommandAsync("SET_VOICE_SETTINGS", args, null, 10000, ct));
                Log.Info(targetMuted ? "Discord: stummgeschaltet." : "Discord: Stummschaltung aufgehoben.");
            }
            catch (DiscordRpcException ex)
            {
                Log.Warn("Discord hat das Umschalten abgelehnt: " + ex.Message + " (Code " + ex.Code + ")");
            }
        }

        void UpdateVoiceState(Dictionary<string, object> data)
        {
            if (data == null) return;
            bool? mute = Json.GetBool(data, "mute");
            bool? deaf = Json.GetBool(data, "deaf");
            lock (_sync)
            {
                if (mute.HasValue) _discordMute = mute;
                if (deaf.HasValue) _discordDeaf = deaf;
            }
            Log.Debug("Discord-Status: stumm=" + FormatBool(mute) + ", taub=" + FormatBool(deaf));
        }

        static string FormatBool(bool? b)
        {
            return b.HasValue ? (b.Value ? "ja" : "nein") : "?";
        }

        // ---------- Anmeldung: gespeichertes Token -> Refresh -> einmalige Autorisierung ----------

        async Task AuthenticateAsync(DiscordConnection conn, CancellationToken ct)
        {
            TokenSet token = LoadToken();
            if (token != null && !string.IsNullOrEmpty(token.AccessToken) && token.ExpiresAtUtc > DateTime.UtcNow.AddHours(12))
            {
                if (await TryAuthenticateAsync(conn, token.AccessToken, ct)) return;
            }

            if (token != null && !string.IsNullOrEmpty(token.RefreshToken))
            {
                TokenSet refreshed = null;
                try
                {
                    refreshed = await RequestTokenAsync("refresh_token", token.RefreshToken, ct);
                }
                catch (DiscordFatalException)
                {
                    throw;
                }
                catch (Exception ex)
                {
                    Log.Warn("Discord-Token konnte nicht erneuert werden: " + ex.Message);
                }
                if (refreshed != null)
                {
                    SaveToken(refreshed);
                    Log.Info("Discord-Anmeldung automatisch erneuert.");
                    if (await TryAuthenticateAsync(conn, refreshed.AccessToken, ct)) return;
                }
            }

            Log.Info("Discord: Bitte in Discord auf „Autorisieren“ klicken (nur beim ersten Mal nötig).");
            Dictionary<string, object> args = new Dictionary<string, object>();
            args["client_id"] = _cfg.DiscordClientId;
            args["scopes"] = Scopes;
            args["prompt"] = "none";
            Dictionary<string, object> result;
            try
            {
                result = await conn.CommandAsync("AUTHORIZE", args, null, 5 * 60 * 1000, ct);
            }
            catch (DiscordRpcException ex)
            {
                throw new DiscordFatalException("Discord-Autorisierung abgelehnt oder fehlgeschlagen: " + ex.Message + " (Code " + ex.Code + ").");
            }
            catch (TimeoutException)
            {
                throw new DiscordFatalException("Discord-Autorisierung wurde nicht innerhalb von 5 Minuten bestätigt.");
            }

            string code = Json.GetString(result, "code");
            if (string.IsNullOrEmpty(code)) throw new DiscordFatalException("Discord hat keinen Autorisierungscode geliefert.");
            TokenSet fresh = await RequestTokenAsync("authorization_code", code, ct);
            SaveToken(fresh);
            if (!await TryAuthenticateAsync(conn, fresh.AccessToken, ct))
            {
                throw new DiscordFatalException("Discord akzeptiert das neue Token nicht.");
            }
        }

        async Task<bool> TryAuthenticateAsync(DiscordConnection conn, string accessToken, CancellationToken ct)
        {
            Dictionary<string, object> args = new Dictionary<string, object>();
            args["access_token"] = accessToken;
            try
            {
                await conn.CommandAsync("AUTHENTICATE", args, null, 15000, ct);
                return true;
            }
            catch (DiscordRpcException ex)
            {
                Log.Debug("Discord AUTHENTICATE abgelehnt: " + ex.Message + " (Code " + ex.Code + ")");
                return false;
            }
        }

        async Task<TokenSet> RequestTokenAsync(string grantType, string value, CancellationToken ct)
        {
            Dictionary<string, string> form = new Dictionary<string, string>();
            form["client_id"] = _cfg.DiscordClientId;
            form["client_secret"] = _cfg.DiscordClientSecret;
            form["grant_type"] = grantType;
            if (grantType == "authorization_code")
            {
                form["code"] = value;
                form["redirect_uri"] = _cfg.DiscordRedirectUri;
            }
            else
            {
                form["refresh_token"] = value;
            }

            string url = _cfg.DiscordApiBase + "/oauth2/token";
            HttpResult response = await Task.Run(() => Http.PostForm(url, form), ct);

            Dictionary<string, object> body = null;
            try { body = Json.AsObject(Json.Parse(response.Body)); }
            catch (Exception) { }

            if (response.Status == 200 && body != null && !string.IsNullOrEmpty(Json.GetString(body, "access_token")))
            {
                TokenSet t = new TokenSet();
                t.AccessToken = Json.GetString(body, "access_token");
                t.RefreshToken = Json.GetString(body, "refresh_token");
                long? expiresIn = Json.GetLong(body, "expires_in");
                t.ExpiresAtUtc = DateTime.UtcNow.AddSeconds(expiresIn.HasValue ? expiresIn.Value : 3600);
                return t;
            }

            string error = Json.GetString(body, "error") ?? ("HTTP " + response.Status);
            string description = Json.GetString(body, "error_description") ?? Log.Shorten(response.Body, 200);
            if (error == "invalid_client")
            {
                throw new DiscordFatalException("Discord lehnt Client-ID/Client-Secret ab – bitte in der config.json prüfen (" + description + ").");
            }
            if (grantType == "authorization_code" && (error == "invalid_grant" || error == "invalid_request"))
            {
                throw new DiscordFatalException("Discord lehnt den Autorisierungscode ab (" + error + ": " + description
                    + "). Ist im Developer Portal unter OAuth2 → Redirects genau " + _cfg.DiscordRedirectUri + " eingetragen?");
            }
            throw new InvalidOperationException("Token-Anfrage fehlgeschlagen (" + error + ": " + description + ").");
        }

        sealed class TokenSet
        {
            public string AccessToken;
            public string RefreshToken;
            public DateTime ExpiresAtUtc;
        }

        TokenSet LoadToken()
        {
            try
            {
                if (!File.Exists(_tokenPath)) return null;
                byte[] raw = ProtectedData.Unprotect(File.ReadAllBytes(_tokenPath), TokenEntropy, DataProtectionScope.CurrentUser);
                Dictionary<string, object> o = Json.AsObject(Json.Parse(Encoding.UTF8.GetString(raw)));
                if (Json.GetString(o, "client_id") != _cfg.DiscordClientId) return null;   // andere Discord-App
                TokenSet t = new TokenSet();
                t.AccessToken = Json.GetString(o, "access_token");
                t.RefreshToken = Json.GetString(o, "refresh_token");
                long? expiresAt = Json.GetLong(o, "expires_at");
                t.ExpiresAtUtc = expiresAt.HasValue ? UnixEpoch.AddSeconds(expiresAt.Value) : DateTime.MinValue;
                return t;
            }
            catch (Exception ex)
            {
                Log.Warn("Gespeichertes Discord-Token ist nicht lesbar – es wird eine neue Autorisierung nötig (" + ex.Message + ").");
                return null;
            }
        }

        void SaveToken(TokenSet t)
        {
            try
            {
                Dictionary<string, object> o = new Dictionary<string, object>();
                o["client_id"] = _cfg.DiscordClientId;
                o["access_token"] = t.AccessToken;
                o["refresh_token"] = t.RefreshToken;
                o["expires_at"] = (long)(t.ExpiresAtUtc - UnixEpoch).TotalSeconds;
                byte[] data = ProtectedData.Protect(Encoding.UTF8.GetBytes(Json.Stringify(o)), TokenEntropy, DataProtectionScope.CurrentUser);
                string tmp = _tokenPath + ".tmp";
                File.WriteAllBytes(tmp, data);
                if (File.Exists(_tokenPath)) File.Delete(_tokenPath);
                File.Move(tmp, _tokenPath);
            }
            catch (Exception ex)
            {
                Log.Warn("Discord-Token konnte nicht gespeichert werden: " + ex.Message);
            }
        }

        void DeleteToken()
        {
            try { if (File.Exists(_tokenPath)) File.Delete(_tokenPath); }
            catch (Exception) { }
        }
    }

    public sealed class HttpResult
    {
        public int Status;
        public string Body;
    }

    public static class Http
    {
        public static HttpResult PostForm(string url, IDictionary<string, string> form)
        {
            try
            {
                ServicePointManager.SecurityProtocol = ServicePointManager.SecurityProtocol | SecurityProtocolType.Tls12;
            }
            catch (Exception)
            {
            }

            StringBuilder sb = new StringBuilder();
            foreach (KeyValuePair<string, string> kv in form)
            {
                if (sb.Length > 0) sb.Append('&');
                sb.Append(Uri.EscapeDataString(kv.Key)).Append('=').Append(Uri.EscapeDataString(kv.Value ?? ""));
            }
            byte[] body = Encoding.UTF8.GetBytes(sb.ToString());

            HttpWebRequest request = (HttpWebRequest)WebRequest.Create(url);
            request.Method = "POST";
            request.ContentType = "application/x-www-form-urlencoded";
            request.Accept = "application/json";
            request.UserAgent = "WaveMuteSync/" + Program.Version;
            request.Timeout = 20000;
            request.ReadWriteTimeout = 20000;
            request.ContentLength = body.Length;
            using (Stream s = request.GetRequestStream())
            {
                s.Write(body, 0, body.Length);
            }

            HttpWebResponse response;
            try
            {
                response = (HttpWebResponse)request.GetResponse();
            }
            catch (WebException ex)
            {
                response = ex.Response as HttpWebResponse;
                if (response == null) throw;
            }

            using (response)
            using (StreamReader reader = new StreamReader(response.GetResponseStream(), Encoding.UTF8))
            {
                HttpResult result = new HttpResult();
                result.Status = (int)response.StatusCode;
                result.Body = reader.ReadToEnd();
                return result;
            }
        }
    }

    // =================================================================================
    //  Ziel: Microsoft Teams (neues Teams, Windows UI Automation)
    // =================================================================================
    public enum TeamsMicState
    {
        Unknown,
        Live,
        Muted
    }

    // Trennt die Entscheidungslogik von den Windows-Aufrufen (dadurch testbar).
    public interface ITeamsUi
    {
        bool IsTeamsUsingMic();                 // Teams läuft und benutzt gerade das Mikrofon
        object FindMicButton();                 // null = kein Anruf-Button gefunden
        string ReadButtonName(object button);   // null = nicht lesbar
        bool PressButton(object button);        // false = nicht auslösbar
    }

    public sealed class TeamsSink : IMicStateListener
    {
        enum Phase { Idle, Waiting, InCall }

        readonly Config _cfg;
        readonly MicStateHub _hub;
        readonly ITeamsUi _ui;
        readonly Regex _mutedRx;
        readonly Regex _liveRx;
        readonly AutoResetEvent _wake = new AutoResetEvent(false);
        readonly object _sync = new object();
        Thread _thread;

        bool _hasPending;
        bool _pendingMuted;
        bool _pendingChange;
        DateTime _pendingAtUtc;

        Phase _phase = Phase.Idle;
        DateTime _phaseSinceUtc = DateTime.UtcNow;
        DateTime _lastUiCheckUtc = DateTime.MinValue;
        int _missCount;
        string _lastUnknownName;
        string _lastError;

        public TeamsSink(Config cfg, MicStateHub hub, ITeamsUi ui)
        {
            _cfg = cfg;
            _hub = hub;
            _ui = ui;
            _mutedRx = MakeRegex(cfg.TeamsMutedNamePattern, Config.DefaultTeamsMutedPattern, "mutedNamePattern");
            _liveRx = MakeRegex(cfg.TeamsLiveNamePattern, Config.DefaultTeamsLivePattern, "liveNamePattern");
        }

        static Regex MakeRegex(string pattern, string fallback, string name)
        {
            try
            {
                return new Regex(pattern, RegexOptions.CultureInvariant);
            }
            catch (ArgumentException ex)
            {
                Log.Warn("teams." + name + " in der config.json ist ungültig (" + ex.Message + ") – nutze Standard.");
                return new Regex(fallback, RegexOptions.CultureInvariant);
            }
        }

        public void Start(CancellationToken ct)
        {
            _thread = new Thread(delegate() { Loop(ct); });
            _thread.IsBackground = true;
            _thread.Name = "WaveMuteSync.Teams";
            _thread.Start();
        }

        public void Join(int timeoutMs)
        {
            if (_thread != null) _thread.Join(timeoutMs);
        }

        public void OnMicState(bool muted, bool isChange)
        {
            lock (_sync)
            {
                _hasPending = true;
                _pendingMuted = muted;
                _pendingChange = _pendingChange || isChange;
                _pendingAtUtc = DateTime.UtcNow;
            }
            _wake.Set();
        }

        void Loop(CancellationToken ct)
        {
            Log.Info("Teams-Sync aktiv (reagiert, sobald du in einem Teams-Anruf bist).");
            WaitHandle[] handles = new WaitHandle[] { _wake, ct.WaitHandle };
            while (!ct.IsCancellationRequested)
            {
                WaitHandle.WaitAny(handles, _phase == Phase.Idle ? _cfg.TeamsIdlePollMs : _cfg.TeamsPollMs);
                if (ct.IsCancellationRequested) break;
                try
                {
                    Tick();
                    _lastError = null;
                }
                catch (Exception ex)
                {
                    string message = ex.GetType().Name + ": " + ex.Message;
                    if (message != _lastError)
                    {
                        Log.Warn("Teams: " + message);
                        _lastError = message;
                    }
                }
            }
        }

        // Ein Arbeitsschritt – wird regelmäßig und sofort nach jedem Antippen aufgerufen.
        public void Tick()
        {
            bool has, target, change;
            DateTime at;
            lock (_sync)
            {
                has = _hasPending;
                target = _pendingMuted;
                change = _pendingChange;
                at = _pendingAtUtc;
            }
            bool fresh = has && (DateTime.UtcNow - at).TotalSeconds < 10;
            if (has && !fresh) ClearPending();

            if (!_ui.IsTeamsUsingMic())
            {
                if (_phase == Phase.InCall) Log.Info("Teams: Anruf beendet.");
                SetPhase(Phase.Idle);
                if (fresh) ClearPending();   // ohne Anruf gibt es nichts umzuschalten
                return;
            }
            if (_phase == Phase.Idle) SetPhase(Phase.Waiting);

            // Den UI-Baum nur bei Bedarf durchsuchen – das spart CPU.
            double sinceCheck = (DateTime.UtcNow - _lastUiCheckUtc).TotalMilliseconds;
            if (!fresh)
            {
                if (_phase == Phase.InCall && sinceCheck < _cfg.TeamsInCallCheckMs) return;
                if (_phase == Phase.Waiting && (DateTime.UtcNow - _phaseSinceUtc).TotalSeconds > 60 && sinceCheck < 5000) return;
            }

            object button = _ui.FindMicButton();
            _lastUiCheckUtc = DateTime.UtcNow;
            if (button == null)
            {
                if (_phase == Phase.InCall && ++_missCount >= 2)
                {
                    Log.Info("Teams: Anruf-Steuerung nicht mehr gefunden.");
                    SetPhase(Phase.Waiting);
                }
                return;   // ein frisches Antippen bleibt bis zu 10 s vorgemerkt
            }
            _missCount = 0;

            TeamsMicState state = ReadState(button);
            bool joined = _phase != Phase.InCall;
            if (joined)
            {
                SetPhase(Phase.InCall);
                Log.Info("Teams: Anruf erkannt (Teams-Mikro " + Describe(state) + ").");
            }
            if (state == TeamsMicState.Unknown) return;

            if (fresh)
            {
                ClearPending();
                bool teamsMuted = state == TeamsMicState.Muted;
                // Stummschalten immer; Entstummen nur nach echtem Antippen.
                if (teamsMuted != target && (target || change)) Press(button, target);
            }
            else if (joined && _hub.Muted == true && state == TeamsMicState.Live)
            {
                Log.Info("Teams: Wave:3 ist stumm – schalte Teams beim Beitritt ebenfalls stumm.");
                Press(button, true);
            }
        }

        void SetPhase(Phase phase)
        {
            if (_phase == phase) return;
            _phase = phase;
            _phaseSinceUtc = DateTime.UtcNow;
            _missCount = 0;
        }

        void ClearPending()
        {
            lock (_sync)
            {
                _hasPending = false;
                _pendingChange = false;
            }
        }

        TeamsMicState ReadState(object button)
        {
            string name = _ui.ReadButtonName(button);
            if (name == null) return TeamsMicState.Unknown;
            Log.Debug("Teams-Mikrofon-Button: \"" + name + "\"");
            if (_mutedRx.IsMatch(name)) return TeamsMicState.Muted;
            if (_liveRx.IsMatch(name)) return TeamsMicState.Live;
            if (name != _lastUnknownName)
            {
                Log.Warn("Teams: Beschriftung des Mikrofon-Buttons nicht erkannt: \"" + name
                    + "\" – bitte teams.mutedNamePattern / liveNamePattern in der config.json anpassen (siehe README).");
                _lastUnknownName = name;
            }
            return TeamsMicState.Unknown;
        }

        void Press(object button, bool wantMuted)
        {
            if (!_ui.PressButton(button))
            {
                Log.Warn("Teams: Der Mikrofon-Button ließ sich nicht auslösen.");
                return;
            }
            TeamsMicState want = wantMuted ? TeamsMicState.Muted : TeamsMicState.Live;
            for (int i = 0; i < 14; i++)
            {
                Thread.Sleep(150);
                object b = _ui.FindMicButton();
                if (b != null && ReadState(b) == want)
                {
                    Log.Info(wantMuted ? "Teams: stummgeschaltet." : "Teams: Stummschaltung aufgehoben.");
                    return;
                }
            }
            Log.Warn("Teams: Button wurde gedrückt, der neue Zustand ließ sich aber nicht bestätigen.");
        }

        static string Describe(TeamsMicState s)
        {
            if (s == TeamsMicState.Muted) return "stumm";
            if (s == TeamsMicState.Live) return "aktiv";
            return "unbekannt";
        }
    }

    // Echte Windows-Anbindung für TeamsSink.
    public sealed class UiaTeamsUi : ITeamsUi
    {
        const string ConsentStoreMicrophone = @"Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone";

        readonly PropertyCondition _micButtonCondition =
            new PropertyCondition(AutomationElement.AutomationIdProperty, "microphone-button");
        AutomationElement _cachedWindow;
        List<int> _teamsPids = new List<int>();
        DateTime _pidsAtUtc = DateTime.MinValue;

        public bool IsTeamsUsingMic()
        {
            if (TeamsPids().Count == 0) return false;
            bool? capturing = RegistrySaysTeamsCapturing();
            return capturing.HasValue ? capturing.Value : true;   // unbekannt -> lieber nachsehen
        }

        public object FindMicButton()
        {
            if (_cachedWindow != null)
            {
                AutomationElement b = FindIn(_cachedWindow);
                if (b != null) return b;
                _cachedWindow = null;
            }
            foreach (int pid in TeamsPids())
            {
                AutomationElementCollection windows;
                try
                {
                    windows = AutomationElement.RootElement.FindAll(TreeScope.Children,
                        new PropertyCondition(AutomationElement.ProcessIdProperty, pid));
                }
                catch (Exception)
                {
                    continue;
                }
                foreach (AutomationElement window in windows)
                {
                    AutomationElement b = FindIn(window);
                    if (b != null)
                    {
                        _cachedWindow = window;
                        return b;
                    }
                }
            }
            return null;
        }

        public string ReadButtonName(object button)
        {
            try
            {
                return ((AutomationElement)button).Current.Name ?? "";
            }
            catch (Exception)
            {
                return null;
            }
        }

        public bool PressButton(object button)
        {
            try
            {
                object pattern;
                if (!((AutomationElement)button).TryGetCurrentPattern(InvokePattern.Pattern, out pattern)) return false;
                ((InvokePattern)pattern).Invoke();
                return true;
            }
            catch (Exception ex)
            {
                Log.Debug("Teams: Invoke fehlgeschlagen: " + ex.Message);
                return false;
            }
        }

        AutomationElement FindIn(AutomationElement window)
        {
            try
            {
                return window.FindFirst(TreeScope.Descendants, _micButtonCondition);
            }
            catch (Exception)
            {
                return null;
            }
        }

        List<int> TeamsPids()
        {
            if ((DateTime.UtcNow - _pidsAtUtc).TotalSeconds < 10) return _teamsPids;
            List<int> pids = new List<int>();
            foreach (Process p in Process.GetProcesses())
            {
                try
                {
                    string n = p.ProcessName;
                    if (string.Equals(n, "ms-teams", StringComparison.OrdinalIgnoreCase)
                        || string.Equals(n, "Teams", StringComparison.OrdinalIgnoreCase))
                    {
                        pids.Add(p.Id);
                    }
                }
                catch (Exception)
                {
                }
                finally
                {
                    p.Dispose();
                }
            }
            _teamsPids = pids;
            _pidsAtUtc = DateTime.UtcNow;
            return pids;
        }

        // Windows vermerkt pro App, ob sie gerade das Mikrofon benutzt (LastUsedTimeStop = 0).
        // Teams hält das Mikro während des ganzen Anrufs offen – auch wenn es stumm ist.
        static bool? RegistrySaysTeamsCapturing()
        {
            try
            {
                using (RegistryKey mic = Registry.CurrentUser.OpenSubKey(ConsentStoreMicrophone))
                {
                    if (mic == null) return null;
                    bool found = false;
                    if (AnyTeamsCapturing(mic, ref found)) return true;
                    using (RegistryKey nonPackaged = mic.OpenSubKey("NonPackaged"))
                    {
                        if (nonPackaged != null && AnyTeamsCapturing(nonPackaged, ref found)) return true;
                    }
                    if (found) return false;
                    return null;
                }
            }
            catch (Exception)
            {
                return null;
            }
        }

        static bool AnyTeamsCapturing(RegistryKey parent, ref bool found)
        {
            foreach (string name in parent.GetSubKeyNames())
            {
                bool isTeams = name.StartsWith("MSTeams_", StringComparison.OrdinalIgnoreCase)
                    || name.EndsWith("#ms-teams.exe", StringComparison.OrdinalIgnoreCase)
                    || name.EndsWith("#teams.exe", StringComparison.OrdinalIgnoreCase);
                if (!isTeams) continue;
                using (RegistryKey app = parent.OpenSubKey(name))
                {
                    if (app == null) continue;
                    object stop = app.GetValue("LastUsedTimeStop");
                    if (stop == null) continue;
                    found = true;
                    if (stop is long && (long)stop == 0) return true;
                }
            }
            return false;
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
