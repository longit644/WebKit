using System;
using WebKit;
using WebKitMiniBrowser.Models;
using WebKitMiniBrowser.Services.Interfaces;

namespace WebKitMiniBrowser.Services
{
    /// <summary>Adapts the XAML-created WebKit control and browser policy for view models.</summary>
    public sealed class BrowserService : IBrowserService
    {
        /// <summary>The page-owned public browser control observed by this service.</summary>
        private readonly WebKitWebView _browser;

        /// <summary>The app-owned policy mediating all navigation requests.</summary>
        private readonly BrowserPolicy _policy;

        /// <summary>Whether event subscriptions and policy resources have been released.</summary>
        private bool _disposed;

        /// <summary>Creates the page-scoped browser service over the public control and policy.</summary>
        /// <param name="browser">The XAML-created public WebKit control.</param>
        /// <param name="policy">The app-owned navigation policy for this control.</param>
        public BrowserService(WebKitWebView browser, BrowserPolicy policy)
        {
            _browser = browser ?? throw new ArgumentNullException(nameof(browser));
            _policy = policy ?? throw new ArgumentNullException(nameof(policy));
            _browser.PropertyChanged += OnBrowserPropertyChanged;
            _browser.SourceChanged += OnSourceChanged;
            _browser.HistoryChanged += OnHistoryChanged;
            _browser.DocumentTitleChanged += OnDocumentTitleChanged;
            _browser.NavigationCompleted += OnNavigationCompleted;
            _policy.StatusChanged += OnStatusChanged;
            _policy.WarningConsentReady += OnWarningConsentReady;
        }

        /// <inheritdoc />
        public BrowserNavigationState CurrentState
        {
            get
            {
                ThrowIfDisposed();
                return new BrowserNavigationState(
                    _browser.Source,
                    _browser.DocumentTitle,
                    _browser.CanGoBack,
                    _browser.CanGoForward,
                    _browser.IsLoading);
            }
        }

        /// <inheritdoc />
        public event EventHandler<BrowserNavigationState> StateChanged;

        /// <inheritdoc />
        public event Action<string> StatusChanged;

        /// <inheritdoc />
        public event Action<bool> WarningConsentReady;

        /// <inheritdoc />
        public void Navigate(Uri uri) { ThrowIfDisposed(); _policy.Navigate(uri); }

        /// <inheritdoc />
        public void GoBack() { ThrowIfDisposed(); _policy.GoBack(); }

        /// <inheritdoc />
        public void GoForward() { ThrowIfDisposed(); _policy.GoForward(); }

        /// <inheritdoc />
        public void Reload() { ThrowIfDisposed(); _policy.Reload(); }

        /// <inheritdoc />
        public void Stop() { ThrowIfDisposed(); _policy.Stop(); }

        /// <inheritdoc />
        public void LoadDemo() { ThrowIfDisposed(); _policy.NavigateToDemo(); }

        /// <inheritdoc />
        public void ClearSource() { ThrowIfDisposed(); _policy.ClearSource(); }

        /// <inheritdoc />
        public void ProceedWithWarning() { ThrowIfDisposed(); _policy.ProceedWithCurrentWarning(); }

        /// <summary>Publishes a copied state after a generic browser property change.</summary>
        /// <param name="sender">The public browser control.</param>
        /// <param name="args">The changed property.</param>
        private void OnBrowserPropertyChanged(object sender, System.ComponentModel.PropertyChangedEventArgs args)
        {
            PublishState();
        }

        /// <summary>Publishes state after the committed source changes.</summary>
        /// <param name="sender">The public browser control.</param>
        /// <param name="args">The copied source-change arguments.</param>
        private void OnSourceChanged(WebKitWebView sender, WebKitSourceChangedEventArgs args)
        {
            PublishState();
        }

        /// <summary>Publishes state after history availability changes.</summary>
        /// <param name="sender">The public browser control.</param>
        /// <param name="args">The copied history state.</param>
        private void OnHistoryChanged(WebKitWebView sender, WebKitHistoryChangedEventArgs args)
        {
            PublishState();
        }

        /// <summary>Publishes state after the document title changes.</summary>
        /// <param name="sender">The public browser control.</param>
        /// <param name="args">The copied document title.</param>
        private void OnDocumentTitleChanged(WebKitWebView sender, WebKitDocumentTitleChangedEventArgs args)
        {
            PublishState();
        }

        /// <summary>Publishes state after a main navigation reaches its terminal outcome.</summary>
        /// <param name="sender">The public browser control.</param>
        /// <param name="args">The copied navigation result.</param>
        private void OnNavigationCompleted(WebKitWebView sender, WebKitNavigationCompletedEventArgs args)
        {
            PublishState();
        }

        /// <summary>Forwards app-owned policy status to the view model.</summary>
        /// <param name="message">The status selected by browser policy.</param>
        private void OnStatusChanged(string message)
        {
            if (_disposed)
                return;
            StatusChanged?.Invoke(message);
        }

        /// <summary>Forwards certificate-consent readiness to the view model.</summary>
        /// <param name="ready">Whether explicit consent is available.</param>
        private void OnWarningConsentReady(bool ready)
        {
            if (_disposed)
                return;
            WarningConsentReady?.Invoke(ready);
        }

        /// <summary>Creates and publishes a new immutable browser-state snapshot.</summary>
        private void PublishState()
        {
            if (_disposed)
                return;
            StateChanged?.Invoke(this, CurrentState);
        }

        /// <summary>Rejects service calls after control and policy subscriptions are revoked.</summary>
        private void ThrowIfDisposed()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(BrowserService));
        }

        /// <summary>Revokes control and policy subscriptions owned by this service.</summary>
        public void Dispose()
        {
            if (_disposed)
                return;

            _disposed = true;
            _browser.PropertyChanged -= OnBrowserPropertyChanged;
            _browser.SourceChanged -= OnSourceChanged;
            _browser.HistoryChanged -= OnHistoryChanged;
            _browser.DocumentTitleChanged -= OnDocumentTitleChanged;
            _browser.NavigationCompleted -= OnNavigationCompleted;
            _policy.StatusChanged -= OnStatusChanged;
            _policy.WarningConsentReady -= OnWarningConsentReady;
            _policy.Dispose();
            StateChanged = null;
            StatusChanged = null;
            WarningConsentReady = null;
        }
    }
}
