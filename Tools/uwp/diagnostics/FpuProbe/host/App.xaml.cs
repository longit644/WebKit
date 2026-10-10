using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using Windows.ApplicationModel.Activation;
using Windows.Storage;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;

namespace WebKitFpuProbe
{
    /// <summary>
    /// FpuProbe C# application host. Owns activation, window and launch; the
    /// ARM32 setjmp/longjmp and floating-point work itself stays in the native
    /// FpuProbePayload.dll and is invoked once on the owning UI thread, which
    /// matches the thread the retired WRL IFrameworkView ran it on.
    /// </summary>
    sealed partial class App : Application
    {
        /// <summary>Report version matching the staged package version.</summary>
        private const string ReportVersion = "1.0.0.12";

        /// <summary>Name of the native probe log inside LocalState.</summary>
        private const string ProbeLogName = "fpuprobe.log";

        /// <summary>Name of this host's verification summary inside LocalState.</summary>
        private const string SummaryName = "fpuprobe-csharp.txt";

        /// <summary>
        /// Initializes the application object.
        /// </summary>
        public App()
        {
            InitializeComponent();
        }

        /// <summary>
        /// Invoked when the application is launched. Shows the main page, runs
        /// the native payload, then verifies and records its report lines.
        /// </summary>
        /// <param name="e">Launch details.</param>
        protected override async void OnLaunched(LaunchActivatedEventArgs e)
        {
            var rootFrame = Window.Current.Content as Frame;
            if (rootFrame == null)
            {
                rootFrame = new Frame();
                Window.Current.Content = rootFrame;
            }

            if (rootFrame.Content == null)
            {
                rootFrame.Navigate(typeof(MainPage), e.Arguments);
            }

            Window.Current.Activate();
            await RunProbeAsync(rootFrame.Content as MainPage);
        }

        /// <summary>
        /// Runs the native payload on the owning UI thread, reads back its log,
        /// checks every retained arithmetic expectation, and records the summary.
        /// </summary>
        /// <param name="page">The displayed main page, if navigation succeeded.</param>
        private static async Task RunProbeAsync(MainPage page)
        {
            var lines = new StringBuilder();
            var checks = 0;
            var failures = 0;

            void Check(bool value, string name)
            {
                checks++;
                if (!value)
                {
                    failures++;
                }

                lines.Append(value ? "PASS: " : "FAIL: ");
                lines.AppendLine(name);
            }

            int nativeResult = -1;
            IList<string> logLines = new List<string>();
            try
            {
                // Same thread the retired WRL host used: the UI/engine thread
                // that owns this window. setjmp/longjmp state is thread-local
                // by construction, so the calling thread is load-bearing.
                Check(Window.Current != null, "a current window exists");
                Check(page != null, "main page constructed from compiled XAML");
                nativeResult = NativeMethods.RunProbe();
                Check(nativeResult == 0, "native payload completed (WKFpuProbeRun == 0)");

                logLines = await ReadProbeLogAsync();
                Check(logLines.Count > 0, "native probe log is present and non-empty");
            }
            catch (Exception)
            {
                Check(false, "no uncaught launch exception");
            }

            if (logLines.Count > 0)
            {
                VerifyReport(logLines, Check);
            }

            var header = new StringBuilder();
            header.Append("version=").Append(ReportVersion).Append("\r\n");
            header.Append("result=").Append(failures == 0 ? "PASS" : "FAIL").Append("\r\n");
            header.Append("checks=").Append(checks).Append("\r\n");
            header.Append("failures=").Append(failures).Append("\r\n");
            header.Append("native=").Append(nativeResult).Append("\r\n");
            lines.Insert(0, header.ToString());

            try
            {
                var folder = ApplicationData.Current.LocalFolder;
                var file = await folder.CreateFileAsync(
                    SummaryName, CreationCollisionOption.ReplaceExisting);
                await FileIO.WriteTextAsync(file, lines.ToString());
                page?.ShowReport(failures == 0, lines.ToString());
            }
            catch (Exception)
            {
                page?.ShowReport(false, lines.ToString());
            }
        }

