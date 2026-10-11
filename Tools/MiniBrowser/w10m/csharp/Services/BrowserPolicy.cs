using System;
using System.IO;

namespace WebKitMiniBrowser
{
    /// <summary>Pure validation, templating, and warning-eligibility rules for BrowserPolicy.</summary>
    internal static class BrowserPolicyRules
    {
        /// <summary>Placeholder replaced with the escaped failed request URI.</summary>
        internal const string UriPlaceholder = "{{uri}}";
        /// <summary>Placeholder replaced with the escaped warning-page title.</summary>
        internal const string TitlePlaceholder = "{{title}}";

        /// <summary>Determines whether a URI is an absolute HTTP or HTTPS target.</summary>
        /// <param name="uri">The candidate URI.</param>
        /// <returns>True only for an absolute HTTP(S) URI with a host.</returns>
        internal static bool IsAllowedUri(Uri uri)
        {
            return uri != null && uri.IsAbsoluteUri && !string.IsNullOrEmpty(uri.Host) &&
                (string.Equals(uri.Scheme, "http", StringComparison.OrdinalIgnoreCase) ||
                 string.Equals(uri.Scheme, "https", StringComparison.OrdinalIgnoreCase));
        }

        /// <summary>Escapes text for safe insertion into HTML text or quoted attributes.</summary>
        /// <param name="text">The untrusted text to escape.</param>
        /// <returns>HTML-escaped text, or an empty string for null.</returns>
        internal static string EscapeHtml(string text)
        {
            if (text == null)
                return string.Empty;
            return text.Replace("&", "&amp;").Replace("<", "&lt;").Replace(">", "&gt;")
                .Replace("\"", "&quot;").Replace("'", "&#39;");
        }

        /// <summary>Loads the highest-precedence valid warning template.</summary>
        /// <param name="overridePath">Optional app-owned explicit override path.</param>
        /// <param name="packagedPath">Path to the package blocked-page asset.</param>
        /// <returns>A valid template, using a built-in safe template when necessary.</returns>
        internal static string LoadTemplate(string overridePath, string packagedPath)
        {
            string value = TryReadTemplate(overridePath);
            if (value != null)
                return value;
            value = TryReadTemplate(packagedPath);
            return value ?? "<!doctype html><html><head><meta charset=\"utf-8\"><title>{{title}}</title></head>" +
                "<body><main><h1>{{title}}</h1><p>Unable to display the requested page.</p><p>{{uri}}</p></main></body></html>";
        }

        /// <summary>Reads the app-owned demo asset or returns a local-only fallback page.</summary>
        /// <param name="packagedPath">The optional path to the demo HTML asset.</param>
        /// <returns>Demo markup that does not contain a remote resource base.</returns>
        internal static string LoadDemo(string packagedPath)
        {
            string demo = TryReadFile(packagedPath);
            if (!string.IsNullOrWhiteSpace(demo) && IsSafeDemo(demo))
                return demo;
            return "<!doctype html><html><head><meta charset=\"utf-8\"><title>about:demo</title></head>" +
                "<body><h1>WebKit MiniBrowser</h1><p>Local application demo.</p></body></html>";
        }

        /// <summary>Rejects demo markup that could escape its opaque local origin.</summary>
        /// <param name="demo">The candidate demo markup.</param>
        /// <returns>True when the markup has no remote resource base element.</returns>
        internal static bool IsSafeDemo(string demo)
        {
            return demo != null && demo.IndexOf("<base", StringComparison.OrdinalIgnoreCase) < 0;
        }

        /// <summary>Reads one candidate template and rejects missing or malformed content.</summary>
        /// <param name="path">The optional file-system path to read.</param>
        /// <returns>The template when readable and valid; otherwise null.</returns>
        private static string TryReadTemplate(string path)
        {
            if (string.IsNullOrWhiteSpace(path))
                return null;
            string text = TryReadFile(path);
            return IsValidTemplate(text) ? text : null;
        }

