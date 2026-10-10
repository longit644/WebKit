using System;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Windows.ApplicationModel;
using Windows.ApplicationModel.Activation;
using Windows.Storage;
using Windows.System.Display;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;

namespace WebKitWebView.RuntimeSmoke
{
    /// <summary>
    /// RuntimeSmoke application host. Owns activation, window lifetime, launch
    /// sequencing and result display in C# over the unchanged native smoke
    /// payload (WebKitRuntimeSmoke.dll). Diagnostic stages, constants and the
    /// LocalState/runtime-smoke.txt line protocol are preserved from the
    /// retired native host; see payload/SmokeDiagnostics.cpp and README.md
    /// for the old-to-new host line mapping.
    /// </summary>
    sealed partial class App : Application
    {
        /// <summary>Report and package version matching the staged manifest.</summary>
        private const string ReportVersion = "1.0.0.11";

        /// <summary>Diagnostic log file inside the package LocalState folder.</summary>
        private const string LogFileName = "runtime-smoke.txt";

        /// <summary>Fallback snapshot width when no usable window bounds exist.</summary>
        private const uint SnapshotWidth = 480;

        /// <summary>Fallback snapshot height when no usable window bounds exist.</summary>
        private const uint SnapshotHeight = 800;

        /// <summary>Largest snapshot extent the compositor can present.</summary>
        private const double MaxPageDimension = 2048.0;

        /// <summary>Keeps the display awake while the one-frame smoke app runs.</summary>
        private static DisplayRequest displayRequest;

        /// <summary>
        /// Runs the full native diagnostics sequence on the calling UI thread.
        /// </summary>
        /// <param name="fontDirectory">Staged Fonts directory beside the package.</param>
        /// <param name="logPath">Append target for the protocol lines.</param>
        /// <param name="width">Snapshot width in pixels.</param>
        /// <param name="height">Snapshot height in pixels.</param>
        /// <param name="coreWindow">Owning CoreWindow for D3D presentation.</param>
        /// <returns>Zero when every required native stage succeeded.</returns>
        [DllImport("WebKitRuntimeSmoke.dll", CharSet = CharSet.Unicode, CallingConvention = CallingConvention.StdCall, ExactSpelling = true)]
        private static extern uint WKRunSmokeDiagnostics(
            string fontDirectory,
            string logPath,
            uint width,
            uint height,
            [MarshalAs(UnmanagedType.IUnknown)] object coreWindow);

        /// <summary>
        /// Initializes the application object.
        /// </summary>
        public App()
        {
            InitializeComponent();
        }

        /// <summary>
        /// Invoked when the application is launched. Shows the main page and
        /// runs the native diagnostics on the owning UI thread.
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
            await RunSmokeAsync((MainPage)rootFrame.Content);
        }

        /// <summary>
        /// Writes the host protocol lines, invokes the native payload and
        /// displays the combined verdict on the main page.
        /// </summary>
        /// <param name="page">The displayed main page.</param>
        private static async Task RunSmokeAsync(MainPage page)
        {
            var passed = false;
            try
            {
                var folder = ApplicationData.Current.LocalFolder;
                var file = await folder.CreateFileAsync(
                    LogFileName, CreationCollisionOption.ReplaceExisting);
                await AppendLineAsync(file, "open-log", 0);
                await AppendLineAsync(file, "host-started", 0);
                await AppendLineAsync(file, "window-activated", 0);

                var bounds = Window.Current.Bounds;
                var width = bounds.Width > 0.0 && bounds.Width <= MaxPageDimension
                    ? (uint)bounds.Width : SnapshotWidth;
                var height = bounds.Height > 0.0 && bounds.Height <= MaxPageDimension
                    ? (uint)bounds.Height : SnapshotHeight;
                await AppendLineAsync(file, "page-viewport-width", width);
                await AppendLineAsync(file, "page-viewport-height", height);

                displayRequest = new DisplayRequest();
                displayRequest.RequestActive();

                var fonts = Package.Current.InstalledLocation.Path + "\\Fonts";
                var diagnostics = WKRunSmokeDiagnostics(
                    fonts, file.Path, width, height, Window.Current.CoreWindow);
                await AppendLineAsync(file, "diagnostics-returned", diagnostics);
                passed = diagnostics == 0;
            }
            catch (Exception exception)
            {
                try
                {
                    var folder = ApplicationData.Current.LocalFolder;
                    var file = await folder.CreateFileAsync(
                        LogFileName, CreationCollisionOption.OpenIfExists);
                    await AppendLineAsync(file, "host-pinvoke", unchecked((uint)exception.HResult));
                }
                catch (Exception)
                {
                    passed = false;
                }
            }

            page?.ShowResult(passed);
        }

        /// <summary>
        /// Appends one protocol line using the native "%s: 0x%08lx" format.
        /// </summary>
        /// <param name="file">Open log file receiving the line.</param>
        /// <param name="stage">Stage name prefixing the line.</param>
        /// <param name="result">Numeric result rendered as eight hex digits.</param>
        private static async Task AppendLineAsync(StorageFile file, string stage, uint result)
        {
            var line = string.Format("{0}: 0x{1:x8}\r\n", stage, result);
            await FileIO.AppendTextAsync(file, line);
        }
    }
}
