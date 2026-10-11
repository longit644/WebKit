using System;
using WebKitMiniBrowser.Models;

namespace WebKitMiniBrowser.Services.Interfaces
{
    /// <summary>Provides a view-model-facing boundary over browser state and policy operations.</summary>
    public interface IBrowserService : IDisposable
    {
        /// <summary>Gets a copied snapshot of current browser state.</summary>
        BrowserNavigationState CurrentState { get; }

        /// <summary>Raised when the public control publishes a new state snapshot.</summary>
        event EventHandler<BrowserNavigationState> StateChanged;

        /// <summary>Raised when app-owned navigation policy updates its status message.</summary>
        event Action<string> StatusChanged;

        /// <summary>Raised when certificate warning consent becomes available or unavailable.</summary>
        event Action<bool> WarningConsentReady;

        /// <summary>Navigates to a validated absolute HTTP(S) URI.</summary>
        void Navigate(Uri uri);

        /// <summary>Navigates to the previous history entry through policy.</summary>
        void GoBack();

        /// <summary>Navigates to the next history entry through policy.</summary>
        void GoForward();

        /// <summary>Reloads the current document through policy.</summary>
        void Reload();

        /// <summary>Stops the current navigation through policy.</summary>
        void Stop();

        /// <summary>Loads the app-owned demo through policy.</summary>
        void LoadDemo();

        /// <summary>Clears the current source through policy.</summary>
        void ClearSource();

        /// <summary>Retries a certificate failure only after explicit warning-page consent.</summary>
        void ProceedWithWarning();
    }
}