        /// <summary>Reads a file without propagating missing-path or access errors.</summary>
        /// <param name="path">The optional app-owned file path.</param>
        /// <returns>The file contents or null if it cannot be read.</returns>
        private static string TryReadFile(string path)
        {
            if (string.IsNullOrWhiteSpace(path))
                return null;
            try
            {
                if (!File.Exists(path))
                    return null;
                return File.ReadAllText(path);
            }
            catch (IOException) { return null; }
            catch (UnauthorizedAccessException) { return null; }
            catch (NotSupportedException) { return null; }
            catch (ArgumentException) { return null; }
        }

        /// <summary>Checks that a warning template has HTML and both substitution tokens.</summary>
        /// <param name="text">The candidate template.</param>
        /// <returns>True when the template can be safely rendered by this policy.</returns>
        private static bool IsValidTemplate(string text)
        {
            return !string.IsNullOrWhiteSpace(text) && text.IndexOf("<html", StringComparison.OrdinalIgnoreCase) >= 0 &&
                text.IndexOf(UriPlaceholder, StringComparison.Ordinal) >= 0 &&
                text.IndexOf(TitlePlaceholder, StringComparison.Ordinal) >= 0;
        }
    }

    /// <summary>Tracks an exact remote token separately from its associated HostHtml ID.</summary>
    internal sealed class WarningEligibility
    {
        /// <summary>The opaque remote certificate decision object.</summary>
        private object _token;
        /// <summary>The ID of the associated HostHtml warning, not the remote failure.</summary>
        private ulong _hostHtmlId;
        /// <summary>Whether the explicit warning HostHtml proposal has begun.</summary>
        private bool _awaitingHostHtml;
        /// <summary>Whether the matching warning document completed successfully.</summary>
        private bool _ready;

        /// <summary>Begins an explicit warning-document proposal for a remote decision token.</summary>
        /// <param name="token">The opaque remote certificate-failure token.</param>
        internal void Begin(object token)
        {
            Invalidate();
            if (token == null)
                return;
            _token = token;
            _awaitingHostHtml = true;
        }

        /// <summary>Associates the next proposal only when it is the pending HostHtml warning.</summary>
        /// <param name="isHostHtml">Whether the synchronous proposal is HostHtml.</param>
        /// <param name="navigationId">The HostHtml proposal ID, never a token identifier.</param>
        /// <returns>True when this is the warning HostHtml proposal.</returns>
        internal bool ObserveStarting(bool isHostHtml, ulong navigationId)
        {
            if (_awaitingHostHtml && isHostHtml && navigationId != 0)
            {
                _awaitingHostHtml = false;
                _hostHtmlId = navigationId;
                return true;
            }
            Invalidate();
            return false;
        }

        /// <summary>Makes consent ready only after the associated HostHtml terminal succeeds.</summary>
        /// <param name="isHostHtml">Whether the terminal belongs to HostHtml content.</param>
        /// <param name="navigationId">The terminal main-navigation ID.</param>
        /// <param name="completed">Whether the terminal outcome is Completed.</param>
        /// <returns>True when the matching warning has become ready for explicit consent.</returns>
        internal bool ObserveCompleted(bool isHostHtml, ulong navigationId, bool completed)
        {
            if (_token != null && !_awaitingHostHtml && _hostHtmlId == navigationId && isHostHtml && completed)
            {
                _ready = true;
                return true;
            }
            if (_token != null && !_ready && !_awaitingHostHtml && _hostHtmlId == navigationId)
                Invalidate();
            return false;
        }

        /// <summary>Consumes consent only for the identical remote token after warning completion.</summary>
        /// <param name="token">The token retained from the remote HTTPS failure.</param>
        /// <returns>True once for the same token object after matching HostHtml completion.</returns>
        internal bool TryConsume(object token)
        {
            if (!_ready || !ReferenceEquals(_token, token))
                return false;
            Invalidate();
            return true;
        }

