using System;
using Windows.ApplicationModel;
using Windows.Storage;
using Windows.System.Display;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;

namespace WebKitTlsSmoke
{
    /// <summary>
    /// TlsSmoke application host: launches, shows the probe page, and drives the
    /// unchanged native transport probe payload. This host owns activation,
    /// window, LocalState/package path resolution and the display keep-awake;
    /// it performs no TLS itself and sets no certificate policy, so the
    /// fail-closed verified phase and the diagnostic insecure phase stay
    /// independent of each other and of this shell.
    /// </summary>
    sealed partial class App : Application
    {
        /// <summary>Success HRESULT logged once the managed host is running.</summary>
        private const uint HostStartupSuccess = 0;

        /// <summary>Initializes the application object.</summary>
        public App()
        {
            InitializeComponent();
        }

        /// <summary>
        /// Invoked when the application is launched. Shows the main page, then
        /// runs the native probe off the UI thread while the display request
        /// keeps the device awake, exactly as the former native view did.
        /// </summary>
        /// <param name="e">Launch details.</param>
        protected override async void OnLaunched(Windows.ApplicationModel.Activation.LaunchActivatedEventArgs e)
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
            var page = rootFrame.Content as MainPage;
            await RunProbeAsync(page);
        }

        /// <summary>
        /// Resolves the report and bundle paths through WinRT, emits the
        /// host-owned report lines, runs the native phases, and shows the
        /// resulting report on the main page.
        /// </summary>
        /// <param name="page">The displayed main page, if navigation succeeded.</param>
        private static async System.Threading.Tasks.Task RunProbeAsync(MainPage page)
        {
            var request = new DisplayRequest();
            request.RequestActive();
            try
            {
                var localFolder = ApplicationData.Current.LocalFolder;
                var logPath = System.IO.Path.Combine(localFolder.Path, TlsProbeInterop.ReportFilename);
                TlsProbeInterop.WKSetTlsSmokeLogPath(logPath);
                TlsProbeInterop.LogResult(TlsProbeInterop.StartupStage, HostStartupSuccess);

                string caBundlePath = string.Empty;
                try
                {
                    var installedLocation = Package.Current.InstalledLocation;
                    var bundleFile = await installedLocation.TryGetItemAsync(TlsProbeInterop.CaBundleFilename);
                    if (bundleFile != null)
                    {
                        caBundlePath = System.IO.Path.Combine(installedLocation.Path, TlsProbeInterop.CaBundleFilename);
                    }
                }
                catch (Exception caught)
                {
                    TlsProbeInterop.LogResult(TlsProbeInterop.PackagePathStage, unchecked((uint)caught.HResult));
                    caBundlePath = string.Empty;
                }

                await System.Threading.Tasks.Task.Run(() => TlsProbeInterop.RunProbes(caBundlePath));

                string report = await ReadReportAsync(localFolder);
                page?.ShowResult(report);
            }
            finally
            {
                request.RequestRelease();
            }
        }

        /// <summary>
        /// Reads the native report back from LocalState for on-screen display.
        /// </summary>
        /// <param name="localFolder">Application LocalState folder.</param>
        /// <returns>The report text, or a diagnostic placeholder when unreadable.</returns>
        private static async System.Threading.Tasks.Task<string> ReadReportAsync(StorageFolder localFolder)
        {
            try
            {
                var file = await localFolder.GetFileAsync(TlsProbeInterop.ReportFilename);
                return await FileIO.ReadTextAsync(file);
            }
            catch (Exception)
            {
                return "tls-smoke report unavailable";
            }
        }
    }
}