        /// <summary>
        /// Reads the native payload's report lines from LocalState.
        /// </summary>
        /// <returns>The raw log lines without trailing blank entries.</returns>
        private static async Task<IList<string>> ReadProbeLogAsync()
        {
            var folder = ApplicationData.Current.LocalFolder;
            var file = await folder.GetFileAsync(ProbeLogName);
            var text = await FileIO.ReadTextAsync(file);
            var raw = text.Split(new[] { "\r\n", "\n" }, StringSplitOptions.None);
            var kept = new List<string>();
            foreach (var line in raw)
            {
                if (!string.IsNullOrWhiteSpace(line))
                {
                    kept.Add(line.Trim());
                }
            }

            return kept;
        }

        /// <summary>
        /// Checks the retained native arithmetic and ABI expectations against
        /// the report lines. Values are the pre-migration inventory: baseline
        /// 2.0*4.0*1000 = 8000 (0x1F40), inside 2.0*3.0*1000 = 6000 (0x1770),
        /// final 6.0*5.0*1000 = 30000 (0x7530), integer 7/9/7, CRT SP round
        /// trip 0 then 11, FP longjmp value 7. FPSCR trap-enable bits must stay
        /// clear: user mode can read FPSCR but not set those bits.
        /// </summary>
        /// <param name="logLines">The raw native report lines.</param>
        /// <param name="check">The recording assertion callback.</param>
        private static void VerifyReport(IList<string> logLines, Action<bool, string> check)
        {
            var values = new Dictionary<string, List<ulong>>();
            foreach (var line in logLines)
            {
                var separator = line.IndexOf(':');
                if (separator <= 0)
                {
                    continue;
                }

                var stage = line.Substring(0, separator).Trim();
                var valueText = line.Substring(separator + 1).Trim();
                if (valueText.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                {
                    valueText = valueText.Substring(2);
                }

                if (ulong.TryParse(
                    valueText,
                    System.Globalization.NumberStyles.HexNumber,
                    System.Globalization.CultureInfo.InvariantCulture,
                    out var value))
                {
                    if (!values.TryGetValue(stage, out var seen))
                    {
                        seen = new List<ulong>();
                        values[stage] = seen;
                    }

                    seen.Add(value);
                }
            }

            bool Has(string stage, ulong expected)
            {
                return values.TryGetValue(stage, out var seen) && seen.Contains(expected);
            }

            bool Sequence(string stage, ulong first, ulong second)
            {
                return values.TryGetValue(stage, out var seen)
                    && seen.Count >= 2
                    && seen[0] == first
                    && seen[seen.Count - 1] == second;
            }

            check(Has("fpuprobe-startup", 0), "payload startup returned S_OK");
            check(Has("probe-log-opened", 1), "probe log opened");
            check(Has("probe-run-entered", 1), "payload run entered");
            check(Has("probe-crt-symbols-found", 1), "CRT setjmp/longjmp symbols resolved");
            check(Sequence("probe-crt-sp-return", 0, 11), "CRT SP frame probe round trip 0 then 11");
            check(Has("probe-crt-sp-survived", 1), "CRT SP frame probe survived longjmp");
            check(Sequence("probe-int-setjmp-returned", 0, 9), "integer probe round trip 0 then 9");
            check(Has("probe-int-before-longjmp", 7), "integer value before longjmp is 7");
            check(Has("probe-int-after-longjmp", 9), "integer probe landed with 9");
            check(Has("probe-int-value", 7), "integer local survived longjmp as 7");
            check(Has("probe-baseline-multiply", 0x1F40), "FP baseline 2.0*4.0*1000 is 8000");
            check(Sequence("probe-setjmp-returned", 0, 7), "FP probe round trip 0 then 7");
            check(Has("probe-inside-before-longjmp", 0x1770), "FP value before longjmp is 6000");
            check(Has("probe-after-longjmp", 7), "FP probe landed with 7");
            check(Has("probe-fpscr-trap-bits", 0), "FPSCR trap-enable bits stay clear");
            check(Has("probe-fp-op-after-longjmp-survived", 1), "FP work continues after longjmp");
            check(Has("probe-fp-result-x1000", 0x7530), "FP result 6.0*5.0*1000 is 30000");
            check(Has("fpuprobe-completed", 1), "payload sequence completed");
        }
    }
}