        /// <summary>Invalidates all pending or ready warning consent.</summary>
        internal void Invalidate()
        {
            _token = null;
            _hostHtmlId = 0;
            _awaitingHostHtml = false;
            _ready = false;
        }

        /// <summary>Reports whether a user-consent action is currently available.</summary>
        /// <returns>True only after the associated HostHtml document completed.</returns>
        internal bool IsReady { get { return _ready; } }

        /// <summary>Reports whether the remote decision is still pending or eligible.</summary>
        /// <returns>True while its associated warning flow has not been invalidated.</returns>
        internal bool IsActive { get { return _token != null; } }
    }
}
#if !POLICY_RULES_ONLY
namespace WebKitMiniBrowser.Services
{
    using WebKitMiniBrowser;
    using Windows.UI.Xaml;
    using WebKit;
    /// <summary>Applies app-owned navigation policy and gates certificate consent on warning-page commit.</summary>
    public sealed class BrowserPolicy : IDisposable
    {
        /// <summary>The public browser control managed by this policy.</summary>
        private readonly WebKitWebView _view;
        /// <summary>Package asset path used for the blocked-page template.</summary>
        private readonly string _packagedBlockedTemplatePath;
        /// <summary>Optional app-owned blocked-page override path.</summary>
        private readonly string _overridePath;
        /// <summary>Package asset path used by the application-owned demo command.</summary>
        private readonly string _packagedDemoTemplatePath;
        /// <summary>Exact-token and HostHtml-ID warning eligibility state.</summary>
        private readonly WarningEligibility _warning = new WarningEligibility();
        /// <summary>Last document title copied from the public control.</summary>
        private string _documentTitle = "Certificate warning";
        /// <summary>Whether this policy has revoked its event subscriptions.</summary>
        private bool _disposed;

        /// <summary>Raised when policy status text should be shown by the application shell.</summary>
        public event Action<string> StatusChanged;

        /// <summary>Raised when explicit consent is available or becomes unavailable.</summary>
        public event Action<bool> WarningConsentReady;

        /// <summary>Creates policy for a WebKit view and app-owned blocked-page templates.</summary>
        /// <param name="view">The public WebKit control to manage.</param>
        /// <param name="packagedBlockedTemplatePath">Path to the packaged Assets/blocked.html template.</param>
        /// <param name="overridePath">Optional readable app-owned override template path.</param>
        /// <param name="packagedDemoTemplatePath">Optional app-owned demo HTML asset.</param>
        public BrowserPolicy(WebKitWebView view, string packagedBlockedTemplatePath, string overridePath = null,
            string packagedDemoTemplatePath = null)
        {
            _view = view ?? throw new ArgumentNullException(nameof(view));
            _packagedBlockedTemplatePath = packagedBlockedTemplatePath;
            _overridePath = overridePath;
            _packagedDemoTemplatePath = packagedDemoTemplatePath;
            _view.NavigationStarting += OnNavigationStarting;
            _view.NavigationCompleted += OnNavigationCompleted;
            _view.DocumentTitleChanged += OnDocumentTitleChanged;
            _view.PropertyChanged += OnPropertyChanged;
        }

        /// <summary>Validates and navigates only to an absolute HTTP(S) URI.</summary>
        /// <param name="uri">The requested remote URI.</param>
        public void Navigate(Uri uri)
        {
            ThrowIfDisposed();
            InvalidateWarning();
            if (!BrowserPolicyRules.IsAllowedUri(uri))
            {
                RaiseStatus("Invalid address. Type an absolute http(s) URI.");
                return;
            }
            _view.Source = uri;
            RaiseStatus("Navigating to " + uri + ".");
        }

        /// <summary>Traverses backward and invalidates certificate-warning consent.</summary>
        public void GoBack()
        {
            ThrowIfDisposed();
            InvalidateWarning();
            _view.GoBack();
            RaiseStatus("Back requested.");
        }

