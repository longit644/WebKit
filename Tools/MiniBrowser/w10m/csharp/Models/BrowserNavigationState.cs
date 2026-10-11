using System;

namespace WebKitMiniBrowser.Models
{
    /// <summary>Immutable browser state copied for binding by the presentation layer.</summary>
    public sealed class BrowserNavigationState : EventArgs
    {
        /// <summary>Creates one copied snapshot of the public browser control state.</summary>
        /// <param name="source">The current committed source URI, if present.</param>
        /// <param name="documentTitle">The currently committed document title.</param>
        /// <param name="canGoBack">Whether a previous history entry is available.</param>
        /// <param name="canGoForward">Whether a forward history entry is available.</param>
        /// <param name="isLoading">Whether the main document is loading.</param>
        public BrowserNavigationState(Uri source, string documentTitle, bool canGoBack,
            bool canGoForward, bool isLoading)
        {
            Source = source;
            DocumentTitle = documentTitle ?? string.Empty;
            CanGoBack = canGoBack;
            CanGoForward = canGoForward;
            IsLoading = isLoading;
        }

        /// <summary>Gets the current committed source URI.</summary>
        public Uri Source { get; private set; }

        /// <summary>Gets the current document title.</summary>
        public string DocumentTitle { get; private set; }

        /// <summary>Gets whether a previous history entry exists.</summary>
        public bool CanGoBack { get; private set; }

        /// <summary>Gets whether a forward history entry exists.</summary>
        public bool CanGoForward { get; private set; }

        /// <summary>Gets whether the main document is loading.</summary>
        public bool IsLoading { get; private set; }
    }
}