        /// <summary>Traverses forward and invalidates certificate-warning consent.</summary>
        public void GoForward()
        {
            ThrowIfDisposed();
            InvalidateWarning();
            _view.GoForward();
            RaiseStatus("Forward requested.");
        }

        /// <summary>Reloads the current document and invalidates certificate-warning consent.</summary>
        public void Reload()
        {
            ThrowIfDisposed();
            InvalidateWarning();
            _view.Reload();
            RaiseStatus("Reload requested.");
        }

        /// <summary>Navigates to app-supplied opaque-origin markup without certificate association.</summary>
        /// <param name="html">The app-owned HTML document.</param>
        public void NavigateToString(string html)
        {
            ThrowIfDisposed();
            InvalidateWarning();
            _view.NavigateToString(html);
            RaiseStatus("Loading app content.");
        }

        /// <summary>Loads the packaged app-owned demo as history-neutral HostHtml.</summary>
        public void NavigateToDemo()
        {
            ThrowIfDisposed();
            InvalidateWarning();
            string html = BrowserPolicyRules.LoadDemo(_packagedDemoTemplatePath);
            _view.NavigateToString(html);
            RaiseStatus("Loading the local demo.");
        }

        /// <summary>Stops current work and invalidates any pending certificate-warning consent.</summary>
        public void Stop()
        {
            ThrowIfDisposed();
            InvalidateWarning();
            _view.Stop();
            RaiseStatus("Navigation stopped.");
        }

        /// <summary>Clears Source and invalidates any pending certificate-warning consent.</summary>
        public void ClearSource()
        {
            ThrowIfDisposed();
            InvalidateWarning();
            _view.Source = null;
            RaiseStatus("Address cleared.");
        }

        /// <summary>Retries the retained HTTPS target only after its associated warning page completed.</summary>
        public void ProceedWithCurrentWarning()
        {
            ThrowIfDisposed();
            WebKitCertificateFailureToken token = _currentWarningToken;
            if (token == null || !_warning.TryConsume(token))
            {
                RaiseStatus("No completed certificate warning is awaiting consent.");
                NotifyConsentReady(false);
                return;
            }
            _currentWarningToken = null;
            NotifyConsentReady(false);
            _view.ProceedWithCertificateFailure(token);
            RaiseStatus("Retrying the HTTPS request with your consent.");
        }

        /// <summary>Remote certificate token retained separately from the associated HostHtml navigation ID.</summary>
        private WebKitCertificateFailureToken _currentWarningToken;

        /// <summary>Associates only the next explicitly token-bound HostHtml warning proposal.</summary>
        /// <param name="sender">The public browser control.</param>
        /// <param name="args">The synchronous navigation proposal.</param>
        private void OnNavigationStarting(WebKitWebView sender, WebKitNavigationStartingEventArgs args)
        {
            bool isHostHtml = args.ContentKind == WebKitNavigationContentKind.HostHtml;
            bool associated = _warning.ObserveStarting(isHostHtml, args.NavigationId);
            if (!associated)
            {
                _currentWarningToken = null;
                NotifyConsentReady(false);
            }
        }

        /// <summary>Shows the app-owned warning page or invalidates a failed warning transition.</summary>
        /// <param name="sender">The public browser control.</param>
        /// <param name="args">The terminal navigation record.</param>
        private void OnNavigationCompleted(WebKitWebView sender, WebKitNavigationCompletedEventArgs args)
        {
            bool isHostHtml = args.ContentKind == WebKitNavigationContentKind.HostHtml;
            bool completed = args.Outcome == WebKitNavigationOutcome.Completed;
            if (_warning.ObserveCompleted(isHostHtml, args.NavigationId, completed))
            {
                NotifyConsentReady(true);
                RaiseStatus("Certificate warning loaded. Review it before choosing Proceed.");
                return;
            }
            if (!_warning.IsActive)
                _currentWarningToken = null;

            bool isRemoteHttpsFailure = args.ContentKind == WebKitNavigationContentKind.Remote &&
                args.Outcome == WebKitNavigationOutcome.Failed && args.Uri != null &&
                string.Equals(args.Uri.Scheme, "https", StringComparison.OrdinalIgnoreCase) &&
                args.CertificateFailureToken != null && args.CanProceedWithCertificateFailure;
            if (!isRemoteHttpsFailure)
            {
                if (isHostHtml)
                    RaiseStatus("Host content " + args.Outcome + ".");
                else
                    RaiseStatus("Navigation " + args.Outcome + ".");
                return;
            }

            _currentWarningToken = args.CertificateFailureToken;
            _warning.Begin(_currentWarningToken);
            NotifyConsentReady(false);
            _documentTitle = "Certificate warning for " + args.Uri.Host;
            string template = BrowserPolicyRules.LoadTemplate(_overridePath, _packagedBlockedTemplatePath);
            string warningHtml = template.Replace(BrowserPolicyRules.UriPlaceholder, BrowserPolicyRules.EscapeHtml(args.Uri.AbsoluteUri))
                .Replace(BrowserPolicyRules.TitlePlaceholder, BrowserPolicyRules.EscapeHtml(_documentTitle));
            _view.NavigateToString(warningHtml, _currentWarningToken);
            RaiseStatus("HTTPS certificate failure. Warning page loading; consent is not yet available.");
        }

        /// <summary>Tracks the title for warning-template substitution.</summary>
        /// <param name="sender">The public browser control.</param>
        /// <param name="args">The copied title change.</param>
        private void OnDocumentTitleChanged(WebKitWebView sender, WebKitDocumentTitleChangedEventArgs args)
        {
            _documentTitle = string.IsNullOrEmpty(args.DocumentTitle) ? "Certificate warning" : args.DocumentTitle;
        }

        /// <summary>Invalidates warning eligibility when Source becomes null.</summary>
        /// <param name="sender">The public browser control.</param>
        /// <param name="args">The property-change notification.</param>
        private void OnPropertyChanged(object sender, System.ComponentModel.PropertyChangedEventArgs args)
        {
            if (args.PropertyName == "Source" && _view.Source == null)
                InvalidateWarning();
        }

        /// <summary>Revokes the exact remote decision and notifies the shell that consent is unavailable.</summary>
        private void InvalidateWarning()
        {
            _warning.Invalidate();
            _currentWarningToken = null;
            NotifyConsentReady(false);
        }

        /// <summary>Publishes current consent-button availability to the shell.</summary>
        /// <param name="ready">Whether the associated warning document completed.</param>
        private void NotifyConsentReady(bool ready)
        {
            Action<bool> handler = WarningConsentReady;
            if (handler != null)
                handler(ready);
        }

        /// <summary>Publishes policy status text to the ViewModel.</summary>
        /// <param name="text">The user-facing status message.</param>
        private void RaiseStatus(string text)
        {
            Action<string> handler = StatusChanged;
            if (handler != null)
                handler(text);
        }

        /// <summary>Rejects policy calls after event roots and token state are disposed.</summary>
        private void ThrowIfDisposed()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(BrowserPolicy));
        }

        /// <summary>Revokes all event subscriptions and warning eligibility.</summary>
        public void Dispose()
        {
            if (_disposed)
                return;
            _disposed = true;
            _view.NavigationStarting -= OnNavigationStarting;
            _view.NavigationCompleted -= OnNavigationCompleted;
            _view.DocumentTitleChanged -= OnDocumentTitleChanged;
            _view.PropertyChanged -= OnPropertyChanged;
            _warning.Invalidate();
            _currentWarningToken = null;
            WarningConsentReady = null;
            StatusChanged = null;
        }
    }
}
#endif
